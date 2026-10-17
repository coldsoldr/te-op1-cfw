// The grain overlap-add kernels: one run of one grain (no buffer wrap, a
// linear envelope ramp) added into the output, in C and in Blackfin
// assembly (grain_kernels_bfin.S) with exactly the same arithmetic. Checked
// by tests/test_kernels.cc (make test).
//
// The arithmetic is the Blackfin's 16-bit dual-MAC arithmetic:
//   Fr(a, b): a 16 x 16 fractional product, (a b) << 1;
//   ExtractT: an accumulator's high half, truncated, saturated to 16 bits.
// Per sample:
//   t = (phase & 0xffff) >> 1 (Q15);
//   linear: s = ExtractT(Fr(x0, 32767 - t) + Fr(x1, t));
//   Hermite (Laurent de Soras's 4-point, as Catmull-Rom weights of t):
//     t2 = ExtractT(Fr(t, t)), t3 = ExtractT(Fr(t2, t)),
//     wm1 = t2 - (t3 >> 1) - (t >> 1),  w2 = (t3 - t2) >> 1,
//     w1 = min(2 t2 + (t >> 1) - t3 - (t3 >> 1), 32767),
//     w0 = min(32767 - wm1 - w1 - w2, 32767)   (the weights sum to 1);
//     s = ExtractT(sum of Fr(x_k, w_k)) over xm1, x0, x1, x2;
//   stereo pan (gains Q14, i.e. half: a mix of two full-scale sides fits):
//     ml = ExtractT(Fr(l, gl) + Fr(r, cr)), mr = ExtractT(Fr(r, gr) + Fr(l, cl))
//     (packed as the dual MACs need them: gains_a = gl | gr, gains_b = cr | cl);
//   mono: m = ExtractT(Fr(s, g));
//   out += (Fr(m, env >> 16) saturated to 32 bits) >>> 3   (Q27);
//   env += env_step; phase += increment.
// Samples are read at base + (phase >> 16) (+1 linear, +3 Hermite); the
// right channel r_offset bytes further. The caller keeps every read below
// the buffer's end plus its 8-sample mirror.

#ifndef OP1CLOUDS_CORE_GRAIN_KERNEL_H_
#define OP1CLOUDS_CORE_GRAIN_KERNEL_H_

#include <cstdint>

