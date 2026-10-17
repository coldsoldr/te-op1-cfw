// Component tests: each piece of the fixed-point port against the float copy
// of upstream (namespace op1marbles) with identical inputs.
//
//   exact:  RandomSequence (deja vu loop, replay, rewrite, clone), NextVector,
//           SemitonesToRatio, float rounding helpers, the divider-pattern
//           hysteresis quantizer, the quantizer's level selection
//   close:  beta distribution sampling, quantized voltages, lag processor
//
// Prints one line per test; exits 1 if an "exact" test has a mismatch or a
// "close" test exceeds its bound.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "upstream/random/distributions.h"
#include "upstream/random/lag_processor.h"
#include "upstream/random/quantizer.h"
#include "upstream/random/random_generator.h"
#include "upstream/random/random_sequence.h"
#include "upstream/random/random_stream.h"
#include "stmlib/dsp/hysteresis_quantizer.h"
#include "stmlib/dsp/units.h"

#include "src/distributions_fx.h"
#include "src/output_channel_fx.h"
#include "src/quantizer_fx.h"
#include "src/random_fx.h"
#include "src/t_generator_fx.h"
#include "src/tables_fx.h"

namespace fx = op1marbles::fx;
namespace up = op1marbles;

static int failures = 0;

static void Report(const char* name, bool ok, const char* fmt, double a = 0, double b = 0, double c = 0) {
  char detail[256];
  snprintf(detail, sizeof(detail), fmt, a, b, c);
  printf("%-34s %s  %s\n", name, ok ? "PASS" : "FAIL", detail);
  if (!ok) ++failures;
}

// A small deterministic generator for test inputs (independent of Marbles').
static uint32_t rng_state = 12345;
static uint32_t Rand() {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}
static int32_t RandQ16() { return static_cast<int32_t>(Rand() % 65537); }

static const up::Scale kScales[6] =
#include "upstream/scales_data.inc"
;

// --- RandomSequence ----------------------------------------------------------

static void TestRandomSequence() {
  long mismatches = 0, calls = 0;
  for (int trial = 0; trial < 200; ++trial) {
    uint32_t seed = Rand();
    up::RandomGenerator ug;
    up::RandomStream us;
    ug.Init(seed);
    us.Init(&ug);
    fx::RandomGenerator fg;
    fx::RandomStream fs;
    fg.Init(seed);
    fs.Init(&fg);
    static up::RandomSequence useq[2];
    static fx::RandomSequence fseq[2];
    for (int k = 0; k < 2; ++k) {
      useq[k].Init(&us);
      fseq[k].Init(&fs);
    }
    for (int step = 0; step < 2000; ++step) {
      int k = Rand() & 1;
      uint32_t op = Rand() % 100;
      if (op < 8) {
        int32_t dv = (Rand() & 3) == 0 ? 32768 : RandQ16();
        useq[k].set_deja_vu(static_cast<float>(dv) / 65536.0f);
        fseq[k].set_deja_vu(dv);
      } else if (op < 12) {
        int len = 1 + Rand() % 16;
        useq[k].set_length(len);
        fseq[k].set_length(len);
      } else if (op < 14) {
        useq[k].Record();
        fseq[k].Record();
      } else if (op < 16) {
        uint32_t h = Rand();
        useq[k].ReplayPseudoRandom(h);
        fseq[k].ReplayPseudoRandom(h);
      } else if (op < 17) {
        uint32_t sh = Rand() % 3;
        useq[k].ReplayShifted(sh);
        fseq[k].ReplayShifted(sh);
      } else if (op < 18) {
        useq[k].Reset();
        fseq[k].Reset();
      } else if (op < 19) {
        useq[k].Clone(useq[1 - k]);
        fseq[k].Clone(fseq[1 - k]);
      } else if (op < 23) {
        int32_t v = RandQ16() & 0xffff;
        float a = useq[k].RewriteValue(static_cast<float>(v) / 65536.0f);
        uint32_t b = fseq[k].RewriteValue(static_cast<uint32_t>(v) << 16);
        ++calls;
        if (static_cast<double>(a) * 4294967296.0 != static_cast<double>(b)) ++mismatches;
      } else if (op < 30) {
        float uv[6];
        uint32_t fv[6];
        useq[k].NextVector(uv, 6);
        fseq[k].NextVector(fv, 6);
        for (int i = 0; i < 6; ++i) {
          ++calls;
          if (static_cast<double>(uv[i]) * 4294967296.0 != static_cast<double>(fv[i])) ++mismatches;
        }
      } else {
        bool det = (Rand() & 7) == 0;
        int32_t v = RandQ16() & 0xffff;
        float a = useq[k].NextValue(det, static_cast<float>(v) / 65536.0f);
        uint32_t b = fseq[k].NextValue(det, static_cast<uint32_t>(v) << 16);
        ++calls;
        if (static_cast<double>(a) * 4294967296.0 != static_cast<double>(b)) ++mismatches;
      }
    }
  }
  Report("RandomSequence (exact)", mismatches == 0, "%.0f values, %.0f mismatches",
         static_cast<double>(calls), static_cast<double>(mismatches));
}

