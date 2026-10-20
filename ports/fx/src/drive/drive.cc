// DRIVE: an oversampled soft-clip overdrive in fixed point. See drive.h and
// docs/drive-port.md.

#include "src/drive/drive.h"

namespace op1fx {

namespace {

// --- Compile-time maths (constexpr: no run-time float, no initialisers) ---

constexpr double Exp(double x) {
  int halvings = 0;
  while (x > 0.25 || x < -0.25) {
    x *= 0.5;
    ++halvings;
  }
  double term = 1.0, sum = 1.0;
  for (int i = 1; i < 16; ++i) {
    term *= x / i;
    sum += term;
  }
  for (int i = 0; i < halvings; ++i) sum *= sum;
  return sum;
}

constexpr double Sqrt(double x) {
  if (x <= 0) return 0;
  double r = x > 1 ? x : 1;
  for (int i = 0; i < 60; ++i) r = 0.5 * (r + x / r);
  return r;
}

// sin(x) for |x| <= pi (Taylor; the 20th term is under 1e-15).
constexpr double Sin(double x) {
  double term = x, sum = x;
  for (int i = 1; i < 20; ++i) {
    term *= -x * x / ((2 * i) * (2 * i + 1));
    sum += term;
  }
  return sum;
}

constexpr int32_t Round(double x) {
  return static_cast<int32_t>(x < 0 ? x - 0.5 : x + 0.5);
}

constexpr double kRate = 44100.0;
constexpr double kPi = 3.14159265358979323846;
constexpr double kLn10 = 2.30258509299404568;
constexpr double kLn2 = 0.69314718055994531;
constexpr double kDriveDb = 36.0;
constexpr double kBiasMax = 0.5;
constexpr double kRefLevel = 0.25;          // the auto-level's reference: a -12 dBFS sine
constexpr double kTonePivot = 1000.0;       // Hz
constexpr double kDcCorner = 10.0;          // Hz

// The clipper's gain K (u = K x', x' = x / 2): 2 g / 1.875, g = 10^(dB / 20).
constexpr double GainK(double knob) {         // knob 0..1
  return 2.0 * Exp(knob * kDriveDb / 20.0 * kLn10) / 1.875;
}

// The soft clip at half scale (as the fixed-point one), u clamped.
constexpr double ShapeD(double u) {
  if (u > 1) u = 1;
  if (u < -1) u = -1;
  const double t = u * u;
  return u * (15.0 - 10.0 * t + 3.0 * t * t) / 16.0;
}

struct Table33 {   // 33 points, and the last again (the lookup reads i + 1 <= 33)
  int32_t v[34];
};
struct Table16 {   // the same, int16 (16 x 16 products in the lookup)
  int16_t v[34];
};

// K, Q16, over the drive knob.
constexpr Table33 MakeKTable() {
  Table33 t = {};
  for (int i = 0; i <= 32; ++i) t.v[i] = Round(65536.0 * GainK(i / 32.0));
  t.v[33] = t.v[32];
  return t;
}
constexpr Table33 kKTable = MakeKTable();

// 1 / K, Q15.
constexpr Table16 MakeInvKTable() {
  Table16 t = {};
  for (int i = 0; i <= 32; ++i) t.v[i] = static_cast<int16_t>(Round(32768.0 / GainK(i / 32.0)));
  t.v[33] = t.v[32];
  return t;
}
constexpr Table16 kInvKTable = MakeInvKTable();

// The tilt's gains: 2^(-1 .. 1) over 0..32768, Q13 (8192 at the middle).
constexpr Table16 MakeTiltTable() {
  Table16 t = {};
  for (int i = 0; i <= 32; ++i) {
    t.v[i] = static_cast<int16_t>(Round(8192.0 * Exp((i - 16) / 16.0 * kLn2)));
  }
  t.v[33] = t.v[32];
  return t;
}
constexpr Table16 kTiltTable = MakeTiltTable();

// The gain's exponent e for K < 2^e, by the integer part of K (1..67).
struct ExpTable {
  int8_t v[128];
};
constexpr ExpTable MakeExpTable() {
  ExpTable t = {};
  for (int k = 0; k < 128; ++k) {
    int e = 1;
    while (k >= (1 << e)) ++e;
    t.v[k] = static_cast<int8_t>(e);
  }
  return t;
}
constexpr ExpTable kExp = MakeExpTable();

// The auto-level gain (Q13) over drive (17 points) x bias (9 points): the
// reference sine's RMS in over the wet's RMS out (DC removed), full scale.
const int32_t kMkDrive = 17, kMkBias = 9;
struct MakeupTable {
  int16_t v[kMkDrive][kMkBias];
};
constexpr MakeupTable MakeMakeup() {
  MakeupTable t = {};
  const int kN = 256;
  double s[kN] = {};
  for (int k = 0; k < kN; ++k) {
    const double ph = 2.0 * kPi * k / kN;
    s[k] = ph <= kPi ? Sin(ph) : -Sin(2.0 * kPi - ph);
  }
  for (int i = 0; i < kMkDrive; ++i) {
    const double g = GainK(i / 16.0);
    for (int j = 0; j < kMkBias; ++j) {
      const double b = kBiasMax * j / 8.0;
      double sum = 0, sum2 = 0;
      for (int k = 0; k < kN; ++k) {
        const double y = 2.0 * ShapeD(g * kRefLevel * 0.5 * s[k] + b);
        sum += y;
        sum2 += y * y;
      }
      const double mean = sum / kN;
      const double rms = Sqrt(sum2 / kN - mean * mean);
      double m = kRefLevel / Sqrt(2.0) / rms;
      int32_t q = Round(8192.0 * m);
      if (q > 32767) q = 32767;
      t.v[i][j] = static_cast<int16_t>(q);
    }
  }
  return t;
}
constexpr MakeupTable kMakeup = MakeMakeup();

// The tilt's one-pole low-pass, k = 1 - exp(-2 pi fc / fs), Q15.
constexpr int32_t kToneK = Round(32768.0 * (1.0 - Exp(-2.0 * kPi * kTonePivot / kRate)));
// The DC blocker's (2 pi fc / fs, Q15).
constexpr int32_t kDcK = Round(32768.0 * 2.0 * kPi * kDcCorner / kRate);
static_assert(kToneK == 4351, "drive_bfin.S has the tone coefficient");
static_assert(kDcK == 47, "drive_bfin.S has the DC coefficient");

// The soft clip's constants: y = u (A - t (B - C t)), t = u^2 (Q15).
const int32_t kShA = 30720;   // 15/16
const int32_t kShB = 20480;   // 10/16
const int32_t kShC = 6144;    //  3/16

// Clouds' halfband (19 taps): h[+-1], h[+-3] .. h[+-9], Q15; the centre 0.5.
const int16_t kHb[5] = { 10019, -2397, 710, -151, 11 };

// --- The Blackfin's 16-bit arithmetic -----------------------------------

constexpr int64_t Fr(int32_t a, int32_t b) {
  return (a == -32768 && b == -32768) ? 0x7fffffff : static_cast<int64_t>(a * b) * 2;
}
constexpr int32_t Sat16(int64_t x) {
  return static_cast<int32_t>(x > 32767 ? 32767 : (x < -32768 ? -32768 : x));
}
// The accumulator's high half, rounded as the MAC does with RND_MOD clear
// (unbiased: ties to even), saturated.
constexpr int32_t Rnd(int64_t acc) {
  int64_t q = acc >> 16;
  const int32_t r = static_cast<int32_t>(acc & 0xffff);
  if (r > 0x8000 || (r == 0x8000 && (q & 1))) ++q;
  return Sat16(q);
}
constexpr int32_t Lo(int32_t x) { return static_cast<int16_t>(x & 0xffff); }
constexpr int32_t Hi(int32_t x) { return static_cast<int16_t>(static_cast<uint32_t>(x) >> 16); }
constexpr int32_t Half(int32_t x, int c) { return c ? Hi(x) : Lo(x); }
constexpr int32_t Pack(int32_t lo, int32_t hi) {
  return (lo & 0xffff) | static_cast<int32_t>(static_cast<uint32_t>(hi) << 16);
}
// Vector add, each half wrapping (+|+).
constexpr int32_t AddV(int32_t a, int32_t b) { return Pack(Lo(a) + Lo(b), Hi(a) + Hi(b)); }

// The soft clip, as pass 2 computes it.
constexpr int32_t ShapeOne(int32_t u) {
  const int32_t t = Rnd(Fr(u, u));
  const int32_t s1 = Sat16(kShB - Rnd(Fr(t, kShC)));
  const int32_t s2 = Sat16(kShA - Rnd(Fr(t, s1)));
  return Rnd(Fr(u, s2));
}

// f(b), the bias's static offset at the shaper's output, over the bias
// knob (b = knob / 2).
constexpr Table16 MakeFbTable() {
  Table16 t = {};
  for (int i = 0; i <= 32; ++i) t.v[i] = static_cast<int16_t>(ShapeOne((i * 1024) >> 1));
  t.v[33] = t.v[32];
  return t;
}
constexpr Table16 kFbTable = MakeFbTable();

// --- Control helpers --------------------------------------------------------

inline int32_t Clamp15(int32_t x) {   // 0..32767
  x = x < 0 ? 0 : x;
  return x > 32767 ? 32767 : x;
}

// A 16 x 16 product (the MAC's integer multiply, not a 32-bit one).
inline int32_t Mul16(int32_t a, int32_t b) {
  return static_cast<int32_t>(static_cast<int16_t>(a)) * static_cast<int16_t>(b);
}

// x 0..32768.
inline int32_t Lookup(const Table33& t, int32_t x) {
  const int32_t* v = t.v + (x >> 10);
  const int32_t f = x & 1023;
  return v[0] + (((v[1] - v[0]) * f) >> 10);   // |diff| < 2^20
}
inline int32_t Lookup16(const Table16& t, int32_t x) {
  const int16_t* v = t.v + (x >> 10);
  return v[0] + (Mul16(v[1] - v[0], x & 1023) >> 10);   // the tables' steps fit 16 bits
}

// Bilinear in the auto-level table; drive, bias 0..32767.
inline int32_t Makeup(int32_t drive, int32_t bias) {
  const int16_t* p = &kMakeup.v[0][0] + (drive >> 11) * kMkBias + (bias >> 12);
  const int32_t fi = drive & 2047, fj = bias & 4095;
  const int32_t a = p[0] + (Mul16(p[1] - p[0], fj) >> 12);
  const int32_t b = p[kMkBias] + (Mul16(p[kMkBias + 1] - p[kMkBias], fj) >> 12);
  return a + (Mul16(b - a, fi) >> 11);
}

// 65536 / c and log2(c) (-1 if c isn't a power of two) for even c = 2 j,
// j = 0..64.
struct CountTable {
  int32_t recip[65];
  int8_t log2[65];
};
constexpr CountTable MakeCountTable() {
  CountTable t = {};
  for (int j = 0; j <= 64; ++j) {
    t.recip[j] = j ? Round(65536.0 / (2 * j)) : 0;
    t.log2[j] = -1;
    for (int s = 1; s <= 7; ++s) {
      if ((1 << s) == 2 * j) t.log2[j] = static_cast<int8_t>(s);
    }
  }
  return t;
}
constexpr CountTable kCount = MakeCountTable();

// A ramp's length: count steps, a shift when count is a power of two.
using Count = DriveCount;

// delta / count, rounded toward 0 (so the ramp never overshoots).
inline int32_t RampStep(int32_t delta, const Count& c) {
  if (c.shift >= 0) return (delta + ((delta >> 31) & (c.count - 1))) >> c.shift;
  // count <= 128, |delta| < 2^17: the product fits 32 bits.
  const int32_t a = delta < 0 ? -delta : delta;
  int32_t q = (a * kCount.recip[c.count >> 1]) >> 16;
  if (q * c.count > a) --q;
  return delta < 0 ? -q : q;
}

// Smoothing per call (Q15 << 8): an eighth of the way each call (~12 ms),
// snapped when close. target 0..32767.
inline int32_t Smooth(int32_t s, int32_t target) {
  const int32_t t = target << 8;
  const int32_t d = t - s;
  return static_cast<uint32_t>(d + 2047) < 4095u ? t : s + (d >> 3);
}

// The ramp from *cur to target; *cur is left at its end (snapped to the
// target when the step rounds to 0). Returns the step; start is *cur
// before the call.
inline int32_t Ramp(int32_t* cur, int32_t* start, int32_t target, const Count& c) {
  const int32_t step = RampStep(target - *cur, c);
  if (step == 0) *cur = target;
  *start = *cur;
  *cur += c.shift >= 0 ? step << c.shift : step * c.count;
  return step;
}

// Branch-free forms for the usual (power-of-two) block sizes: GCC turns
// selects into taken branches on the Blackfin, so these use masks.
// -1 if x != 0, else 0 (x > INT32_MIN).
inline int32_t NonZero(int32_t x) { return (x | -x) >> 31; }
// mask ? a : b, mask 0 or -1.
inline int32_t Select(int32_t mask, int32_t a, int32_t b) { return (a & mask) | (b & ~mask); }

inline int32_t SmoothFast(int32_t s, int32_t target) {
  const int32_t t = target << 8;
  const int32_t d = t - s;
  const int32_t far = ((d + 2047) | (2047 - d)) >> 31;   // -1: |d| > 2047
  return Select(far, s + (d >> 3), t);
}

// Ramp() over 2^shift steps.
inline int32_t RampFast(int32_t* cur, int32_t* start, int32_t target, int32_t shift) {
  const int32_t d = target - *cur;
  const int32_t step = (d + ((d >> 31) & ((1 << shift) - 1))) >> shift;
  const int32_t from = Select(NonZero(step), *cur, target);
  *start = from;
  *cur = from + (step << shift);
  return step;
}

}  // namespace

// A device build can define OP1FX_DRIVE_ASM_ONLY: the passes are then the
// assembly only, and the C references (and the tests' no-oversampling
// path) aren't compiled.
#if defined(__bfin__) && defined(OP1FX_DRIVE_ASM_ONLY)
#define DRIVE_C_REFS 0
#else
#define DRIVE_C_REFS 1
#endif

// --- Reference kernels ------------------------------------------------------

int32_t DriveShapeOne(int32_t u) { return ShapeOne(u); }

#if DRIVE_C_REFS

void DriveInterpC(DriveInterpKernel* k, const int16_t* in0, const int16_t* in1, int32_t* xh,
                  int32_t* ys, int32_t n) {
  int32_t beta = k->beta;
  for (int32_t m = 0; m < n; ++m) {
    xh[kDriveXHistory + m] = Pack(in0[m], in1[m]);
    const int32_t* w = xh + m;
    int32_t ev[2], od[2];
    for (int c = 0; c < 2; ++c) {
      int64_t acc = 0;
      for (int j = 0; j < 5; ++j) {
        acc += Fr(Half(w[j], c), kHb[4 - j]);
        acc += Fr(Half(w[9 - j], c), kHb[4 - j]);
      }
      ev[c] = Sat16(Rnd(acc) + Half(beta, c));
      od[c] = Sat16(Rnd(Fr(Half(w[5], c), 16384)) + Half(beta, c));
    }
    ys[2 * m] = Pack(ev[0], ev[1]);
    ys[2 * m + 1] = Pack(od[0], od[1]);
    beta = AddV(beta, k->dbeta);
  }
  k->beta = beta;
}

void DriveShapeC(DriveShapeKernel* k, int32_t* y, int32_t words) {
  int32_t g = k->gain;
  const int32_t e = k->shift;
  for (int32_t i = 0; i < words; ++i) {
    int32_t out[2];
    for (int c = 0; c < 2; ++c) {
      const int32_t v = Rnd(Fr(Half(y[i], c), Half(g, c)));
      const int32_t u = Sat16(static_cast<int64_t>(v) << e);
      out[c] = DriveShapeOne(u);
    }
    y[i] = Pack(out[0], out[1]);
    g = AddV(g, k->dgain);
  }
  k->gain = g;
}

void DriveDecimC(const int32_t* ys, int32_t* out, int32_t n) {
  for (int32_t k = 0; k < n; ++k) {
    const int32_t* w = ys + 2 * k;
    int32_t o[2];
    for (int c = 0; c < 2; ++c) {
      int64_t acc = Fr(Half(w[9], c), 16384);
      for (int i = 0; i < 5; ++i) {
        acc += Fr(Half(w[8 - 2 * i], c), kHb[i]);
        acc += Fr(Half(w[10 + 2 * i], c), kHb[i]);
      }
      o[c] = Rnd(acc);
    }
    out[k] = Pack(o[0], o[1]);
  }
}

void DriveToneC(DriveToneKernel* k, int32_t* yw, int32_t n) {
  int32_t fb = k->fb, lp = k->lp, tone = k->tone;
  for (int32_t i = 0; i < n; ++i) {
    int32_t nlp[2], o[2];
    for (int c = 0; c < 2; ++c) {
      const int32_t x = Sat16(Half(yw[i], c) - Half(fb, c));
      const int32_t l = Sat16(Half(lp, c) + Rnd(Fr(Sat16(x - Half(lp, c)), kToneK)));
      nlp[c] = l;
      // (S2RND): the accumulator x 2, then its high half.
      o[c] = Rnd(2 * (Fr(x, Lo(tone)) + Fr(l, Hi(tone))));
    }
    lp = Pack(nlp[0], nlp[1]);
    yw[i] = Pack(o[0], o[1]);
    fb = AddV(fb, k->dfb);
    tone = AddV(tone, k->dtone);
  }
  k->fb = fb;
  k->lp = lp;
  k->tone = tone;
}

void DriveDcC(DriveDcKernel* k, int32_t* w, int32_t n) {
  int64_t acc[2] = { k->acc0, k->acc1 };
  for (int32_t i = 0; i < n; ++i) {
    int32_t out[2];
    for (int c = 0; c < 2; ++c) {
      const int32_t x = Sat16(Half(w[i], c) - Rnd(acc[c]));
      acc[c] += Fr(x, kDcK);
      out[c] = x;
    }
    w[i] = Pack(out[0], out[1]);
  }
  k->acc0 = static_cast<int32_t>(acc[0]);
  k->acc1 = static_cast<int32_t>(acc[1]);
}

void DriveMixC(DriveMixKernel* k, const int32_t* w, const int16_t* dry0, const int16_t* dry1,
               int16_t* out0, int16_t* out1, int32_t n) {
  int32_t mix = k->mix;
  for (int32_t i = 0; i < n; ++i) {
    // (S2RND), weights Q14: mix 0 (16384, 0) gives the dry exactly.
    const int32_t a = Rnd(2 * (Fr(dry0[i], Lo(mix)) + Fr(Lo(w[i]), Hi(mix))));
    const int32_t b = Rnd(2 * (Fr(dry1[i], Lo(mix)) + Fr(Hi(w[i]), Hi(mix))));
    out0[i] = static_cast<int16_t>(a);
    out1[i] = static_cast<int16_t>(b);
    mix = AddV(mix, k->dmix);
  }
  k->mix = mix;
}

void DriveMonoWetC(const DriveMonoWetKernel* k, const int32_t* y, int16_t* io, int32_t n,
                   int32_t runs) {
  const int32_t per_run = n >> (runs - 1);
  DriveToneKernel* t = k->tone;
  DriveMixKernel* mk = k->mix;
  int32_t fb = t->fb, lp = Lo(t->lp), tone = t->tone, mix = mk->mix;
  int64_t acc = k->dc->acc0;
  for (int32_t i = 0; i < n; ++i) {
    const int32_t yi = i < per_run ? Lo(y[i]) : Hi(y[i - per_run]);
    const int32_t x = Sat16(yi - Lo(fb));
    lp = Sat16(lp + Rnd(Fr(Sat16(x - lp), kToneK)));
    const int32_t w = Rnd(2 * (Fr(x, Lo(tone)) + Fr(lp, Hi(tone))));
    const int32_t d = Sat16(w - Rnd(acc));
    acc += Fr(d, kDcK);
    io[i] = static_cast<int16_t>(Rnd(2 * (Fr(io[i], Lo(mix)) + Fr(d, Hi(mix)))));
    fb = AddV(fb, t->dfb);
    tone = AddV(tone, t->dtone);
    mix = AddV(mix, mk->dmix);
  }
  t->fb = fb;
  t->lp = Pack(lp, lp);
  t->tone = tone;
  k->dc->acc0 = k->dc->acc1 = static_cast<int32_t>(acc);
  mk->mix = mix;
}

#endif  // DRIVE_C_REFS

// --- The effect -------------------------------------------------------------

void Drive::Init() {
  drive_ = bias_ = mix_ = 0;
  tone_ = 16384 << 8;
  k_ = kKTable.v[0];
  beta_ = 0;
  fb_ = 0;
  hm_ = Makeup(0, 0) << 1;
  dm_ = 0;
  gd_ = 16384;
  gw_ = 0;
#ifdef __bfin__
  use_asm_ = true;
#else
  use_asm_ = false;
#endif
  oversample_ = true;
  settled_ = false;
  last_ = DriveParams();
  layout_ = 0;
  phase_ = 0;
  hold_b_ = false;
  fresh_ = 0;
  moving_a_ = moving_b_ = 1;
  ik_ = DriveInterpKernel();
  sk_ = DriveShapeKernel();
  tk_ = DriveToneKernel();
  dk_ = DriveDcKernel();
  mk_ = DriveMixKernel();
  for (int32_t i = 0; i < kDriveXHistory + kDriveMaxBlock; ++i) xh_[i] = 0;
  for (int32_t i = 0; i < kDriveYHistory + 2 * kDriveMaxBlock + 1; ++i) ys_[i] = 0;
  for (int32_t i = 0; i < kDriveMaxBlock + 1; ++i) yd_[i] = 0;
}

// A ramp's length: count steps; shift = log2(count) when kPow2 (the usual
// block sizes: shifts, and no branches, which GCC makes of selects on the
// Blackfin), else -1 and a reciprocal.
template <bool kPow2>
inline int32_t StepOf(int32_t d, const Count& c) {
  if (kPow2) return (d + ((d >> 31) & (c.count - 1))) >> c.shift;
  return RampStep(d, c);
}
template <bool kPow2>
inline int32_t Times(int32_t step, const Count& c) {
  return kPow2 ? step << c.shift : step * c.count;
}
template <bool kPow2>
inline int32_t RampOf(int32_t* cur, int32_t* start, int32_t target, const Count& c) {
  if (kPow2) return RampFast(cur, start, target, c.shift);
  return Ramp(cur, start, target, c);
}

// Group A: the gain (per 2x word: gc), the bias and f(b) (per sample: c).
// hw / hs: the second half's offset in the mono split (2x words /
// samples), else 0. Returns the steps, or'ed.
template <bool kPow2>
int32_t Drive::GroupA(const Count& c, const Count& gc, int32_t hw, int32_t hs) {
  const int32_t drive = drive_ >> 8, bias = bias_ >> 8;
  // Gain: K (Q16) = Gm (Q15) x 2^(e + 1), one exponent for the ramp, K < 2^e.
  const int32_t k_target = Lookup(kKTable, drive);
  const int32_t kint = (k_ > k_target ? k_ : k_target) >> 16;   // 1..67
  const int32_t e = kExp.v[kint];
  const int32_t sh = e + 1;
  const int32_t ge = k_target >> sh;
  const int32_t gstep = StepOf<kPow2>(ge - (k_ >> sh), gc);
  const int32_t gnz = NonZero(gstep);
  const int32_t gs = Select(gnz, k_ >> sh, ge);
  k_ = Select(gnz, (gs + Times<kPow2>(gstep, gc)) << sh, k_target);
  sk_.gain = Pack(gs, gs + Mul16(hw, gstep));
  sk_.dgain = Pack(gstep, gstep);
  sk_.shift = e;

  // Bias: b (Q15, 0..0.5) at the clipper; b / K in the interpolator.
  int32_t start;
  int32_t step = RampOf<kPow2>(&beta_, &start,
                               Mul16(bias >> 1, Lookup16(kInvKTable, drive)) >> 15, c);
  int32_t moving = gstep | step;
  ik_.beta = Pack(start, start + Mul16(hs, step));
  ik_.dbeta = Pack(step, step);
  step = RampOf<kPow2>(&fb_, &start, Lookup16(kFbTable, bias), c);
  tk_.fb = Pack(start, start);
  tk_.dfb = Pack(step, step);
  return moving | step;
}

// Group B: the level and the tilt (Hm = m hi, Dm = m (lo - hi), Q14; out =
// Hm x + Dm lp), the mix (dry weight Q14, the wet's Q14 x 2: the wet is
// half scale; mix 32767 is all wet). Per sample.
template <bool kPow2>
int32_t Drive::GroupB(const Count& c) {
  const int32_t drive = drive_ >> 8, tone = tone_ >> 8, bias = bias_ >> 8, mix = mix_ >> 8;
  const int32_t m = Makeup(drive, bias);                     // Q13
  const int32_t hi = Lookup16(kTiltTable, tone);             // Q13
  const int32_t lo = Lookup16(kTiltTable, 32768 - tone);
  // The shelves' gains, each clamped to the kernel's 2.0 (only at drive ~0
  // with the bias up and the tilt far over), so the shape stays a tilt.
  int32_t hg = Mul16(m, hi) >> 12, lg = Mul16(m, lo) >> 12;
  hg = hg > 32767 ? 32767 : hg;
  lg = lg > 32767 ? 32767 : lg;
  const int32_t hm = hg, dm = lg - hg;
  int32_t start, start2;
  int32_t step = RampOf<kPow2>(&hm_, &start, hm, c);
  int32_t step2 = RampOf<kPow2>(&dm_, &start2, dm, c);
  int32_t moving = step | step2;
  tk_.tone = Pack(start, start2);
  tk_.dtone = Pack(step, step2);
  const int32_t mq = mix - ((32766 - mix) >> 31);         // 32767 -> 32768
  step = RampOf<kPow2>(&gd_, &start, (32768 - mq) >> 1, c);
  step2 = RampOf<kPow2>(&gw_, &start2, mix, c);
  mk_.mix = Pack(start, start2);
  mk_.dmix = Pack(step, step2);
  return moving | step | step2;
}

// Once per call. The knobs are smoothed every call; the coefficients'
// targets are recomputed in two groups on alternate calls, each ramping
// over two calls (so each call pays for half). When the block size or the
// layout changes, both groups ramp within the call, and group B holds for
// the next one so the alternation restarts cleanly.
void Drive::Control(const DriveParams& p, int32_t n, bool split) {
  const int32_t pd = Clamp15(p.drive), pt = Clamp15(p.tone), pb = Clamp15(p.bias),
                pm = Clamp15(p.mix);
  if (settled_ && pd == last_.drive && pt == last_.tone && pb == last_.bias && pm == last_.mix) {
    return;   // every step is 0: the kernel blocks hold
  }
  last_.drive = pd;
  last_.tone = pt;
  last_.bias = pb;
  last_.mix = pm;
  const int32_t d0 = drive_, t0 = tone_, b0 = bias_, m0 = mix_;
  drive_ = SmoothFast(drive_, pd);
  tone_ = SmoothFast(tone_, pt);
  bias_ = SmoothFast(bias_, pb);
  mix_ = SmoothFast(mix_, pm);
  const bool moved = ((d0 ^ drive_) | (t0 ^ tone_) | (b0 ^ bias_) | (m0 ^ mix_)) != 0;
  const bool at_target = ((drive_ ^ (pd << 8)) | (tone_ ^ (pt << 8)) | (bias_ ^ (pb << 8)) |
                          (mix_ ^ (pm << 8))) == 0;
  if (moved) fresh_ = 0;   // both groups must see the new values

  const int32_t shift = kCount.log2[n >> 1];            // log2(n), or -1
  const int32_t os = oversample_ ? 1 : 0;
  const int32_t words = n << os;                         // 2x words per call
  const int32_t hw = split ? words >> 1 : 0, hs = split ? n >> 1 : 0;
  const int32_t layout = n | (split ? 0x100 : 0) | (os << 9);
  if (shift < 0 || layout != layout_) {
    const Count c = { n, shift }, gc = { words, shift < 0 ? -1 : shift + os };
    if (shift < 0) {
      moving_a_ = GroupA<false>(c, gc, hw, hs);
      moving_b_ = GroupB<false>(c);
    } else {
      moving_a_ = GroupA<true>(c, gc, hw, hs);
      moving_b_ = GroupB<true>(c);
    }
    layout_ = shift < 0 ? 0 : layout;
    phase_ = 0;
    hold_b_ = true;
    fresh_ = 3;
  } else {
    const Count c2 = { 2 * n, shift + 1 }, gc2 = { 2 * words, shift + 1 + os };
    if (phase_ == 0) {
      moving_a_ = GroupA<true>(c2, gc2, hw, hs);
      fresh_ |= 1;
      if (hold_b_) {         // group B's ramp ended with the last call
        tk_.dtone = 0;
        mk_.dmix = 0;
        hold_b_ = false;
      }
    } else {
      moving_b_ = GroupB<true>(c2);
      fresh_ |= 2;
      if (split) {           // group A's second call: the halves move on
        const int32_t g = Hi(sk_.gain), b = Hi(ik_.beta);
        sk_.gain = Pack(g, g + Mul16(hw, Lo(sk_.dgain)));
        ik_.beta = Pack(b, b + Mul16(hs, Lo(ik_.dbeta)));
      }
    }
    phase_ ^= 1;
  }
  settled_ = !moved && at_target && fresh_ == 3 && (moving_a_ | moving_b_) == 0 && !hold_b_;
}

void Drive::Process(int16_t* l, int16_t* r, int32_t n, const DriveParams& p) {
  const bool split = !r && n >= kDriveYHistory && oversample_;
  Control(p, n, split);
#if DRIVE_C_REFS
  if (!oversample_) {
    RunNoOversampling(l, r, n);
    return;
  }
#endif
  if (split) {
    RunMonoSplit(l, n);
  } else {
    RunPasses(l, r, n);
  }
}

#if !DRIVE_C_REFS
#define DRIVE_PASS(name, ...) Drive##name##Asm(__VA_ARGS__)
#elif defined(__bfin__)
#define DRIVE_PASS(name, ...) (use_asm_ ? Drive##name##Asm(__VA_ARGS__) : Drive##name##C(__VA_ARGS__))
#else
#define DRIVE_PASS(name, ...) Drive##name##C(__VA_ARGS__)
#endif

// Stereo (or mono with r = l, for short blocks): L and R side by side.
void Drive::RunPasses(int16_t* l, int16_t* r, int32_t n) {
  const int16_t* in1 = r ? r : l;
  DRIVE_PASS(Interp, &ik_, l, in1, xh_, ys_ + kDriveYHistory, n);
  DRIVE_PASS(Shape, &sk_, ys_ + kDriveYHistory, 2 * n);
  DRIVE_PASS(Decim, ys_, yd_, n);
  for (int32_t i = 0; i < kDriveXHistory; ++i) xh_[i] = xh_[n + i];
  for (int32_t i = 0; i < kDriveYHistory; ++i) ys_[i] = ys_[2 * n + i];
  RunWet(l, r, n, 1);
}

// Passes 4..6 on the decimated wet (yd_, packed), the mix into l / r (r
// null: mono, fused; runs: see DriveMonoWetC).
void Drive::RunWet(int16_t* l, int16_t* r, int32_t n, int32_t runs) {
  if (!r) {
    const DriveMonoWetKernel mw = { &tk_, &dk_, &mk_ };
    DRIVE_PASS(MonoWet, &mw, yd_, l, n, runs);
    return;
  }
  DRIVE_PASS(Tone, &tk_, yd_, n);
  DRIVE_PASS(Dc, &dk_, yd_, n);
  DRIVE_PASS(Mix, &mk_, yd_, l, r, l, r, n);
}


// Mono, n >= 18: the block's halves side by side through the oversampled
// passes (no feedback there), each with the other's samples as history.
// The halves of the packed buffers as int16 (little-endian: word i's low
// half is [2 i], its high half [2 i + 1]).
typedef int16_t __attribute__((may_alias)) Alias16;

void Drive::RunMonoSplit(int16_t* l, int32_t n) {
  const int32_t h = n >> 1;
  Alias16* xs = reinterpret_cast<Alias16*>(xh_);
  Alias16* ys = reinterpret_cast<Alias16*>(ys_);
  // The second half's interpolator history: the first half's last samples.
  for (int32_t i = 0; i < kDriveXHistory; ++i) xs[2 * i + 1] = l[h - kDriveXHistory + i];
  DRIVE_PASS(Interp, &ik_, l, l + h, xh_, ys_ + kDriveYHistory, h);
  DRIVE_PASS(Shape, &sk_, ys_ + kDriveYHistory, n);
  // The second half's decimator history: the first half's last 18 2x samples.
  for (int32_t i = 0; i < kDriveYHistory; ++i) ys[2 * i + 1] = ys[2 * (n + i)];
  DRIVE_PASS(Decim, ys_, yd_, h);
  // The next call's history: the second half's end, in both halves.
  for (int32_t i = 0; i < kDriveXHistory; ++i) {
    const uint32_t w = static_cast<uint32_t>(xh_[h + i]) >> 16;
    xh_[i] = static_cast<int32_t>(w | (w << 16));
  }
  for (int32_t i = 0; i < kDriveYHistory; ++i) {
    const uint32_t w = static_cast<uint32_t>(ys_[n + i]) >> 16;
    ys_[i] = static_cast<int32_t>(w | (w << 16));
  }
  RunWet(l, nullptr, n, 2);
}

#if DRIVE_C_REFS
// Tests only: the same chain with the shaper at 44.1 kHz (no halfbands).
void Drive::RunNoOversampling(int16_t* l, int16_t* r, int32_t n) {
  const int16_t* in1 = r ? r : l;
  int32_t* y = ys_ + kDriveYHistory;
  int32_t beta = ik_.beta;
  for (int32_t i = 0; i < n; ++i) {
    const int32_t a = Sat16(Rnd(Fr(l[i], 16384)) + Lo(beta));
    const int32_t b = Sat16(Rnd(Fr(in1[i], 16384)) + Hi(beta));
    y[i] = Pack(a, b);
    beta = AddV(beta, ik_.dbeta);
  }
  DriveShapeC(&sk_, y, n);
  for (int32_t i = 0; i < n; ++i) yd_[i] = y[i];
  if (!r) {
    const DriveMonoWetKernel mw = { &tk_, &dk_, &mk_ };
    DriveMonoWetC(&mw, yd_, l, n, 1);
    return;
  }
  DriveToneC(&tk_, yd_, n);
  DriveDcC(&dk_, yd_, n);
  DriveMixC(&mk_, yd_, l, r, l, r, n);
}

#endif  // DRIVE_C_REFS

#undef DRIVE_PASS

}  // namespace op1fx
