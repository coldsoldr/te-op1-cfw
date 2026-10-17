// Clouds' looping delay player (clouds/dsp/looping_sample_player.h) in fixed
// point, upstream's structure and order of operations.
//
// Not frozen: a delay line read (Hermite) at a delay smoothed towards
// position x max_delay per sample (one-pole 0.00005 at 32 kHz). Frozen: a
// loop of the frozen buffer, played at the pitch ratio, with a crossfade
// between the loop's end and its restart.
//
// Formats: the smoothed delay in Q15 samples (int32: the longest buffer,
// ~45k samples at 44.1 kHz, fits); phases, loop points and durations in Q16
// samples, unsigned. Read positions use upstream's 12 fractional bits.
// Upstream's float delay smoothing stops moving once the remaining error is
// below its float resolution (~40 samples at the far end of the buffer);
// this one keeps converging, to ~0.3 sample.
//
// The crossfade gain, phase / tail_duration, is phase x a reciprocal taken
// when the tail duration changes (at a loop restart), in FFloat, only
// during the crossfade (at most 64 samples per loop).

#ifndef OP1CLOUDS_CORE_LOOPING_PLAYER_H_
#define OP1CLOUDS_CORE_LOOPING_PLAYER_H_

#include <cstddef>
#include <cstdint>

#include "src/core/config.h"
#include "src/core/dsp_fixed.h"
#include "src/core/params.h"
#include "src/core/record_buffer.h"

namespace op1clouds {

class LoopingPlayer {
 public:
  void Init(int32_t num_channels) {
    num_channels_ = num_channels;
    phase_ = 0;
    current_delay_ = 0;
    loop_point_ = 0;
    loop_duration_ = 0;
    tail_start_ = 0;
    tail_duration_ = 1 << 16;
    static constexpr FFloat kOne = FFloat(1.0);
    tail_reciprocal_ = kOne;
    loop_reset_ = 0;
    tap_delay_ = 0;
    tap_delay_counter_ = 0;
    synchronized_ = false;
  }

  bool synchronized() const { return synchronized_; }

  // out_l, out_r: size Q27 samples (out_r ignored in mono).
  void Play(const RecordBuffer* buffer, const CloudsParams& p, int32_t* out_l, int32_t* out_r,
            int32_t size) {
    const int32_t max_delay = buffer->size() - kLoopCrossfade;
    tap_delay_counter_ += size;
    if (tap_delay_counter_ > max_delay) {
      tap_delay_ = 0;
      tap_delay_counter_ = 0;
      synchronized_ = false;
    }
    if (p.trigger) {
      tap_delay_ = tap_delay_counter_;
      tap_delay_counter_ = 0;
      synchronized_ = tap_delay_ > 128;
      loop_reset_ = phase_;
      phase_ = 0;
    }
    if (!p.freeze) {
      PlayDelay(buffer, p, max_delay, out_l, out_r, size);
    } else {
      PlayLoop(buffer, p, max_delay, out_l, out_r, size);
    }
  }

 private:
  void PlayDelay(const RecordBuffer* buffer, const CloudsParams& p, int32_t max_delay,
                 int32_t* out_l, int32_t* out_r, int32_t size) {
    // position x max_delay in Q15 samples.
    int32_t target = static_cast<int32_t>((static_cast<int64_t>(p.position) * max_delay) >> 1);
    if (synchronized_) target = tap_delay_ << 15;
    const int32_t head = buffer[0].head();
    const int32_t buffer_size = buffer[0].size();
    for (int32_t i = 0; i < size; ++i) {
      current_delay_ += MulQ31Fast(target - current_delay_, kDelaySmoothingQ31);
      // Upstream: (head - 4 - remaining + size) << 12, minus delay x 4096.
      const int32_t remaining = size - 1 - i;
      const int32_t position =
          ((head - 4 - remaining + buffer_size) << 12) - (current_delay_ >> 3);
      const int32_t integral = position >> 12;
      const uint32_t fractional = static_cast<uint32_t>(position << 4);
      const int32_t l = buffer[0].ReadHermite(integral, fractional);
      out_l[i] = l;
      if (num_channels_ == 2) out_r[i] = buffer[1].ReadHermite(integral, fractional);
    }
    phase_ = 0;
  }

