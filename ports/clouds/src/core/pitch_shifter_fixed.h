// Clouds' pitch shifter (clouds/dsp/fx/pitch_shifter.h: two crossfaded
// taps sweeping a delay line, on FxEngine<4096, FORMAT_16_BIT>) in fixed
// point, upstream's buffer layout and order of operations.
//
// The phase is a 32-bit fraction; its per-sample increment (1 - ratio) /
// size is computed per block (upstream sets ratio and size once per block),
// so there is no division per sample. Upstream wraps the phase into
// (0, 1]; here it wraps modulo 1 (only an exact 0 differs).

#ifndef OP1CLOUDS_CORE_PITCH_SHIFTER_FIXED_H_
#define OP1CLOUDS_CORE_PITCH_SHIFTER_FIXED_H_

#include <cstddef>
#include <cstdint>

#include "src/core/config.h"
#include "src/core/dsp_fixed.h"

namespace op1clouds {

class PitchShifterFixed {
 public:
  static const int32_t kSize = 1 << kPitchShifterBufferBits;
  static const int32_t kMask = kSize - 1;
  // Two lines of kPitchShifterMaxSize, one sample apart (FxEngine).
  static const int32_t kLineLength = kPitchShifterMaxSize;
  static const int32_t kRightBase = kLineLength + 1;
  static_assert(kRightBase + kLineLength <= kSize, "pitch shifter memory full");

  void Init(int16_t* buffer, bool clear_buffers = true) {
    buffer_ = buffer;
    Clear(clear_buffers);
    phase_ = 0;
    size_ = kPitchShifterMaxSize << 16;
    increment_ = 0;
  }

  void Clear(bool clear_buffer = true) {
    if (clear_buffer) {
      for (int32_t i = 0; i < kSize; ++i) buffer_[i] = 0;
    }
    write_ptr_ = 0;
  }

  // Per block, in upstream's order: set_ratio, set_size (one-pole 0.05 on
  // the window size), then the per-sample increment.
  void Prepare(FFloat ratio, int32_t size_q16) {
    // target = 128 + (2047 - 128) size^3 (scaled), Q16 samples.
    const FFloat s = Q16ToFFloat(size_q16);
    // Compile-time constants: FFloat(double) at run time is soft-float.
    static constexpr FFloat kMin = FFloat(static_cast<double>(kPitchShifterMinSize));
    static constexpr FFloat kRange = FFloat(static_cast<double>(kPitchShifterMaxSize - kPitchShifterMinSize));
    const FFloat target = kMin + kRange * (s * s * s);
    const int32_t target_q16 = target.ToQ(16);
    size_ += MulQ31Fast(target_q16 - size_, Q(0.05, 31));
    // (1 - ratio) / size, as a 32-bit phase fraction (signed).
    static constexpr FFloat one = FFloat(1.0);
    increment_ = ((one - ratio) * FFloat::Reciprocal(FFloat::FromQ(size_, 16))).ToQ(32);
  }

  // r == nullptr: mono (left line only).
  void Process(int32_t* l, int32_t* r, size_t size) {
    int16_t* const buffer = buffer_;
    int32_t wp = write_ptr_;
    uint32_t phase = phase_;
    const uint32_t increment = static_cast<uint32_t>(increment_);
    const int32_t window = size_;
    const int32_t half_window = size_ >> 1;
    while (size--) {
      wp = (wp - 1) & kMask;
      phase += increment;
      // tri = 2 min(phase, 1 - phase), Q31 (1.0 saturates).
      uint32_t tri_u = phase < 0x80000000u ? phase : 0u - phase;
      const int32_t tri = tri_u >= 0x80000000u ? kQ31One : static_cast<int32_t>(tri_u);
      const int32_t anti = kQ31One - tri;
      // Tap offsets in samples, Q16.
      const int32_t offset = MulQ31Fast(static_cast<int32_t>(phase >> 1), window);
      int32_t half = offset + half_window;
      if (half >= window) half -= window;
      *l = Tap(buffer, wp, 0, *l, offset, half, tri, anti);
      ++l;
      if (r) {
        *r = Tap(buffer, wp, kRightBase, *r, offset, half, tri, anti);
        ++r;
      }
    }
    write_ptr_ = wp;
    phase_ = phase;
  }

 private:
  // Write the input at the line's start, then the two interpolated taps.
  static inline int32_t Tap(int16_t* buffer, int32_t wp, int32_t base, int32_t in,
                            int32_t offset, int32_t half, int32_t tri, int32_t anti) {
    buffer[(wp + base) & kMask] = Q27ToS16(in);
    return MulQ31Fast(Read(buffer, wp + base, offset), tri) +
           MulQ31Fast(Read(buffer, wp + base, half), anti);
  }

  static inline int32_t Read(const int16_t* buffer, int32_t p, int32_t offset_q16) {
    const int32_t i = p + (offset_q16 >> 16);
    const int32_t a = static_cast<int32_t>(buffer[i & kMask]) << 12;
    const int32_t b = static_cast<int32_t>(buffer[(i + 1) & kMask]) << 12;
    return a + MulQ31Fast(b - a, (offset_q16 & 0xffff) << 15);
  }

  int16_t* buffer_;
  int32_t write_ptr_;
  uint32_t phase_;
  int32_t size_;        // window, Q16 samples
  int32_t increment_;   // per sample, 32-bit phase fraction
};

}  // namespace op1clouds

#endif  // OP1CLOUDS_CORE_PITCH_SHIFTER_FIXED_H_