namespace op1clouds {

// Layout shared with the assembly (byte offsets in comments).
struct GrainKernel {
  const int16_t* base;      // 0: left samples, + first sample
  int32_t r_offset;         // 4: right channel's samples - left's, bytes
  int32_t phase;            // 8: Q16, read position from base
  int32_t increment;        // 12: Q16
  int32_t env;              // 16: Q31
  int32_t env_step;         // 20
  int32_t gains_a;          // 24: stereo gl (low) | gr (high), mono g (low); Q14
  int32_t gains_b;          // 28: stereo cr (low) | cl (high)
};

extern "C" void GrainLinearStereoAsm(GrainKernel* k, int32_t* out_l, int32_t* out_r, int32_t n);
extern "C" void GrainHermiteStereoAsm(GrainKernel* k, int32_t* out_l, int32_t* out_r, int32_t n);
extern "C" void GrainLinearMonoAsm(GrainKernel* k, int32_t* out, int32_t n);
extern "C" void GrainHermiteMonoAsm(GrainKernel* k, int32_t* out, int32_t n);

namespace grain_kernel_detail {

inline int64_t Fr(int32_t a, int32_t b) {
  if (a == -32768 && b == -32768) return 0x7fffffff;
  return static_cast<int64_t>(a * b) * 2;
}

inline int32_t ExtractT(int64_t acc) {
  int64_t x = acc >> 16;
  if (x > 32767) x = 32767;
  if (x < -32768) x = -32768;
  return static_cast<int32_t>(x);
}

inline int32_t Sat32(int64_t x) {
  if (x > INT32_MAX) return INT32_MAX;
  if (x < INT32_MIN) return INT32_MIN;
  return static_cast<int32_t>(x);
}

inline int32_t Lo(int32_t x) { return static_cast<int16_t>(x & 0xffff); }
inline int32_t Hi(int32_t x) { return static_cast<int16_t>(static_cast<uint32_t>(x) >> 16); }

inline int32_t Linear(const int16_t* s, int32_t t) {
  return ExtractT(Fr(s[0], 32767 - t) + Fr(s[1], t));
}

struct HermiteWeights {
  int32_t wm1, w0, w1, w2;
  explicit HermiteWeights(int32_t t) {
    const int32_t t2 = ExtractT(Fr(t, t));
    const int32_t t3 = ExtractT(Fr(t2, t));
    wm1 = t2 - (t3 >> 1) - (t >> 1);
    w2 = (t3 - t2) >> 1;
    w1 = 2 * t2 + (t >> 1) - t3 - (t3 >> 1);
    if (w1 > 32767) w1 = 32767;   // 32768 at t = 32767
    w0 = 32767 - wm1 - w1 - w2;
    if (w0 > 32767) w0 = 32767;
  }
  int32_t Read(const int16_t* s) const {
    return ExtractT(Fr(s[0], wm1) + Fr(s[1], w0) + Fr(s[2], w1) + Fr(s[3], w2));
  }
};

inline int32_t EnvOut(int32_t m, int32_t env) {
  return Sat32(Fr(m, env >> 16)) >> 3;
}

}  // namespace grain_kernel_detail

inline void GrainLinearStereoC(GrainKernel* k, int32_t* out_l, int32_t* out_r, int32_t n) {
  using namespace grain_kernel_detail;
  const int16_t* r_base = reinterpret_cast<const int16_t*>(
      reinterpret_cast<const uint8_t*>(k->base) + k->r_offset);
  int32_t phase = k->phase, env = k->env;
  for (int32_t i = 0; i < n; ++i) {
    const int32_t index = phase >> 16;
    const int32_t t = (phase & 0xffff) >> 1;
    const int32_t l = Linear(k->base + index, t);
    const int32_t r = Linear(r_base + index, t);
    const int32_t ml = ExtractT(Fr(l, Lo(k->gains_a)) + Fr(r, Lo(k->gains_b)));
    const int32_t mr = ExtractT(Fr(r, Hi(k->gains_a)) + Fr(l, Hi(k->gains_b)));
    out_l[i] += EnvOut(ml, env);
    out_r[i] += EnvOut(mr, env);
    env += k->env_step;
    phase += k->increment;
  }
  k->phase = phase;
  k->env = env;
}

inline void GrainHermiteStereoC(GrainKernel* k, int32_t* out_l, int32_t* out_r, int32_t n) {
  using namespace grain_kernel_detail;
  const int16_t* r_base = reinterpret_cast<const int16_t*>(
      reinterpret_cast<const uint8_t*>(k->base) + k->r_offset);
  int32_t phase = k->phase, env = k->env;
  for (int32_t i = 0; i < n; ++i) {
    const int32_t index = phase >> 16;
    const HermiteWeights w((phase & 0xffff) >> 1);
    const int32_t l = w.Read(k->base + index);
    const int32_t r = w.Read(r_base + index);
    const int32_t ml = ExtractT(Fr(l, Lo(k->gains_a)) + Fr(r, Lo(k->gains_b)));
    const int32_t mr = ExtractT(Fr(r, Hi(k->gains_a)) + Fr(l, Hi(k->gains_b)));
    out_l[i] += EnvOut(ml, env);
    out_r[i] += EnvOut(mr, env);
    env += k->env_step;
    phase += k->increment;
  }
  k->phase = phase;
  k->env = env;
}

inline void GrainLinearMonoC(GrainKernel* k, int32_t* out, int32_t n) {
  using namespace grain_kernel_detail;
  int32_t phase = k->phase, env = k->env;
  for (int32_t i = 0; i < n; ++i) {
    const int32_t s = Linear(k->base + (phase >> 16), (phase & 0xffff) >> 1);
    out[i] += EnvOut(ExtractT(Fr(s, Lo(k->gains_a))), env);
    env += k->env_step;
    phase += k->increment;
  }
  k->phase = phase;
  k->env = env;
}

inline void GrainHermiteMonoC(GrainKernel* k, int32_t* out, int32_t n) {
  using namespace grain_kernel_detail;
  int32_t phase = k->phase, env = k->env;
  for (int32_t i = 0; i < n; ++i) {
    const HermiteWeights w((phase & 0xffff) >> 1);
    const int32_t s = w.Read(k->base + (phase >> 16));
    out[i] += EnvOut(ExtractT(Fr(s, Lo(k->gains_a))), env);
    env += k->env_step;
    phase += k->increment;
  }
  k->phase = phase;
  k->env = env;
}

// The build's kernels: assembly on Blackfin, C elsewhere.
inline void GrainStereo(bool hermite, GrainKernel* k, int32_t* out_l, int32_t* out_r, int32_t n) {
#if defined(__bfin__) && !defined(OP1CLOUDS_NO_ASM)
  if (hermite) GrainHermiteStereoAsm(k, out_l, out_r, n); else GrainLinearStereoAsm(k, out_l, out_r, n);
#else
  if (hermite) GrainHermiteStereoC(k, out_l, out_r, n); else GrainLinearStereoC(k, out_l, out_r, n);
#endif
}

inline void GrainMono(bool hermite, GrainKernel* k, int32_t* out, int32_t n) {
#if defined(__bfin__) && !defined(OP1CLOUDS_NO_ASM)
  if (hermite) GrainHermiteMonoAsm(k, out, n); else GrainLinearMonoAsm(k, out, n);
#else
  if (hermite) GrainHermiteMonoC(k, out, n); else GrainLinearMonoC(k, out, n);
#endif
}

}  // namespace op1clouds

#endif  // OP1CLOUDS_CORE_GRAIN_KERNEL_H_
