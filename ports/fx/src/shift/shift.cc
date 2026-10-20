// SHIFT: control and the C reference kernels (see shift.h).

#include "src/shift/shift.h"

#include <cstring>

namespace op1fx {

namespace {

// 2^16 / n, n = 0..64 (0 unused).
struct RecipTable {
  int32_t v[SH_MAX_BLOCK + 1];
  constexpr RecipTable() : v() {
    for (int32_t i = 1; i <= SH_MAX_BLOCK; ++i) v[i] = 65536 / i;
  }
};
constexpr RecipTable kRecip;

// 2000 Hz in phase steps: 2000 x 2^32 / 44100, rounded.
const int64_t kInc2kHz = 194783097;
// The feedback knob's top: 0.93 in Q15.
const int32_t kFeedbackMax = 30474;
// Q15 change of the mode gain m per sample: 256 samples end to end.
const int32_t kModeStep = 128;

inline int32_t Sat32(int64_t x) {
  return x > 2147483647LL ? 2147483647 : (x < -2147483648LL ? -2147483647 - 1 : static_cast<int32_t>(x));
}
inline int32_t Sat16(int32_t x) { return x > 32767 ? 32767 : (x < -32768 ? -32768 : x); }
// (min/max: the Blackfin's MIN and MAX instructions, no branches)
inline int32_t Min(int32_t a, int32_t b) { return a < b ? a : b; }
inline int32_t Max(int32_t a, int32_t b) { return a > b ? a : b; }
inline int32_t Clamp(int32_t x, int32_t lo, int32_t hi) { return Max(lo, Min(x, hi)); }
inline int32_t Lo(uint32_t w) { return static_cast<int16_t>(w & 0xffff); }
inline int32_t Hi(uint32_t w) { return static_cast<int16_t>(w >> 16); }
inline uint32_t Pack(int32_t lo, int32_t hi) {
  return (static_cast<uint32_t>(hi) << 16) | (static_cast<uint32_t>(lo) & 0xffff);
}

// 32-bit Hilbert data (int16 x 2^10) -> int16: x 2^6 saturated, then the
// high half rounded (half up), saturated.
inline int32_t Round16(int32_t x) {
  const int64_t v = static_cast<int64_t>(Sat32(static_cast<int64_t>(x) << 6)) + 0x8000;
  return Sat16(static_cast<int32_t>(v >> 16));
}

// One allpass in z^-2, in place: y = sat(a sat(x + y[n-2]) >> 15 - x[n-2]).
void Section(int32_t* buf, int32_t n, int32_t a, int32_t* st) {
  int32_t x1 = st[0], x2 = st[1], y1 = st[2], y2 = st[3];
  for (int32_t i = 0; i < n; ++i) {
    const int32_t x = buf[i];
    const int32_t t = Sat32(static_cast<int64_t>(x) + y2);
    const int32_t p = static_cast<int32_t>((static_cast<int64_t>(a) * t) >> 15);
    const int32_t y = Sat32(static_cast<int64_t>(p) - x2);
    buf[i] = y;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
  }
  st[0] = x1;
  st[1] = x2;
  st[2] = y1;
  st[3] = y2;
}

// x / 2^15 rounded to nearest, ties to even (the MAC's default, unbiased
// rounding of its high half), saturated to 16 bits.
inline int32_t Rnd15(int32_t x) {
  int32_t q = x >> 15;
  const int32_t r = x & 0x7fff;
  if (r > 0x4000 || (r == 0x4000 && (q & 1))) ++q;
  return Sat16(q);
}

// g x / 2^15 rounded towards zero (g >= 0).
inline int32_t Scale0(int32_t g, int32_t x) { return (g * x + ((x >> 15) & 32767)) >> 15; }

// The oscillator at a phase: sin (low half) and cos (high half), from
// kShiftOsc with linear interpolation (step x frac, floor).
inline void SinCos(const uint32_t* tab, uint32_t phase, int32_t* s, int32_t* c) {
  const uint32_t* e = tab + (phase >> 23);
  const int32_t frac = static_cast<int32_t>((phase >> 8) & 0x7fff);
  *s = Lo(e[0]) + ((Lo(e[512]) * frac) >> 15);
  *c = Hi(e[0]) + ((Hi(e[512]) * frac) >> 15);
}

// The per-sample step that takes value to target in n samples.
inline int32_t Step(int32_t value, int32_t target, int32_t n) {
  // Rounded towards zero, so n steps never pass the target. (Every delta
  // here fits 32 bits: phase steps differ by < 2^29, the rest by < 2^16.)
  if (value == target) return 0;
  if (n == SH_MAX_BLOCK) {
    const int32_t delta = target - value;
    return delta < 0 ? -((-delta) >> 6) : delta >> 6;
  }
  const int64_t delta = static_cast<int64_t>(target) - value;
  const int64_t step = ((delta < 0 ? -delta : delta) * kRecip.v[n]) >> 16;
  return static_cast<int32_t>(delta < 0 ? -step : step);
}

}  // namespace

void ShiftHilbertC(ShiftHilbertKernel* k, const int16_t* l, const int16_t* r, uint32_t* iq,
                   int32_t* buf, int32_t n) {
  int32_t* bi = buf;
  int32_t* bq = buf + SH_MAX_BLOCK;
  for (int32_t i = 0; i < n; ++i) {
    bi[i] = r ? (static_cast<int32_t>(l[i]) + r[i]) * 512 : static_cast<int32_t>(l[i]) * 1024;
    bq[i] = i ? bi[i - 1] : k->xh[0];
  }
  const int32_t xn[3] = {bi[n - 1], bi[n - 2], n > 2 ? bi[n - 3] : k->xh[0]};
  // Each path's sections in turn; a section's input history is the
  // previous section's output history (the path's input's, for the first).
  for (int32_t path = 0; path < 2; ++path) {
    int32_t x1 = k->xh[path], x2 = k->xh[path + 1];
    for (int32_t s = 0; s < 4; ++s) {
      const int32_t j = 2 * s + path;
      int32_t st[4] = {x1, x2, k->y[j][0], k->y[j][1]};
      x1 = k->y[j][0];
      x2 = k->y[j][1];
      Section(path ? bq : bi, n, k->coef[j], st);
      k->y[j][0] = st[2];
      k->y[j][1] = st[3];
    }
  }
  k->xh[0] = xn[0];
  k->xh[1] = xn[1];
  k->xh[2] = xn[2];
  int32_t m = k->m;
  const bool unity = m == 32768 && k->dm == 0;
  for (int32_t i = 0; i < n; ++i) {
    const int32_t vi = Round16(bi[i]);
    const int32_t vq = Round16(bq[i]);
    const int32_t mq = unity ? vq : (vq * (m < 32767 ? m : 32767)) >> 15;
    m += k->dm;
    iq[i] = Pack(vi, mq);
  }
  k->m = m;
}

void ShiftChannelC(ShiftChannelKernel* k, const uint32_t* iq, int16_t* io, int32_t n,
                   int32_t write, uint32_t* scratch) {
  (void)scratch;   // the assembly's: the oscillator, then the wet signal
  uint32_t phase = k->phase;
  int32_t inc = k->inc, g = k->g, mq = k->mix, mq0 = 32767 - k->mix;
  int32_t lr = Lo(k->lp), li = Hi(k->lp);
  int32_t pos = k->pos;
  for (int32_t i = 0; i < n; ++i) {
    int32_t s, c;
    SinCos(k->osc, phase, &s, &c);
    phase += static_cast<uint32_t>(inc);
    inc += k->dinc;

    const uint32_t fb = k->delay[pos >> 2];
    // Feedback: g fb rounded towards zero (ceil for fb < 0), so the
    // loop's quantisation can't sustain itself: silence in, silence out.
    const int32_t ai = Sat16(Lo(iq[i]) + Scale0(g, Lo(fb)));
    const int32_t aq = Sat16(Hi(iq[i]) + Scale0(g, Hi(fb)));
    g += k->dg;
    // Rotation and loop low-pass: rounded as the MAC does (ties to even).
    const int32_t wr = Rnd15(ai * c - aq * s);
    const int32_t wi = Rnd15(ai * s + aq * c);
    lr = Rnd15(wr * SH_LP_K + lr * SH_LP_K2);
    li = Rnd15(wi * SH_LP_K + li * SH_LP_K2);
    k->delay[pos >> 2] = Pack(lr, li);
    pos = (pos + 4) & (4 * SH_DELAY - 1);

    // Mix: weights 32767 - mix and mix (Q15), floor.
    if (write) io[i] = static_cast<int16_t>((io[i] * mq0 + wr * mq) >> 15);
    mq += k->dmix;
    mq0 -= k->dmix;
  }
  k->phase = phase;
  k->inc = inc;
  k->g = g;
  k->mix = mq;
  k->lp = Pack(lr, li);
  k->pos = pos;
}

int32_t ShiftIncrement(int32_t d) {
  const int64_t d3 = static_cast<int64_t>(d) * d * d;   // |d3| <= 2^45
  return static_cast<int32_t>(((d3 >> 15) * kInc2kHz) >> 30);
}

void FreqShift::Init() {
  memset(&hk_, 0, sizeof(hk_));
  for (int32_t j = 0; j < 8; ++j) hk_.coef[j] = kShiftHilbert[j];
  hk_.m = 32768;
  for (int32_t c = 0; c < 2; ++c) {
    memset(&ck_[c], 0, sizeof(ck_[c]));
    ck_[c].osc = kShiftOsc;
  }
  memset(buf_, 0, sizeof(buf_));
  memset(iq_, 0, sizeof(iq_));
  d_[0] = d_[1] = 0;
  inc_[0] = inc_[1] = 0;
#ifdef __bfin__
  use_asm_ = true;
#else
  use_asm_ = false;
#endif
}

void FreqShift::Process(int16_t* l, int16_t* r, int32_t n, const ShiftParams& p) {
  if (n <= 0) return;
  if (n > SH_MAX_BLOCK) n = SH_MAX_BLOCK;

  const int32_t d = ShiftKnobToD(Clamp(p.shift, 0, 32767));
  const int32_t s = ShiftSpreadToD(Clamp(p.spread, 0, 32767));
  // The phase steps (L: shift + spread, R: shift - spread), recomputed
  // only when their position changes.
  const int32_t dc[2] = {Clamp(d + s, -32768, 32767), Clamp(d - s, -32768, 32767)};
  for (int32_t c = 0; c < 2; ++c) {
    if (dc[c] != d_[c]) {
      d_[c] = dc[c];
      inc_[c] = ShiftIncrement(dc[c]);
    }
  }
  const int32_t* inc = inc_;
  const int32_t g = (Clamp(p.feedback, 0, 32767) * kFeedbackMax) >> 15;
  const int32_t mix = Clamp(p.mix, 0, 32767);

  // The mode gain: towards 0 (ring) or 32768 (shift), 128 a sample at most.
  const int32_t m_target = p.ring ? 0 : 32768;
  const int32_t m_max = kModeStep * n;
  const int32_t m_end = hk_.m + Clamp(m_target - hk_.m, -m_max, m_max);
  hk_.dm = Step(hk_.m, m_end, n);
#ifdef __bfin__
  if (use_asm_) ShiftHilbertAsm(&hk_, l, r, iq_, buf_, n);
  else
#endif
  ShiftHilbertC(&hk_, l, r, iq_, buf_, n);
  hk_.m = m_end;

  const int32_t channels = r ? 2 : 1;
  for (int32_t c = 0; c < channels; ++c) {
    ShiftChannelKernel* k = &ck_[c];
    k->dinc = Step(k->inc, inc[c], n);
    k->dg = Step(k->g, g, n);
    k->dmix = Step(k->mix, mix, n);
    const int32_t write = !(k->mix == 0 && mix == 0);
    int16_t* io = c ? r : l;
#ifdef __bfin__
    if (use_asm_) ShiftChannelAsm(k, iq_, io, n, write, reinterpret_cast<uint32_t*>(buf_));
    else
#endif
    ShiftChannelC(k, iq_, io, n, write, reinterpret_cast<uint32_t*>(buf_));
    k->inc = inc[c];
    k->g = g;
    k->mix = mix;
  }
}

}  // namespace op1fx
