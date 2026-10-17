// Marbles' slave ramps and ramp divider in fixed point (upstream:
// marbles/ramp/slave_ramp.h, ramp_divider.h, ramp.h). Phases are Q32 in
// int64 where they exceed 1.0; ratios are kept as integers p/q, so the
// per-sample work has no division (only a multiply by a precomputed 1/q).
//
// Based on Mutable Instruments Marbles (MIT, (c) 2015 Emilie Gillet).

#ifndef OP1MARBLES_FX_RAMPS_FX_H_
#define OP1MARBLES_FX_RAMPS_FX_H_

#include <stdint.h>

#include "src/fx.h"
#include "src/tables_fx.h"

namespace op1marbles {
namespace fx {

const int64_t kQ32One = 1LL << 32;

struct Ratio {
  int p;
  int q;
};

// ceil(2^32 / q) for q in 1..64 (exact quotient for powers of two).
inline uint32_t ReciprocalQ32(int q) {
  return q <= 1 ? 0xffffffffu : static_cast<uint32_t>((kQ32One + q - 1) / q);
}

// x / q for q in 1..64, with recip = ReciprocalQ32(q): x * recip / 2^32
// (error at most 1 LSB).
inline uint64_t DivSmall(uint64_t x, int q, uint32_t recip) {
  if (q <= 1) return x;
  uint32_t hi = static_cast<uint32_t>(x >> 32);
  uint32_t lo = static_cast<uint32_t>(x);
  return Mul64(hi, recip) + MulU32(lo, recip);
}

// x * s when (x >> 32) * s < 2^32 (no 64-bit multiply call).
inline uint64_t MulSmall64(uint64_t x, uint32_t s) {
  uint32_t hi = static_cast<uint32_t>(x >> 32);
  return (static_cast<uint64_t>(hi * s) << 32) + Mul64(static_cast<uint32_t>(x), s);
}

// Phases in Q40 (int64) holding float values, with upstream's float
// roundings applied (FloatAdd, FloatMul): without jitter the slave ramps'
// edges fall on the same samples as upstream's. Output ramps Q32.
const int64_t kQ40One = 1LL << 40;

// p / q rounded to float, for small positive integers.
inline Float24 RatioToFloat(int p, int q) {
  uint64_t num = static_cast<uint64_t>(p) << 40;
  uint64_t quotient = num / static_cast<uint64_t>(q);
  if (quotient * static_cast<uint64_t>(q) != num) quotient |= 1;  // sticky
  return Float24Normalize(quotient, -40);
}

class SlaveRamp {
 public:
  void Init() {
    phase_ = 0;
    max_phase_ = static_cast<int64_t>(kMaxRampQ32) << 8;
    ratio_ = RatioToFloat(1, 1);
    ratio_p_ = ratio_q_ = 1;
    bernoulli_ratio_ = kQ40One;
    step_ = 0;
    step_frequency_ = -1;
    step_ratio_ = 0;
    pulse_width_ = 0;
    target_ = 1;
    pulse_length_ = 0;
    bernoulli_ = false;
    must_complete_ = false;
  }

  void Reset() {
    Init();
    phase_ = kQ40One;
  }

  // Multiplied/divided rate compared to the master. pulse_width Q32.
  void Init(int pattern_length, Ratio ratio, uint32_t pulse_width) {
    bernoulli_ = false;
    phase_ = 0;
    // pattern_length * 0.9999f, rounded to float as upstream.
    max_phase_ = static_cast<int64_t>(RoundToFloat(
        static_cast<uint64_t>(pattern_length) * kMaxRampQ32)) << 8;
    ratio_ = RatioToFloat(ratio.p, ratio.q);
    ratio_p_ = ratio.p;
    ratio_q_ = ratio.q;
    pulse_width_ = pulse_width;
    target_ = 1;
    pulse_length_ = 0;
  }

  // Adaptive slope (Bernoulli models). expected_value is always 0.5 in
  // Marbles.
  void Init(bool must_complete, uint32_t pulse_width) {
    bernoulli_ = true;
    if (must_complete_) {
      phase_ = 0;
      pulse_width_ = pulse_width;
      pulse_length_ = 0;
    }
    // ratio = (1 - phase) [* 0.5 unless the ramp must complete]
    int64_t r = FloatAdd(kQ40One, -phase_);
    if (!must_complete) r /= 2;  // exact (a float halved)
    bernoulli_ratio_ = r;
    must_complete_ = must_complete;
  }