// --- Float rounding helpers --------------------------------------------------

static void TestRounding() {
  long bad = 0, n = 0;
  for (int i = 0; i < 2000000; ++i) {
    uint64_t x = (static_cast<uint64_t>(Rand()) << 32) | Rand();
    x >>= Rand() % 64;
    float f = static_cast<float>(x);
    uint64_t r = fx::RoundToFloat(x);
    ++n;
    if (static_cast<double>(f) != static_cast<double>(r)) ++bad;
  }
  long bad_u = 0;
  for (int i = 0; i < 2000000; ++i) {
    uint32_t w = Rand() >> (Rand() % 32);
    float f = static_cast<float>(w) / 4294967296.0f;
    uint32_t u = fx::WordToUnit(w);
    if (f < 1.0f && static_cast<double>(f) * 4294967296.0 != static_cast<double>(u)) ++bad_u;
  }
  long bad_r = 0;
  for (int i = 0; i < 1000000; ++i) {
    float x = std::ldexp(1.0f + static_cast<float>(Rand() & 0x7fffff) / 8388608.0f,
                         static_cast<int>(Rand() % 40) - 20);
    float r = 1.0f / x;
    int e;
    double m = std::frexp(static_cast<double>(x), &e);
    fx::Float24 a = { static_cast<uint32_t>(std::ldexp(m, 24)), e - 24 };
    fx::Float24 q = fx::Float24Reciprocal(a);
    if (std::ldexp(static_cast<double>(q.m), q.e) != static_cast<double>(r)) ++bad_r;
  }
  Report("RoundToFloat / WordToUnit (exact)", bad == 0 && bad_u == 0,
         "%.0f + 2e6 values, %.0f + %.0f mismatches", static_cast<double>(n),
         static_cast<double>(bad), static_cast<double>(bad_u));
  Report("Float24Reciprocal (exact)", bad_r == 0, "1e6 values, %.0f mismatches",
         static_cast<double>(bad_r));
}

static void TestSemitones() {
  long bad = 0, n = 0;
  for (int32_t s = -127 * 256; s <= 127 * 256; ++s) {
    float ref = stmlib::SemitonesToRatio(static_cast<float>(s) / 256.0f);
    fx::Float24 r = fx::SemitonesToRatioFloat(s);
    ++n;
    if (std::ldexp(static_cast<double>(r.m), r.e) != static_cast<double>(ref)) ++bad;
  }
  Report("SemitonesToRatio (exact, Q8)", bad == 0, "%.0f values, %.0f mismatches",
         static_cast<double>(n), static_cast<double>(bad));
}

// --- Divider-pattern hysteresis quantizer -------------------------------------

static void TestHysteresisQuantizer2() {
  stmlib::HysteresisQuantizer2 u;
  fx::HysteresisQuantizer2 f;
  u.Init(17, 0.1f, false);
  f.Init(17, 26843546, false);  // 0.1f in Q28
  long bad = 0, n = 0;
  for (int pass = 0; pass < 3; ++pass) {
    for (int32_t i = 0; i <= 65536; ++i) {
      int32_t b = pass == 1 ? 65536 - i : (pass == 0 ? i : RandQ16());
      int a = u.Process(static_cast<float>(b) / 65536.0f);
      int c = f.Process(b);
      ++n;
      if (a != c) ++bad;
    }
  }
  Report("HysteresisQuantizer2 (exact)", bad == 0, "%.0f values, %.0f mismatches",
         static_cast<double>(n), static_cast<double>(bad));
}

// --- Beta distribution ----------------------------------------------------------

static void TestBeta() {
  double max_err = 0.0, sum_err = 0.0;
  long n = 0;
  for (int i = 0; i < 2000000; ++i) {
    uint32_t w = Rand();
    uint32_t uq = fx::WordToUnit(w);
    float u = static_cast<float>(w) / 4294967296.0f;
    if (u >= 1.0f) continue;
    int32_t spread = RandQ16(), bias = RandQ16();
    if ((i & 7) == 0) bias = 32768;
    float ref = up::BetaDistributionSample(u, spread / 65536.0f, bias / 65536.0f);
    double port = fx::BetaDistributionSample(uq, spread, bias) / 2147483648.0;
    double e = std::fabs(port - ref);
    if (e > max_err) max_err = e;
    sum_err += e;
    ++n;
  }
  long bad_fast = 0;
  double max_fast = 0.0;
  for (int i = 0; i < 1000000; ++i) {
    uint32_t w = Rand();
    float u = static_cast<float>(w) / 4294967296.0f;
    if (u >= 1.0f) continue;
    float ref = up::FastBetaDistributionSample(u);
    double port = fx::FastBetaDistributionSample(fx::WordToUnit(w)) / 2147483648.0;
    double e = std::fabs(port - ref);
    if (e > max_fast) max_fast = e;
  }
  Report("BetaDistributionSample (close)", max_err < 1e-6,
         "max |err| %.2e, mean %.2e (bound 1e-6 of the unit range)", max_err, sum_err / n);
  Report("FastBetaDistributionSample (close)", max_fast < 1e-6, "max |err| %.2e", max_fast);
}

