// Clouds' reverb in fixed point. See reverb_stereo.h.

#include "src/core/reverb_stereo.h"

#include "src/core/dsp_fixed.h"

namespace op1clouds {

namespace {

// The layout matches upstream's lengths at this rate.
static_assert(RV_AP1_LEN == Scaled(113) && RV_AP2_LEN == Scaled(162) && RV_AP3_LEN == Scaled(241) &&
              RV_AP4_LEN == Scaled(399) && RV_DAP1A_LEN == Scaled(1653) &&
              RV_DAP1B_LEN == Scaled(2038) && RV_DEL1_LEN == Scaled(3411) &&
              RV_DAP2A_LEN == Scaled(1913) && RV_DAP2B_LEN == Scaled(1663) &&
              RV_DEL2_LEN == Scaled(4782), "reverb lengths");
static_assert(RV_AP2 == RV_AP1 + RV_AP1_LEN + 1 && RV_AP3 == RV_AP2 + RV_AP2_LEN + 1 &&
              RV_AP4 == RV_AP3 + RV_AP3_LEN + 1 && RV_DAP1A == RV_AP4 + RV_AP4_LEN + 1 &&
              RV_DAP1B == RV_DAP1A + RV_DAP1A_LEN + 1 && RV_DEL1 == RV_DAP1B + RV_DAP1B_LEN + 1 &&
              RV_DAP2A == RV_DEL1 + RV_DEL1_LEN + 1 && RV_DAP2B == RV_DAP2A + RV_DAP2A_LEN + 1 &&
              RV_DEL2 == RV_DAP2B + RV_DAP2B_LEN + 1, "reverb bases");
static_assert(RV_MAX_REACH <= RV_SIZE, "reverb memory full");
static_assert(RV_SMEAR_OFFSET == Scaled(10) && RV_SMEAR_DEPTH == Scaled(60) &&
              RV_SMEAR_WRITE == Scaled(100) && RV_DEL2_OFFSET == Scaled(4680) &&
              RV_DEL2_DEPTH == Scaled(100), "reverb taps");
static_assert(RV_SMEAR_OFFSET + RV_SMEAR_DEPTH + 1 < RV_AP1_LEN && RV_SMEAR_WRITE < RV_AP1_LEN, "ap1 smear");
static_assert(RV_DEL2_OFFSET + RV_DEL2_DEPTH + 1 < RV_DEL2_LEN, "del2 tap");
// Immediate offsets in the assembly (W[P + uimm16m2]).
static_assert(2 * (RV_MAX_REACH - 1) <= 65534, "reverb offsets");

// The LFOs: upstream's approximate cosine oscillators, stepped every 32
// samples, run at sqrt(32) / (2 pi) of their nominal 0.5 and 0.3 Hz (as in
// Rings' fixed reverb, rings/src/reverb_fixed.cc).
constexpr uint32_t LfoIncrement(double hz) {
  return static_cast<uint32_t>(0.9003163161571061 * hz * 32.0 / OP1CLOUDS_SAMPLE_RATE * 4294967296.0);
}
const uint32_t kLfoIncrement[2] = { LfoIncrement(0.5), LfoIncrement(0.3) };

// 0.5 + 0.5 cos(2 pi phase), Q31.
inline int32_t Lfo(uint32_t phase) {
  const uint32_t p = phase + 0x40000000u;
  const uint32_t i = p >> 20;
  const int32_t frac = static_cast<int32_t>((p & 0xfffff) << 11);
  const int32_t s = op1rings::kSineQ31[i] + MulQ31Fast(op1rings::kSineQ31[i + 1] - op1rings::kSineQ31[i], frac);
  return (1 << 30) + (s >> 1);
}

// A modulated tap: offset + depth lfo samples (Q16), as bytes from p and
// the two interpolation weights (Q15).
inline void Tap(int32_t base, int32_t offset, int32_t depth, int32_t lfo, int32_t* off_bytes,
                int32_t* weights) {
  const int32_t o = (offset << 16) + MulQ31(lfo, depth << 16);
  const int32_t f = (o & 0xffff) >> 1;
  *off_bytes = 2 * (base + (o >> 16));
  *weights = (32767 - f) | (f << 16);
}

// The Blackfin's 16-bit MAC arithmetic (grain_kernel.h has the same).
inline int64_t Fr(int32_t a, int32_t b) {
  if (a == -32768 && b == -32768) return 0x7fffffff;
  return static_cast<int64_t>(a * b) * 2;
}
inline int32_t Sat16(int64_t x) {
  return static_cast<int32_t>(x > 32767 ? 32767 : (x < -32768 ? -32768 : x));
}
inline int32_t ExtractT(int64_t acc) { return Sat16(acc >> 16); }
inline int32_t Sat32(int64_t x) {
  return static_cast<int32_t>(x > INT32_MAX ? INT32_MAX : (x < INT32_MIN ? INT32_MIN : x));
}
inline int32_t Lo(int32_t x) { return static_cast<int16_t>(x & 0xffff); }
inline int32_t Hi(int32_t x) { return static_cast<int16_t>(static_cast<uint32_t>(x) >> 16); }
inline int32_t Wrap32(int64_t x) { return static_cast<int32_t>(static_cast<uint32_t>(x)); }
// floor(d a / 2^15) for a >= 0: the low half unsigned, then the high half.
inline int32_t Mul32x16(int32_t d, int32_t a) {
  const int64_t low = (static_cast<int64_t>(d & 0xffff) * a) >> 15;
  return Sat32(low + Fr(d >> 16, a));
}

}  // namespace

void ReverbRunC(ReverbKernel* k, int32_t* l, int32_t* r, int16_t* p, int32_t n,
                int32_t alias_bytes) {
  const int32_t alias = alias_bytes / 2;
  const int32_t gain = Lo(k->gain_amount), amount = Hi(k->gain_amount);
  const int32_t kap = Lo(k->kap), minus_kap = Hi(k->kap);
  const int32_t krt = Lo(k->krt_klp), klp = Hi(k->krt_klp);
  int32_t lp_1 = Lo(k->lp), lp_2 = Hi(k->lp);
  const int32_t smear = k->smear_off / 2, d2 = k->d2_off / 2;
  const int32_t smear_wl = Lo(k->smear_w), smear_wh = Hi(k->smear_w);
  const int32_t d2_wl = Lo(k->d2_w), d2_wh = Hi(k->d2_w);
#define WRITE(index, value) { p[index] = static_cast<int16_t>(value); p[(index) + alias] = static_cast<int16_t>(value); }
#define ALLPASS(base, length, rs, ws)                         \
  {                                                           \
    const int32_t previous = p[(base) + (length) - 1];        \
    acc += Fr(previous, rs);                                  \
    const int32_t w = ExtractT(acc);                          \
    WRITE(base, w)                                            \
    acc = (static_cast<int64_t>(previous) << 16) + Fr(w, ws); \
  }
  for (int32_t i = 0; i < n; ++i) {
    --p;
    // Smear AP1 by LFO 1: Interpolate(ap1, 10 + 60 lfo_1); Write(ap1, 100).
    const int32_t x = ExtractT(Fr(p[smear], smear_wl) + Fr(p[smear + 1], smear_wh));
    WRITE(RV_AP1 + RV_SMEAR_WRITE, x)
    // The input, (l + r) as Q15, x gain.
    const int32_t in = Sat32(static_cast<int64_t>(Wrap32(static_cast<int64_t>(l[i]) + r[i])) << 4) >> 16;
    int64_t acc = Fr(in, gain);
    ALLPASS(RV_AP1, RV_AP1_LEN, kap, minus_kap)
    ALLPASS(RV_AP2, RV_AP2_LEN, kap, minus_kap)
    ALLPASS(RV_AP3, RV_AP3_LEN, kap, minus_kap)
    ALLPASS(RV_AP4, RV_AP4_LEN, kap, minus_kap)
    const int64_t apout = acc;

    // First branch: del2's modulated tap, lp_1, dap1a, dap1b, del1.
    const int32_t t2 = ExtractT(Fr(p[d2], d2_wl) + Fr(p[d2 + 1], d2_wh));
    acc = apout + Fr(t2, krt);
    lp_1 = Sat16(lp_1 + ExtractT(Fr(Sat16(ExtractT(acc) - lp_1), klp)));
    acc = static_cast<int64_t>(lp_1) << 16;
    ALLPASS(RV_DAP1A, RV_DAP1A_LEN, minus_kap, kap)
    ALLPASS(RV_DAP1B, RV_DAP1B_LEN, kap, minus_kap)
    int32_t w = ExtractT(acc);
    WRITE(RV_DEL1, w)
    l[i] = Wrap32(static_cast<int64_t>(l[i]) + Mul32x16(Wrap32(static_cast<int64_t>(w) * 8192 - l[i]), amount));

    // Second branch: del1's tail, lp_2, dap2a, dap2b, del2.
    acc = apout + Fr(p[RV_DEL1 + RV_DEL1_LEN - 1], krt);
    lp_2 = Sat16(lp_2 + ExtractT(Fr(Sat16(ExtractT(acc) - lp_2), klp)));
    acc = static_cast<int64_t>(lp_2) << 16;
    ALLPASS(RV_DAP2A, RV_DAP2A_LEN, kap, minus_kap)
    ALLPASS(RV_DAP2B, RV_DAP2B_LEN, minus_kap, kap)
    w = ExtractT(acc);
    WRITE(RV_DEL2, w)
    r[i] = Wrap32(static_cast<int64_t>(r[i]) + Mul32x16(Wrap32(static_cast<int64_t>(w) * 8192 - r[i]), amount));
  }
#undef ALLPASS
#undef WRITE
  k->lp = (lp_1 & 0xffff) | (lp_2 << 16);
}

void ReverbStereo::Init() {
  amount_ = 0;
  input_gain_ = Q(0.2, 31);
  time_ = Q(0.5, 31);
  diffusion_ = Q(0.625, 31);
  lp_ = Q(0.7, 31);
#if defined(__bfin__) && !defined(OP1CLOUDS_NO_ASM)
  use_asm_ = true;
#else
  use_asm_ = false;
#endif
  Clear();
}

void ReverbStereo::Clear() {
  for (int32_t i = 0; i < RV_BUFFER_WORDS; ++i) buffer_[i] = 0;
  lp_state_ = 0;
  write_ptr_ = kSize;
  // Upstream's oscillators start at their peak (value() = 1 before the
  // first step).
  lfo_phase_[0] = lfo_phase_[1] = 0;
  lfo_value_[0] = lfo_value_[1] = INT32_MAX;
}

void ReverbStereo::Process(int32_t* l, int32_t* r, size_t size) {
  int32_t* const mono = r ? nullptr : l;
  if (!r) {
    for (size_t i = 0; i < size; ++i) scratch_[i] = l[i];
    r = scratch_;
  }
  ReverbKernel k;
  k.gain_amount = (input_gain_ >> 16) | ((amount_ >> 16) << 16);
  const int32_t kap = diffusion_ >> 16;
  k.kap = (kap & 0xffff) | (-kap << 16);
  k.krt_klp = (time_ >> 16) | ((lp_ >> 16) << 16);
  k.lp = lp_state_;
  int32_t n = static_cast<int32_t>(size);
  int32_t done = 0;
  while (done < n) {
    // The first sample's write pointer; the LFOs step there if it's a
    // multiple of 32, and the run lasts until the next one.
    const int32_t next = write_ptr_ - 1;
    if ((next & 31) == 0) {
      lfo_value_[0] = Lfo(lfo_phase_[0]);
      lfo_value_[1] = Lfo(lfo_phase_[1]);
      lfo_phase_[0] += kLfoIncrement[0];
      lfo_phase_[1] += kLfoIncrement[1];
    }
    int32_t m = (next & 31) ? (next & 31) : 32;
    if (m > next + 1) m = next + 1;
    const bool alias = next < RV_MAX_REACH;
    if (!alias && m > next - RV_MAX_REACH + 1) m = next - RV_MAX_REACH + 1;
    if (m > n - done) m = n - done;
    Tap(RV_AP1, RV_SMEAR_OFFSET, RV_SMEAR_DEPTH, lfo_value_[0], &k.smear_off, &k.smear_w);
    Tap(RV_DEL2, RV_DEL2_OFFSET, RV_DEL2_DEPTH, lfo_value_[1], &k.d2_off, &k.d2_w);
    const int32_t alias_bytes = alias ? 2 * kSize : 0;
#if defined(__bfin__)
    if (use_asm_) {
      ReverbRunAsm(&k, l + done, r + done, buffer_ + write_ptr_, m, alias_bytes);
    } else {
      ReverbRunC(&k, l + done, r + done, buffer_ + write_ptr_, m, alias_bytes);
    }
#else
    ReverbRunC(&k, l + done, r + done, buffer_ + write_ptr_, m, alias_bytes);
#endif
    write_ptr_ -= m;
    if (write_ptr_ == 0) write_ptr_ = kSize;
    done += m;
  }
  lp_state_ = k.lp;
  if (mono) {
    for (int32_t i = 0; i < n; ++i) mono[i] = (mono[i] >> 1) + (r[i] >> 1);
  }
}

}  // namespace op1clouds
