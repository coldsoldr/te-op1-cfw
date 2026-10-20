// CRUSH: sample-rate and bit-depth reduction (docs/fx-briefs/crush.md), in
// fixed point at the effect rate (44.1 kHz, 64 samples a call). No
// anti-aliasing: the aliasing is the sound.
//
// - Rate: a 32-bit phase accumulator; when it wraps, the input is sampled
//   and held. rate 0..32767 maps 44.1 kHz -> 500 Hz exponentially
//   (44100 x (500 / 44100)^r: 6.46 octaves down).
// - Bits: the held sample is rounded to 16 -> 2 bits.
// - Jitter: each new hold restarts the phase at a random fraction of a
//   period, so the holds come early by up to jitter x one period.
// - Mix: dry/wet, linear.
// Stereo shares one clock; each channel holds its own sample.

#ifndef OP1FX_SRC_CRUSH_CRUSH_H_
#define OP1FX_SRC_CRUSH_CRUSH_H_

#include <cstdint>

namespace op1fx {

struct CrushParams {
  int32_t rate;     // 0..32767
  int32_t bits;     // 0..32767: 16 bits -> 2
  int32_t jitter;   // 0..32767
  int32_t mix;      // 0..32767
};

// 2^(-i/16) in Q31, i = 0..16 (2^31 x 2^(-i/16), rounded).
const uint32_t kExp2NegQ31[17] = {
  2147483648u, 2056437387u, 1969251188u, 1885761398u, 1805811301u, 1729250827u, 1655936265u,
  1585730000u, 1518500250u, 1454120821u, 1392470869u, 1333434672u, 1276901417u, 1222764986u,
  1170923762u, 1121280436u, 1073741824u,
};

// 2^(-x) x 2^32, x in Q16 (0..32 octaves): the phase increment of a rate
// x octaves under the effect rate. Not on the per-sample path.
inline uint32_t Exp2NegQ16(int32_t x) {
  const int32_t octaves = x >> 16;
  if (octaves >= 31) return 1;
  const int32_t frac = x & 0xffff;   // Q16: 16 steps of 4096
  const int32_t i = frac >> 12;
  const uint32_t a = kExp2NegQ31[i], b = kExp2NegQ31[i + 1];
  const uint32_t m = a - static_cast<uint32_t>((static_cast<uint64_t>(a - b) * (frac & 0xfff)) >> 12);
  // m is 2^(-frac) in Q31 (2^30 < m <= 2^31): x 2 for Q32, then the octaves.
  const uint64_t q32 = static_cast<uint64_t>(m) << 1;
  const uint64_t inc = q32 >> octaves;
  return inc > 0xffffffffu ? 0xffffffffu : static_cast<uint32_t>(inc);
}

class Crush {
 public:
  void Init() {
    phase_ = 0;
    held_[0] = held_[1] = 0;
    rng_ = 0x12345678u;
  }

  // In place, r null for mono.
  void Process(int16_t* l, int16_t* r, int32_t n, const CrushParams& p) {
    // 6.462 octaves (log2(44100 / 500)) in Q16 over the knob: 423 501 / 32767.
    const int32_t octaves_q16 = static_cast<int32_t>((static_cast<int64_t>(p.rate) * 423501) >> 15);
    const uint32_t inc = Exp2NegQ16(octaves_q16);
    int32_t bits = 16 - (p.bits * 14 + 16383) / 32767;   // 16..2
    const int32_t drop = 16 - bits;
    const int32_t mask = drop ? ~((1 << drop) - 1) : ~0;
    const int32_t half = drop ? 1 << (drop - 1) : 0;
    const uint32_t jitter = static_cast<uint32_t>(p.jitter) << 1;   // Q16
    const int32_t mix = p.mix >= 32767 ? 32768 : p.mix;   // Q15, the top exactly wet
    uint32_t phase = phase_;
    int32_t hl = held_[0], hr = held_[1];
    for (int32_t i = 0; i < n; ++i) {
      const uint32_t next = phase + inc;
      if (next < phase || inc == 0xffffffffu) {
        hl = Quantise(l[i], mask, half);
        if (r) hr = Quantise(r[i], mask, half);
        rng_ = rng_ * 1664525u + 1013904223u;
        // A random fraction of a period, x jitter: the next hold comes early.
        phase = next + ((((rng_ >> 16) * jitter) >> 16) << 16);
      } else {
        phase = next;
      }
      l[i] = Mix(l[i], hl, mix);
      if (r) r[i] = Mix(r[i], hr, mix);
    }
    phase_ = phase;
    held_[0] = hl;
    held_[1] = hr;
  }

 private:
  static int32_t Quantise(int32_t x, int32_t mask, int32_t half) {
    int32_t q = x + half;
    if (q > 32767) q = 32767;
    return q & mask;
  }

  // |wet - dry| < 2^16, mix <= 2^15: the product fits 32 bits.
  static int16_t Mix(int32_t dry, int32_t wet, int32_t mix) {
    return static_cast<int16_t>(dry + (((wet - dry) * mix) >> 15));
  }

  uint32_t phase_;
  int32_t held_[2];
  uint32_t rng_;
};

}  // namespace op1fx

#endif  // OP1FX_SRC_CRUSH_CRUSH_H_