  // frequency: Q40 cycles per sample (< 1), float-valued. Writes the ramp
  // (Q32) and the gate.
  inline void Process(int64_t frequency, uint32_t* phase, bool* gate) {
    uint32_t output_phase;
    if (bernoulli_) {
      // frequency * ratio only changes at a tick: cached.
      if (frequency != step_frequency_ || bernoulli_ratio_ != step_ratio_) {
        int64_t r = bernoulli_ratio_;
        int64_t step = 0;
        if (r) {
          Float24 fr = Float24FromFixed(static_cast<uint64_t>(r < 0 ? -r : r), 40);
          step = FloatMul(frequency, fr);
          if (r < 0) step = -step;
        }
        step_ = step;
        step_frequency_ = frequency;
        step_ratio_ = bernoulli_ratio_;
      }
      phase_ = FloatAdd(phase_, step_);
      if (phase_ >= kQ40One) {
        output_phase = 0xffffffffu;  // 1.0
      } else if (phase_ < 0) {
        output_phase = 0;  // upstream would output a negative phase; unreachable in practice
      } else {
        output_phase = static_cast<uint32_t>(phase_ >> 8);
      }
    } else {
      phase_ = FloatAdd(phase_, frequency);
      if (phase_ >= max_phase_) {
        phase_ = max_phase_;
      }
      // output = phase * ratio; crossing the target restarts the pulse.
      // An integer ratio (q = 1) needs no float product: phase * p, rounded.
      int64_t op = ratio_q_ == 1
          ? static_cast<int64_t>(RoundToFloat(MulSmall64(static_cast<uint64_t>(phase_),
                                                         static_cast<uint32_t>(ratio_p_))))
          : FloatMul(phase_, ratio_);
      if (op > (static_cast<int64_t>(target_) << 40)) {
        pulse_length_ = 0;
        target_ += 1;
      }
      output_phase = static_cast<uint32_t>(static_cast<uint64_t>(op) >> 8);  // fractional part
    }
    *phase = output_phase;
    *gate = pulse_width_ == 0
        ? pulse_length_ < 32 && output_phase <= 0x80000000u
        : output_phase < pulse_width_;
    ++pulse_length_;
  }

 private:
  int64_t phase_;      // Q40
  int64_t max_phase_;  // Q40
  Float24 ratio_;
  int ratio_p_, ratio_q_;
  int64_t bernoulli_ratio_;  // Q40
  int64_t step_;             // cached frequency * bernoulli_ratio_
  int64_t step_frequency_;
  int64_t step_ratio_;
  uint32_t pulse_width_;     // Q32
  int32_t target_;
  int32_t pulse_length_;
  bool bernoulli_;
  bool must_complete_;
};

class RampDivider {
 public:
  void Init() {
    phase_ = 0;
    train_phase_ = 0;
    max_train_phase_ = kQ32One;
    divisor_ = 1;
    recip_ = ReciprocalQ32(1);
    multiplier_ = k099999fQ32;  // upstream's initial f_ratio_ = 0.99999f
    reset_counter_ = 1;
    reset_at_next_pulse_ = false;
  }

  void Reset() { reset_at_next_pulse_ = true; }

  // ratio.p must be 1 (the only ratios Marbles' Y section uses).
  void Process(Ratio ratio, const uint32_t* in, uint32_t* out, int size) {
    while (size--) {
      uint32_t new_phase = *in++;
      uint32_t frequency = new_phase - phase_;  // wraps like upstream's +1.0
      if (new_phase < phase_) {
        if (reset_at_next_pulse_) {
          reset_at_next_pulse_ = false;
          reset_counter_ = 1;
        }
        --reset_counter_;
        if (!reset_counter_) {
          train_phase_ = new_phase;
          reset_counter_ = ratio.q;
          divisor_ = ratio.q;
          recip_ = ReciprocalQ32(ratio.q);
          multiplier_ = kMaxRampQ32;  // ratio.to_float() * kMaxRampValue, p = 1
          frequency = 0;
          max_train_phase_ = static_cast<int64_t>(ratio.q) << 32;
        }
      }
      train_phase_ += frequency;
      if (train_phase_ >= max_train_phase_) {
        train_phase_ = max_train_phase_;
      }
      uint64_t t = DivSmall(static_cast<uint64_t>(train_phase_), divisor_, recip_);
      uint32_t output;
      if (t >= static_cast<uint64_t>(kQ32One)) {
        output = multiplier_;  // train_phase / q == 1: 0.9999 (or 0.99999)
      } else {
        output = MulU32(static_cast<uint32_t>(t), multiplier_);
      }
      *out++ = output;
      phase_ = new_phase;
    }
  }

 private:
  uint32_t phase_;
  int64_t train_phase_;
  int64_t max_train_phase_;
  int divisor_;
  uint32_t recip_;
  uint32_t multiplier_;
  int reset_counter_;
  bool reset_at_next_pulse_;
};

}  // namespace fx
}  // namespace op1marbles

#endif  // OP1MARBLES_FX_RAMPS_FX_H_
