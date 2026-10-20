// The ensemble's checks (make test-ensemble), built for the Blackfin
// simulator and for the host:
//   kernels    each assembly side kernel against its C reference on random
//              runs (random lines with full-scale samples, random delays
//              over the whole reach, random gains and steps including
//              -32768, n 1..64), guard words round the outputs;
//   ensemble   a C instance and an assembly instance through scenarios
//              (knob sweeps and jumps, odd n, mono/stereo in and out, the
//              chorus <-> ensemble switch), many trips round the lines:
//              outputs and lines equal, guard words round the objects
//              intact; the C output's checksums, which the host build
//              prints too;
//   cycles     per call (32 samples at 22050 Hz, 689 calls/s);
//   width      a mono input to the stereo output: L/R correlation;
//   level      wet/in RMS, the delay at rest (latency);
//   stability  60 s with the knobs at the top and sweeping;
//   float      (host only) against a double-precision model with exact
//              sine LFOs: SNR.
//
// Integer only on the Blackfin: float there is soft-float.

#include <cstdint>
#include <cstdio>
#include <cstring>

#ifndef __bfin__
#include <cmath>
#endif

#include "rings/harness/cycles.h"
#include "src/ensemble/ensemble.h"

using op1fx::Ensemble;
using op1fx::EnsembleParams;
using op1fx::EnsembleSideKernel;