  void PlayLoop(const RecordBuffer* buffer, const CloudsParams& p, int32_t max_delay,
                int32_t* out_l, int32_t* out_r, int32_t size) {
    // Loop point and duration, Q16 samples (unsigned: up to 65536 samples).
    // position x max_delay x 15 / 16 + crossfade.
    uint32_t loop_point = static_cast<uint32_t>(
        (static_cast<uint64_t>(p.position) * static_cast<uint32_t>(max_delay) * 15u) >> 4);
    loop_point += static_cast<uint32_t>(kLoopCrossfade) << 16;
    // (0.01 + 0.99 d^3) x max_delay.
    const FFloat d = Q16ToFFloat(p.size);
    static constexpr FFloat k001 = FFloat(0.01);
    static constexpr FFloat k099 = FFloat(0.99);
    const FFloat duration = (k001 + k099 * d * d * d) * FFloat::FromInt(max_delay);
    uint32_t loop_duration = static_cast<uint32_t>(duration.ToQ(15)) << 1;
    if (synchronized_) loop_duration = static_cast<uint32_t>(tap_delay_) << 16;
    const uint32_t max_delay_q16 = static_cast<uint32_t>(max_delay) << 16;
    if (loop_point + loop_duration >= max_delay_q16) {
      loop_point = max_delay_q16 - loop_duration;
    }
    // The phase increment, Q16 (the pitch ratio, at most 2^(127/12)).
    const uint32_t phase_increment =
        synchronized_ ? (1u << 16) : static_cast<uint32_t>(SemitonesToRatio(p.pitch).ToQ(16));

    const int32_t delay_int = (buffer[0].head() - 4 + buffer[0].size()) << 12;
    for (int32_t i = 0; i < size; ++i) {
      if (phase_ >= loop_duration_ || phase_ == 0) {
        if (phase_ >= loop_duration_) loop_reset_ = loop_duration_;
        if (loop_reset_ >= loop_duration_) loop_reset_ = loop_duration_;
        tail_start_ = loop_duration_ - loop_reset_ + loop_point_;
        phase_ = 0;
        // min(64, 64 x increment), and 1 / it for the gain.
        const uint32_t tail = phase_increment < (1u << 16)
            ? static_cast<uint32_t>(kLoopCrossfade) * phase_increment
            : static_cast<uint32_t>(kLoopCrossfade) << 16;
        if (tail != tail_duration_) {
          tail_duration_ = tail;
          tail_reciprocal_ = tail ? FFloat::Reciprocal(FFloat::FromQ(static_cast<int32_t>(tail), 16))
                                  : FFloat();
        }
        loop_point_ = loop_point;
        loop_duration_ = loop_duration;
      }
      phase_ += phase_increment;
      // gain = clamp(phase / tail, 0, 1), Q31.
      int32_t gain = kQ31One;
      if (tail_duration_ != 0 && phase_ < tail_duration_) {
        gain = (FFloat::FromQ(static_cast<int32_t>(phase_), 16) * tail_reciprocal_).ToQ(31);
      }
      // Upstream: delay_int - (loop_duration - phase + loop_point) x 4096.
      const int32_t back = static_cast<int32_t>((loop_duration_ - phase_ + loop_point_) >> 4);
      const int32_t position = delay_int - back;
      int32_t l = buffer[0].ReadHermite(position >> 12, static_cast<uint32_t>(position << 4));
      int32_t r = num_channels_ == 2
          ? buffer[1].ReadHermite(position >> 12, static_cast<uint32_t>(position << 4)) : 0;
      if (gain != kQ31One) {
        l = MulQ31Fast(l, gain);
        r = MulQ31Fast(r, gain);
        const int32_t fade = kQ31One - gain;
        // Upstream: delay_int - (tail_start - phase) x 4096.
        const int32_t tail_back = static_cast<int32_t>(tail_start_ >> 4) - static_cast<int32_t>(phase_ >> 4);
        const int32_t tail_position = delay_int - tail_back;
        l += MulQ31Fast(buffer[0].ReadHermite(tail_position >> 12, static_cast<uint32_t>(tail_position << 4)), fade);
        if (num_channels_ == 2) {
          r += MulQ31Fast(buffer[1].ReadHermite(tail_position >> 12, static_cast<uint32_t>(tail_position << 4)), fade);
        }
      }
      out_l[i] = l;
      if (num_channels_ == 2) out_r[i] = r;
    }
  }

  int32_t num_channels_;
  uint32_t phase_;            // Q16 samples
  int32_t current_delay_;     // Q15 samples
  uint32_t loop_point_;       // Q16
  uint32_t loop_duration_;    // Q16
  uint32_t tail_start_;       // Q16
  uint32_t tail_duration_;    // Q16
  FFloat tail_reciprocal_;    // 1 / tail_duration (in samples)
  uint32_t loop_reset_;       // Q16
  int32_t tap_delay_;
  int32_t tap_delay_counter_;
  bool synchronized_;
};

}  // namespace op1clouds

#endif  // OP1CLOUDS_CORE_LOOPING_PLAYER_H_