// --- Quantizer ----------------------------------------------------------------

static void TestQuantizer() {
  long n = 0, level_bad = 0, note_bad = 0, near = 0;
  double max_dv = 0.0;
  for (int s = 0; s < 6; ++s) {
    static up::Quantizer uq;
    static fx::Quantizer fq;
    uq.Init(kScales[s]);
    fq.Init(fx::preset_scales[s]);
    for (int i = 0; i < 200000; ++i) {
      int32_t steps_q18 = (RandQ16() * 4) & ~3;  // a Q16 steps value, in Q18
      if (i & 1) steps_q18 = static_cast<int32_t>(Rand() % (1 << 18));  // interpolated
      int32_t amount_q18 = 2 * steps_q18 - (1 << 18);
      int32_t v = static_cast<int32_t>(Rand() % (10 * 65536)) - 5 * 65536;
      float ref = uq.Process(v / 65536.0f, amount_q18 / 262144.0f, false);
      int32_t port = fq.Process(v, amount_q18, false);
      ++n;
      double dv = std::fabs(port / 65536.0 - ref);
      if (dv > max_dv && dv < 0.01) max_dv = dv;
      if (dv >= 0.01) {
        ++note_bad;
        // Is the input within 1e-4 V of a decision midpoint? (expected)
        float r2 = uq.Process(v / 65536.0f + 1e-4f, amount_q18 / 262144.0f, false);
        float r3 = uq.Process(v / 65536.0f - 1e-4f, amount_q18 / 262144.0f, false);
        if (r2 != ref || r3 != ref) ++near;
        uq.Process(v / 65536.0f, amount_q18 / 262144.0f, false);
      }
    }
  }
  // The level (which weights count) comes from stmlib's HysteresisQuantizer.
  {
    stmlib::HysteresisQuantizer uh;
    fx::HysteresisQuantizer fh;
    uh.Init();
    fh.Init();
    long m = 0;
    for (int i = 0; i < 2000000; ++i) {
      int32_t steps_q18 = (i & 1) ? static_cast<int32_t>(Rand() % ((1 << 18) + 1))
                                  : (RandQ16() * 4);
      int32_t amount_q18 = 2 * steps_q18 - (1 << 18);
      int a = uh.Process(amount_q18 / 262144.0f, 8);
      int b = fh.Process(amount_q18, 8);
      ++m;
      if (a != b) ++level_bad;
    }
    Report("Quantizer level selection (exact)", level_bad == 0, "%.0f amounts, %.0f mismatches",
           static_cast<double>(m), static_cast<double>(level_bad));
  }
  Report("Quantizer note (exact but midpoints)", note_bad == near,
         "%.0f different notes, %.0f of them within 1e-4 V of a midpoint",
         static_cast<double>(note_bad), static_cast<double>(near));
  Report("Quantizer voltage (close)", max_dv < 2e-5, "max |dv| on equal notes %.2e V "
         "(degree voltages rounded to Q16)", max_dv);
}

// --- Lag processor ----------------------------------------------------------------

static void TestLag() {
  double max_err = 0.0;
  long n = 0;
  for (int trial = 0; trial < 300; ++trial) {
    static up::LagProcessor ul;
    static fx::LagProcessor fl;
    ul.Init();
    fl.Init();
    int32_t steps_q16 = static_cast<int32_t>(Rand() % 32768);  // smooth range
    int32_t smooth_q18 = (1 << 18) - 2 * steps_q16 * 4;
    float smooth = smooth_q18 / 262144.0f;
    uint32_t inc = (Rand() % 40000 + 200) << 8;  // ramp increment (multiple of 2^-24)
    uint32_t phase = 0;
    int32_t value = static_cast<int32_t>(Rand() % (10 * 65536)) - 5 * 65536;
    for (int i = 0; i < 4000; ++i) {
      uint32_t next = phase + inc;
      if (next < phase) {  // new value at each wrap, as OutputChannel does
        value = static_cast<int32_t>(Rand() % (10 * 65536)) - 5 * 65536;
        ul.ResetRamp();
        fl.ResetRamp();
      }
      phase = next;
      float ref = ul.Process(value / 65536.0f, smooth, phase / 4294967296.0f);
      int32_t port = fl.Process(value, smooth_q18, phase);
      double e = std::fabs(port / 65536.0 - ref);
      if (e > max_err) max_err = e;
      ++n;
    }
  }
  Report("LagProcessor (close)", max_err < 1e-3, "max |err| %.2e V over %.0f samples (bound 1 mV)",
         max_err, static_cast<double>(n));
}

int main() {
  TestRandomSequence();
  TestRounding();
  TestSemitones();
  TestHysteresisQuantizer2();
  TestBeta();
  TestQuantizer();
  TestLag();
  printf("%s: %d failing test(s)\n", failures ? "FAIL" : "PASS", failures);
  return failures ? 1 : 0;
}