namespace {

uint32_t rng = 12345;
uint32_t Next() {
  rng = rng * 1664525u + 1013904223u;
  return rng;
}
int32_t Range(int32_t lo, int32_t hi) {   // [lo, hi]
  return lo + static_cast<int32_t>(Next() % static_cast<uint32_t>(hi - lo + 1));
}
int16_t RandomSample() {
  const uint32_t r = Next();
  if ((r & 15) == 0) return (r & 16) ? 32767 : -32768;
  return static_cast<int16_t>(r >> 16);
}
int32_t RandomCoef() {
  const uint32_t r = Next();
  if ((r & 7) == 0) return (r & 8) ? 32767 : -32768;
  return static_cast<int16_t>(r >> 16);
}
int32_t PackCoefs(int32_t lo, int32_t hi) {
  return (lo & 0xffff) | static_cast<int32_t>(static_cast<uint32_t>(hi) << 16);
}
int32_t RandomPair() {   // sequenced: argument order is unspecified
  const int32_t lo = RandomCoef();
  const int32_t hi = RandomCoef();
  return PackCoefs(lo, hi);
}

uint32_t checksum = 2166136261u;
void Hash(uint32_t x) {
  checksum ^= x;
  checksum *= 16777619u;
}

int failures = 0;

int32_t ISqrt(int64_t x) {
  if (x <= 0) return 0;
  int64_t r = 0, bit = int64_t(1) << 62;
  while (bit > x) bit >>= 2;
  while (bit) {
    if (x >= r + bit) {
      x -= r + bit;
      r = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
    bit >>= 2;
  }
  return static_cast<int32_t>(r);
}

// --- Kernels -----------------------------------------------------------------

const int32_t kGuardWords = 32;
const int16_t kGuard = 0x5a5a;
int16_t lines[2][EN_WORDS];
int16_t out_c[64 + kGuardWords], out_a[64 + kGuardWords];

void TestSideKernel(int32_t voices) {
  int bad = 0;
  bool guards = true;
  for (int trial = 0; trial < 3000; ++trial) {
    if (trial % 100 == 0) {
      for (int32_t j = 0; j < 2; ++j) {
        for (int32_t i = 0; i < EN_WORDS; ++i) lines[j][i] = RandomSample();
      }
    }
    const int32_t n = Range(1, 64);
    EnsembleSideKernel k;
    k.line_a = lines[0] + Range(n - 1, EN_SIZE - 1);
    k.line_b = lines[Range(0, 1)] + Range(n - 1, EN_SIZE - 1);
    for (int32_t j = 0; j < 3; ++j) {
      // Delays anywhere in [0, EN_REACH - 2] samples over the run.
      const int32_t hi = (EN_REACH - 2) << 16;
      int32_t pos = Range(0, hi);
      int32_t step = static_cast<int32_t>(Next()) >> 14;   // up to 2 samples per sample
      if (trial % 5 == 0) step = 0;
      if (trial % 7 == 0) pos &= ~0xffff;
      const int64_t end = static_cast<int64_t>(pos) + static_cast<int64_t>(step) * n;
      if (end < 0 || end > hi) step = 0;
      k.pos[j] = pos;
      k.step[j] = step;
    }
    k.g12 = RandomPair();
    k.g12_step = RandomPair();
    k.g3 = RandomPair();
    if (trial % 9 == 0) k.g12_step = k.g3 = 0;
    for (int32_t i = 0; i < 64 + kGuardWords; ++i) out_c[i] = out_a[i] = kGuard;
    op1fx::EnsembleSideRunC(&k, out_c, n, voices);
#ifdef __bfin__
    if (voices == 3) {
      op1fx::EnsembleSide3Asm(&k, out_a, n);
    } else {
      op1fx::EnsembleSide1Asm(&k, out_a, n);
    }
    if (memcmp(out_c, out_a, 2 * n) != 0) {
      if (bad < 4) {
        printf("side kernel %ld: MISMATCH trial %d n %ld\n", static_cast<long>(voices), trial,
               static_cast<long>(n));
        for (int32_t i = 0; i < n && i < 4; ++i) {
          printf("  [%ld] c %d asm %d\n", static_cast<long>(i), out_c[i], out_a[i]);
        }
      }
      ++bad;
    }
#endif
    for (int32_t i = n; i < 64 + kGuardWords; ++i) {
      if (out_c[i] != kGuard || out_a[i] != kGuard) guards = false;
    }
    for (int32_t i = 0; i < n; ++i) Hash(static_cast<uint16_t>(out_c[i]));
  }
#ifdef __bfin__
  printf("side kernel, %ld voice(s): %s (3000 runs, asm = C), guards %s\n",
         static_cast<long>(voices), bad ? "FAIL" : "ok", guards ? "intact" : "HIT");
#else
  printf("side kernel, %ld voice(s): C only, guards %s\n", static_cast<long>(voices),
         guards ? "intact" : "HIT");
#endif
  if (bad || !guards) ++failures;
}

// --- Whole effect ----------------------------------------------------------------

struct GuardedEnsemble {
  uint32_t before[16];
  Ensemble fx;
  uint32_t after[16];
};
GuardedEnsemble ens_c, ens_a;
const uint32_t kGuardWord = 0xa5c3a5c3u;

void ArmGuards(GuardedEnsemble* g) {
  for (int32_t i = 0; i < 16; ++i) g->before[i] = g->after[i] = kGuardWord;
}
bool GuardsOk(const GuardedEnsemble& g) {
  for (int32_t i = 0; i < 16; ++i) {
    if (g.before[i] != kGuardWord || g.after[i] != kGuardWord) return false;
  }
  return true;
}

struct Source {
  uint32_t noise;
  uint32_t phase;
  void Init() {
    noise = 1;
    phase = 0;
  }
  int16_t Noise(int32_t amp) {
    noise = noise * 1664525u + 1013904223u;
    return static_cast<int16_t>((static_cast<int32_t>(noise) >> 16) * amp >> 15);
  }
  int16_t Saw(uint32_t inc, int32_t amp) {
    phase += inc;
    return static_cast<int16_t>((static_cast<int32_t>(phase) >> 16) * amp >> 15);
  }
};

const uint32_t kSaw220 = 0x00a3d70a;   // ~ 220 Hz at 22050

EnsembleParams Knobs(int32_t rate, int32_t depth, int32_t voices) {
  EnsembleParams p;
  p.rate = rate;
  p.depth = depth;
  p.voices = voices;
  return p;
}

void RunScenario(const char* name, int32_t scenario, int32_t calls, bool mono_in, bool mono_out) {
  ArmGuards(&ens_c);
  ArmGuards(&ens_a);
  ens_c.fx.Init();
  ens_a.fx.Init();
  ens_c.fx.set_use_asm(false);
#ifdef __bfin__
  ens_a.fx.set_use_asm(true);
#endif
  Source src;
  src.Init();
  checksum = 2166136261u;
  int32_t mismatch_call = -1;
  int32_t peak = 0;
  for (int32_t call = 0; call < calls; ++call) {
    EnsembleParams p = Knobs(16384, 16384, 32767);
    const int32_t n = (scenario == 2) ? 2 + 2 * (call % 32) : 32;
    int16_t l[64], r[64];
    for (int32_t i = 0; i < n; ++i) {
      int16_t s = 0, t = 0;
      switch (scenario) {
        case 0:   // noise, the knobs sweeping
          s = src.Noise(16000);
          t = src.Noise(16000);
          p = Knobs((call * 37) & 32767, 32767 - ((call * 23) & 32767), (call * 11) & 32767);
          break;
        case 1:   // a saw, everything at the top
          s = t = src.Saw(kSaw220, 30000);
          p = Knobs(32767, 32767, 32767);
          break;
        case 2:   // full-scale noise, odd n, every knob jumping
          s = src.Noise(32767);
          t = static_cast<int16_t>((call & 1) ? 32767 : -32768);
          p = Knobs((call * 977) & 32767, (call * 331) & 32767, (call * 1231) & 32767);
          if ((call / 50) % 3 == 0) p.voices = 0;
          break;
        default:  // chorus <-> ensemble every 100 calls (the one-voice kernel)
          s = src.Saw(kSaw220, 20000);
          t = src.Noise(8000);
          p = Knobs(8000, 24000, (call / 100) % 2 ? 32767 : 0);
          break;
      }
      l[i] = s;
      r[i] = t;
    }
    int16_t lc[64], rc[64], la[64], ra[64];
    ens_c.fx.Process(l, mono_in ? nullptr : r, lc, mono_out ? nullptr : rc, n, p);
    ens_a.fx.Process(l, mono_in ? nullptr : r, la, mono_out ? nullptr : ra, n, p);
    for (int32_t i = 0; i < n; ++i) {
      Hash(static_cast<uint16_t>(lc[i]));
      if (!mono_out) Hash(static_cast<uint16_t>(rc[i]));
      const int32_t a = lc[i] < 0 ? -lc[i] : lc[i];
      if (a > peak) peak = a;
      if (mismatch_call < 0 && (lc[i] != la[i] || (!mono_out && rc[i] != ra[i]))) mismatch_call = call;
    }
  }
  const bool lines_equal = memcmp(ens_c.fx.line(0), ens_a.fx.line(0), 2 * EN_WORDS) == 0 &&
                           memcmp(ens_c.fx.line(1), ens_a.fx.line(1), 2 * EN_WORDS) == 0;
  const bool guards = GuardsOk(ens_c) && GuardsOk(ens_a);
  printf("ensemble %-30s checksum %08lx peak %5ld", name, static_cast<unsigned long>(checksum),
         static_cast<long>(peak));
#ifdef __bfin__
  printf("  asm %s", mismatch_call < 0 && lines_equal ? "= C" : "DIFFERS");
  if (mismatch_call >= 0) printf(" (call %ld)", static_cast<long>(mismatch_call));
  if (mismatch_call >= 0 || !lines_equal) ++failures;
#else
  (void)lines_equal;
#endif
  printf("  guards %s\n", guards ? "intact" : "HIT");
  if (!guards) ++failures;
}

// In-place calls (wet = in) give the same output as separate buffers.
void TestInPlace() {
  Ensemble* a = &ens_c.fx;
  Ensemble* b = &ens_a.fx;
  a->Init();
  b->Init();
  Source src;
  src.Init();
  bool ok = true;
  for (int32_t call = 0; call < 300; ++call) {
    const EnsembleParams p = Knobs(20000, 30000, (call * 97) & 32767);
    int16_t l[32], r[32], l2[32], r2[32], wl[32], wr[32];
    for (int32_t i = 0; i < 32; ++i) {
      l[i] = l2[i] = src.Noise(20000);
      r[i] = r2[i] = src.Noise(20000);
    }
    a->Process(l, r, wl, wr, 32, p);
    b->Process(l2, r2, l2, r2, 32, p);
    if (memcmp(wl, l2, sizeof(wl)) != 0 || memcmp(wr, r2, sizeof(wr)) != 0) ok = false;
  }
  printf("in-place calls: %s\n", ok ? "ok" : "DIFFER");
  if (!ok) ++failures;
}

// --- Cycles ------------------------------------------------------------------

Ensemble bench;

void Cycles(const char* name, bool use_asm, bool mono_in, bool mono_out, int32_t voices) {
  bench.Init();
  bench.set_use_asm(use_asm);
  Source src;
  src.Init();
  const EnsembleParams p = Knobs(20000, 24000, voices);
  harness::CycleStats stats;
  for (int32_t call = 0; call < 400; ++call) {
    int16_t l[32], r[32], wl[32], wr[32];
    for (int32_t i = 0; i < 32; ++i) {
      l[i] = src.Noise(12000);
      r[i] = src.Noise(12000);
    }
    const uint64_t t0 = harness::ReadCycles();
    bench.Process(l, mono_in ? nullptr : r, wl, mono_out ? nullptr : wr, 32, p);
    const uint64_t t1 = harness::ReadCycles();
    if (call >= 50) stats.Add(t1 - t0);
  }
#ifdef __bfin__
  const unsigned long avg = static_cast<unsigned long>(stats.total / stats.n);
  const unsigned long khz_avg = static_cast<unsigned long>(stats.total * 689 / stats.n / 1000);
  const unsigned long khz_max = static_cast<unsigned long>(stats.max * 689 / 1000);
  printf("cycles %-30s per call avg %5lu min %5lu max %5lu  per sample %3lu  => %lu.%02lu MHz avg, %lu.%02lu MHz peak\n",
         name, avg, static_cast<unsigned long>(stats.min), static_cast<unsigned long>(stats.max),
         avg / 32, khz_avg / 1000, (khz_avg % 1000) / 10, khz_max / 1000, (khz_max % 1000) / 10);
#else
  (void)name;
#endif
}

// --- Width, level, latency -----------------------------------------------------------

struct Stats {
  int64_t ll, rr, lr, in2;
  void Clear() { ll = rr = lr = in2 = 0; }
};

// A mono input (saw or noise) through `calls` calls; statistics after the
// first 100.
void Measure(const EnsembleParams& p, bool noise, int32_t calls, Stats* st) {
  bench.Init();
#ifdef __bfin__
  bench.set_use_asm(true);
#endif
  Source src;
  src.Init();
  st->Clear();
  for (int32_t call = 0; call < calls; ++call) {
    int16_t x[32], wl[32], wr[32];
    for (int32_t i = 0; i < 32; ++i) x[i] = noise ? src.Noise(12000) : src.Saw(kSaw220, 12000);
    bench.Process(x, nullptr, wl, wr, 32, p);
    if (call < 100) continue;
    for (int32_t i = 0; i < 32; ++i) {
      st->ll += static_cast<int64_t>(wl[i]) * wl[i];
      st->rr += static_cast<int64_t>(wr[i]) * wr[i];
      st->lr += static_cast<int64_t>(wl[i]) * wr[i];
      st->in2 += static_cast<int64_t>(x[i]) * x[i];
    }
  }
}

// Correlation in permille.
int32_t Corr(const Stats& s) {
  const int64_t d = static_cast<int64_t>(ISqrt(s.ll)) * ISqrt(s.rr);
  return d ? static_cast<int32_t>(s.lr * 1000 / d) : 0;
}
// Wet (left) / input RMS in permille.
int32_t Level(const Stats& s) {
  const int32_t in = ISqrt(s.in2);
  return in ? static_cast<int32_t>(static_cast<int64_t>(ISqrt(s.ll)) * 1000 / in) : 0;
}

void Width() {
  struct Case {
    const char* name;
    EnsembleParams p;
    bool noise;
    int32_t max_corr;   // permille; -1001: no check
  };
  const Case cases[] = {
    { "depth 0 (static delay), saw", Knobs(16384, 0, 32767), false, -1001 },
    { "chorus, depth .5, saw", Knobs(16384, 16384, 0), false, 900 },
    { "chorus, depth 1, saw", Knobs(16384, 32767, 0), false, 900 },
    { "ensemble, depth .5, saw", Knobs(16384, 16384, 32767), false, 900 },
    { "ensemble, depth 1, saw", Knobs(16384, 32767, 32767), false, 900 },
    { "two voices, depth .5, saw", Knobs(16384, 16384, 16384), false, 900 },
    { "chorus, depth .5, noise", Knobs(16384, 16384, 0), true, 300 },
    { "ensemble, depth .5, noise", Knobs(16384, 16384, 32767), true, 500 },
    { "ensemble, rate 8 Hz, depth 1, saw", Knobs(32767, 32767, 32767), false, 995 },
  };
  for (const Case& c : cases) {
    Stats st;
    Measure(c.p, c.noise, 1500, &st);   // ~2 s
    const int32_t corr = Corr(st);
    const int32_t level = Level(st);
    const bool ok = c.max_corr == -1001 ? corr == 1000 : corr <= c.max_corr;
    printf("width %-36s L/R correlation %5ld permille, wet/in RMS %4ld permille  %s\n", c.name,
           static_cast<long>(corr), static_cast<long>(level), ok ? "ok" : "FAIL");
    if (!ok) ++failures;
  }
}

// The delay at rest: an impulse with depth 0 comes out EN_BASE samples later.
void Latency() {
  bench.Init();
#ifdef __bfin__
  bench.set_use_asm(true);
#endif
  int32_t first = -1, peak_at = -1, peak = 0;
  for (int32_t call = 0; call < 20; ++call) {
    int16_t x[32], wl[32], wr[32];
    for (int32_t i = 0; i < 32; ++i) x[i] = (call == 0 && i == 0) ? 20000 : 0;
    bench.Process(x, x, wl, wr, 32, Knobs(16384, 0, 0));
    for (int32_t i = 0; i < 32; ++i) {
      const int32_t t = call * 32 + i;
      if (first < 0 && wl[i] != 0) first = t;
      if (wl[i] > peak) {
        peak = wl[i];
        peak_at = t;
      }
    }
  }
  const bool ok = first == EN_BASE && peak_at == EN_BASE && peak >= 19990;
  printf("latency: impulse at depth 0 out after %ld samples (%ld.%ld ms at 22050), peak %ld  %s\n",
         static_cast<long>(peak_at), static_cast<long>(peak_at * 10000 / 22050 / 10),
         static_cast<long>(peak_at * 10000 / 22050 % 10), static_cast<long>(peak), ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

// --- Stability ------------------------------------------------------------------

void Stability(const char* name, int32_t scenario) {
  bench.Init();
#ifdef __bfin__
  bench.set_use_asm(true);
#endif
  Source src;
  src.Init();
  const int32_t calls_per_10s = 6891;
  checksum = 2166136261u;
  printf("stability %s:\n", name);
  for (int32_t seg = 0; seg < 6; ++seg) {
    int32_t peak = 0;
    int64_t sum = 0, sum2 = 0, in2 = 0, insum = 0;
    for (int32_t call = 0; call < calls_per_10s; ++call) {
      const int32_t t = seg * calls_per_10s + call;
      EnsembleParams p = Knobs(32767, 32767, 32767);
      if (scenario == 1) p = Knobs((t * 7) & 32767, 32767 - ((t * 3) & 32767), (t * 5) & 32767);
      int16_t l[32], r[32], wl[32], wr[32];
      for (int32_t i = 0; i < 32; ++i) {
        l[i] = scenario == 0 ? src.Saw(kSaw220, 16384) : src.Noise(16384);
        r[i] = scenario == 0 ? l[i] : src.Noise(16384);
      }
      bench.Process(l, r, wl, wr, 32, p);
      for (int32_t i = 0; i < 32; ++i) {
        const int32_t a = wl[i] < 0 ? -wl[i] : wl[i];
        const int32_t b = wr[i] < 0 ? -wr[i] : wr[i];
        if (a > peak) peak = a;
        if (b > peak) peak = b;
        sum += wl[i] + wr[i];
        sum2 += static_cast<int64_t>(wl[i]) * wl[i] + static_cast<int64_t>(wr[i]) * wr[i];
        in2 += static_cast<int64_t>(l[i]) * l[i] + static_cast<int64_t>(r[i]) * r[i];
        insum += l[i] + r[i];
        Hash(static_cast<uint16_t>(wl[i]));
      }
    }
    const int64_t count = 2 * 32 * static_cast<int64_t>(calls_per_10s);
    const int32_t rms = ISqrt(sum2 / count);
    const int32_t in_rms = ISqrt(in2 / count);
    printf("  %2ld-%2ld s: wet peak %5ld rms %5ld (input rms %5ld) dc %4ld (input %4ld)\n",
           static_cast<long>(seg * 10), static_cast<long>(seg * 10 + 10), static_cast<long>(peak),
           static_cast<long>(rms), static_cast<long>(in_rms), static_cast<long>(sum / count),
           static_cast<long>(insum / count));
  }
  printf("stability %s checksum %08lx\n", name, static_cast<unsigned long>(checksum));
}

// --- Float reference (host) ------------------------------------------------------------

#ifndef __bfin__

// The same design in double, from the knob formulas (not the tables): exact
// sine LFOs evaluated at every sample, exact gains, exact linear
// interpolation, one plain ring per input.
struct FloatEnsemble {
  double ring[2][4096];
  int32_t w;
  double slow_phase, fast_phase;   // cycles
  const op1fx::EnsembleCoefs* same;   // non-null: take these delays and gains
  void Init() {
    same = nullptr;
    memset(ring, 0, sizeof(ring));
    w = 0;
    slow_phase = fast_phase = 0;
  }
  double Read(int32_t line, double d) const {
    const int32_t ip = static_cast<int32_t>(std::floor(d));
    const double f = d - ip;
    const double a = ring[line][(w - ip) & 4095];
    const double b = ring[line][(w - ip - 1) & 4095];
    return a * (1 - f) + b * f;
  }
  // depth, voices in [0, 1], rate in [0, 1].
  void Process(const int16_t* in_l, const int16_t* in_r, double* out_l, double* out_r, int32_t n,
               double rate, double depth, double voices) {
    const double fs = 22050.0, pi = 3.14159265358979323846;
    const double f = 0.05 * std::pow(160.0, rate);
    const double cap = std::fmin(EN_SLOW_MAX, 0.02 * fs / (2 * pi * f));
    const double a = depth * cap, b = depth * voices * EN_FAST_MAX;
    double g[3];
    if (voices <= 0.5) {
      g[0] = 1 - voices;
      g[1] = voices;
      g[2] = 0;
    } else {
      const double t = 2 * voices - 1;
      g[0] = g[1] = 0.5 - t / 6;
      g[2] = t / 3;
    }
    const int32_t lr = in_r ? 1 : 0;
    for (int32_t i = 0; i < n; ++i) {
      w = (w + 1) & 4095;
      ring[0][w] = in_l[i];
      ring[1][w] = in_r ? in_r[i] : in_l[i];
      double d[6];
      for (int32_t k = 0; k < 3; ++k) {
        const double m = a * std::sin(2 * pi * (slow_phase + k / 3.0)) +
                         b * std::sin(2 * pi * (fast_phase + k / 3.0));
        d[k] = EN_BASE + m;
        d[3 + k] = EN_BASE - m;
      }
      if (same) {
        // The fixed point's own delays and gains: only the arithmetic differs.
        for (int32_t j = 0; j < 6; ++j) d[j] = (same->pos0[j] + double(same->pos_step[j]) * i) / 65536.0;
        for (int32_t j = 0; j < 3; ++j) g[j] = (same->g0[j] + double(same->g_step[j]) * i) / 32768.0;
      }
      out_l[i] = g[0] * Read(0, d[0]) + g[1] * Read(0, d[1]) + g[2] * Read(lr, d[2]);
      out_r[i] = g[0] * Read(lr, d[3]) + g[1] * Read(lr, d[4]) + g[2] * Read(0, d[5]);
      slow_phase += f / fs;
      fast_phase += 6.57 / fs;
      slow_phase -= std::floor(slow_phase);
      fast_phase -= std::floor(fast_phase);
    }
  }
};
FloatEnsemble fref;

void CompareFloat(const char* name, int32_t rate, int32_t depth, int32_t voices, bool mono_in,
                  bool same_delays) {
  bench.Init();
  fref.Init();
  if (same_delays) fref.same = &bench.coefs();
  Source src;
  src.Init();
  const EnsembleParams p = Knobs(rate, depth, voices);
  double sig[2] = { 0, 0 }, err[2] = { 0, 0 }, peak_err = 0;
  for (int32_t call = 0; call < 3000; ++call) {
    int16_t l[32], r[32], wl[32], wr[32];
    double fl[32], fr[32];
    for (int32_t i = 0; i < 32; ++i) {
      l[i] = src.Saw(kSaw220, 10000);
      r[i] = static_cast<int16_t>(src.Noise(6000) + (l[i] >> 1));
    }
    bench.Process(l, mono_in ? nullptr : r, wl, wr, 32, p);
    // The fixed point's first call jumps to the end-of-call delays; the
    // float model starts at phase 0. They agree from the second call on
    // (the ramps start at the first call's end), so skip a few.
    fref.Process(l, mono_in ? nullptr : r, fl, fr, 32, rate / 32768.0, depth / 32768.0,
                 voices / 32768.0);
    if (call < 20) continue;
    for (int32_t i = 0; i < 32; ++i) {
      const double e0 = wl[i] - fl[i], e1 = wr[i] - fr[i];
      sig[0] += fl[i] * fl[i];
      sig[1] += fr[i] * fr[i];
      err[0] += e0 * e0;
      err[1] += e1 * e1;
      peak_err = std::fmax(peak_err, std::fmax(std::fabs(e0), std::fabs(e1)));
    }
  }
  const double count = (3000 - 20) * 32.0;
  printf("float %-30s %-11s SNR L %5.1f dB R %5.1f dB, error rms %.2f LSB, peak %.0f LSB\n", name,
         same_delays ? "(arith.)" : "(exact LFO)",
         10 * std::log10(sig[0] / err[0]), 10 * std::log10(sig[1] / err[1]),
         std::sqrt((err[0] + err[1]) / (2 * count)), peak_err);
}

#endif

}  // namespace

int main() {
  harness::EnableCycleCounter();
  TestSideKernel(3);
  TestSideKernel(1);
  printf("kernels checksum %08lx\n", static_cast<unsigned long>(checksum));

  RunScenario("noise, knobs sweeping", 0, 1500, false, false);
  RunScenario("saw, all at the top", 1, 1500, false, false);
  RunScenario("full-scale, odd n, knobs jump", 2, 1500, false, false);
  RunScenario("chorus <-> ensemble", 3, 1500, false, false);
  RunScenario("mono in, mono out", 0, 1200, true, true);
  RunScenario("mono in, stereo out", 3, 1200, true, false);
  RunScenario("stereo in, mono out", 2, 1200, false, true);
  TestInPlace();

#ifdef __bfin__
  Cycles("stereo, ensemble (3 voices)", true, false, false, 32767);
  Cycles("stereo, 2 voices", true, false, false, 16384);
  Cycles("stereo, chorus (1 voice)", true, false, false, 0);
  Cycles("mono in, stereo out, ensemble", true, true, false, 32767);
  Cycles("mono, ensemble", true, true, true, 32767);
  Cycles("mono, chorus", true, true, true, 0);
  Cycles("C stereo, ensemble", false, false, false, 32767);
  Cycles("C mono, ensemble", false, true, true, 32767);
#endif

  Width();
  Latency();
  Stability("all at the top, held saw", 0);
  Stability("knobs sweeping, stereo noise", 1);

#ifndef __bfin__
  // Against exact sine LFOs (the delay trajectories differ by <= 0.03
  // samples: the sine table and the per-call ramps), then with the fixed
  // point's own delays and gains (the arithmetic alone).
  for (int32_t same = 0; same < 2; ++same) {
    CompareFloat("ensemble, rate .5, depth 1", 16384, 32767, 32767, false, same);
    CompareFloat("ensemble, rate 8 Hz, depth 1", 32767, 32767, 32767, false, same);
    CompareFloat("chorus, rate .5, depth .5", 16384, 16384, 0, false, same);
    CompareFloat("two voices, rate .75, depth 1", 24576, 32767, 16384, false, same);
    CompareFloat("ensemble, mono in, rate .5", 16384, 32767, 32767, true, same);
  }
#endif

  printf("%s: %d failure(s)\n", failures ? "FAIL" : "ok", failures);
  return failures ? 1 : 0;
}
