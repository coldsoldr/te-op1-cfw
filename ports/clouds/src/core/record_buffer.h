// Clouds' recording buffer (clouds/dsp/audio_buffer.h, 16-bit resolution)
// in fixed point: int16 samples, Q27 in and out.
//
// As upstream: a circular buffer of size_ = words - 8 samples whose first 8
// samples are mirrored after its end (kInterpolationTail), so a read at
// index i < size_ can fetch i..i+3 without wrapping; and while frozen, the
// incoming signal is kept in a 256-sample tail to crossfade with when
// recording resumes. Upstream's fast write path scales by 32767 and the
// slow one by 32768; both are kept.

#ifndef OP1CLOUDS_CORE_RECORD_BUFFER_H_
#define OP1CLOUDS_CORE_RECORD_BUFFER_H_

#include <cstdint>

#include "src/core/dsp_fixed.h"

namespace op1clouds {

const int32_t kCrossFadeSize = 256;
const int32_t kInterpolationTail = 8;

class RecordBuffer {
 public:
  // tail: kCrossFadeSize + 1 words (upstream reads one past its 256-word
  // tail, with a zero gain).
  void Init(int16_t* buffer, int32_t words, int16_t* tail) {
    s16_ = buffer;
    size_ = words - kInterpolationTail;
    write_head_ = 0;
    crossfade_counter_ = 0;
    for (int32_t i = 0; i < words; ++i) s16_[i] = 0;
    tail_ = tail;
    for (int32_t i = 0; i <= kCrossFadeSize; ++i) tail_[i] = 0;
  }

  inline void Write(int32_t in) {
    s16_[write_head_] = Q27ToS16(in);
    if (write_head_ < kInterpolationTail) {
      s16_[write_head_ + size_] = s16_[write_head_];
    }
    ++write_head_;
    if (write_head_ >= size_) write_head_ = 0;
  }

  // in: Q27 samples with stride.
  void WriteFade(const int32_t* in, int32_t size, int32_t stride, bool write) {
    if (!write) {
      // Keep recording into the tail, to crossfade with when recording
      // resumes.
      if (crossfade_counter_ < kCrossFadeSize) {
        while (size--) {
          if (crossfade_counter_ < kCrossFadeSize) {
            tail_[crossfade_counter_++] = Q27ToS16Scaled32767(*in);
            in += stride;
          }
        }
      }
    } else if (!crossfade_counter_ && write_head_ >= kInterpolationTail &&
               write_head_ < (size_ - size)) {
      // Upstream's fast path.
      int16_t* s = s16_ + write_head_;
      write_head_ += size;
      while (size--) {
        *s++ = Q27ToS16Scaled32767(*in);
        in += stride;
      }
    } else {
      while (size--) {
        int32_t sample = *in;
        if (crossfade_counter_) {
          --crossfade_counter_;
          const int32_t tail_sample = static_cast<int32_t>(tail_[kCrossFadeSize - crossfade_counter_]) << 12;
          // gain = counter / 256, Q31.
          sample += MulQ31Fast(tail_sample - sample, crossfade_counter_ << 23);
        }
        Write(sample);
        in += stride;
      }
    }
  }

  // Laurent de Soras's Hermite interpolator, as upstream: reads
  // integral..integral + 3 (integral < 2 size_), fractional in Q16.
  inline int32_t ReadHermite(int32_t integral, uint32_t fractional) const {
    if (integral >= size_) integral -= size_;
    const int16_t* s = s16_ + integral;
    const int32_t xm1 = static_cast<int32_t>(s[0]) << 12;
    const int32_t x0 = static_cast<int32_t>(s[1]) << 12;
    const int32_t x1 = static_cast<int32_t>(s[2]) << 12;
    const int32_t x2 = static_cast<int32_t>(s[3]) << 12;
    const int32_t c = (x1 - xm1) >> 1;
    const int32_t v = x0 - x1;
    const int32_t w = c + v;
    const int32_t a = w + v + ((x2 - x0) >> 1);
    const int32_t b_neg = w + a;
    const int32_t t = static_cast<int32_t>((fractional & 0xffff) << 15);  // Q31
    int32_t r = MulQ31Fast(a, t) - b_neg;
    r = MulQ31Fast(r, t) + c;
    r = MulQ31Fast(r, t) + x0;
    return r;
  }

  inline int32_t ReadLinear(int32_t integral, uint32_t fractional) const {
    if (integral >= size_) integral -= size_;
    const int16_t* s = s16_ + integral;
    const int32_t x0 = static_cast<int32_t>(s[0]) << 12;
    const int32_t x1 = static_cast<int32_t>(s[1]) << 12;
    return x0 + MulQ31Fast(x1 - x0, static_cast<int32_t>((fractional & 0xffff) << 15));
  }

  inline int32_t size() const { return size_; }
  inline int32_t head() const { return write_head_; }
  inline const int16_t* samples() const { return s16_; }

 private:
  int16_t* s16_;
  int32_t size_;
  int32_t write_head_;
  int16_t* tail_;
  int32_t crossfade_counter_;
};

}  // namespace op1clouds

#endif  // OP1CLOUDS_CORE_RECORD_BUFFER_H_
