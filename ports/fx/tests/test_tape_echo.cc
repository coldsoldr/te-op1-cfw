// The tape echo's checks (make test-tape-echo), built for the Blackfin
// simulator and for the host:
//   kernels   the stereo and mono assembly run kernels against the C
//             reference on random runs (random tape with full-scale
//             samples, random block values, every write position, the
//             wrap of the reads, n 1..64), with guard words round the tape
//             and the block;
//   echo      a C instance and an assembly instance through scenarios
//             (pattern changes, time sweeps, feedback 1.1, hold, odd n,
//             every knob jumping, mono), more than one trip round the
//             tape: outputs and tape equal, guard words intact; the C
//             output's checksums, which the host build prints too;
//   response  an impulse: each head's echo at its delay, at unity;
//   glide     a sine's echo while TIME jumps: the largest third
//             difference against the steady one (a click would be far
//             above it), and the pitch the glide reaches;
//   cycles    per call (32 samples at 22050 Hz, 689 calls/s);
//   stability 60 s at feedback 1.1 and in hold: peak, RMS and DC per 10 s.
//
// Integer only: float on the Blackfin is soft-float.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "rings/harness/cycles.h"
#include "src/tape_echo/tape_echo.h"

using op1fx::TapeEcho;
using op1fx::TapeEchoKernel;
using op1fx::TapeEchoParams;

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
int32_t RandomPairValue() {
  const int32_t lo = RandomSample();
  const int32_t hi = RandomSample();
  return (lo & 0xffff) | static_cast<int32_t>(static_cast<uint32_t>(hi) << 16);
}

uint32_t checksum = 2166136261u;
void Hash(uint32_t x) {
  checksum ^= x;
  checksum *= 16777619u;
}

int failures = 0;

const int32_t kGuardWords = 32;
const int32_t kGuard = 0x5a5a5a5a;

int32_t Isqrt(int64_t x) {
  int64_t r = 0;
  int64_t bit = static_cast<int64_t>(1) << 40;
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

// --- Kernels ------------------------------------------------------------

int32_t tape_c[kGuardWords + TE_TAPE_PAIRS + kGuardWords];
int32_t tape_a[kGuardWords + TE_TAPE_PAIRS + kGuardWords];

struct GuardedKernel {
  int32_t before[8];
  TapeEchoKernel k;
  int32_t after[8];
};

bool Guarded(const int32_t* b, int32_t words) {
  for (int32_t i = 0; i < kGuardWords; ++i) {
    if (b[i] != kGuard || b[kGuardWords + words + i] != kGuard) return false;
  }
  return true;
}

void FillTape() {
  for (int32_t i = 0; i < kGuardWords; ++i) {
    tape_c[i] = tape_a[i] = kGuard;
    tape_c[kGuardWords + TE_TAPE_PAIRS + i] = tape_a[kGuardWords + TE_TAPE_PAIRS + i] = kGuard;
  }
  for (int32_t i = 0; i < TE_TAPE_PAIRS; ++i) {
    tape_c[kGuardWords + i] = tape_a[kGuardWords + i] = RandomPairValue();
  }
}

void TestKernel(bool mono) {
  int bad = 0;
  bool guards = true;
  for (int32_t trial = 0; trial < 3000; ++trial) {
    if (trial % 100 == 0) FillTape();
    GuardedKernel g;
    for (int32_t i = 0; i < 8; ++i) g.before[i] = g.after[i] = kGuard;
    TapeEchoKernel& k = g.k;
    const int32_t p = (trial % 7 == 0) ? Range(1, 64) : Range(1, TE_TAPE_SIZE);
    int32_t n = Range(1, 64);
    if (n > p) n = p;
    for (int32_t h = 0; h < 3; ++h) {
      // Delays in [n + 2, N - 3] samples over the run, steps up to +-0.8.
      k.step[h] = Range(-26000, 26000);
      if (Next() & 1) k.step[h] = 0;
      int32_t lo = (n + 2) << 15, hi = (TE_TAPE_SIZE - 3) << 15;
      if (k.step[h] > 0) hi -= k.step[h] * n;
      else lo -= k.step[h] * n;
      // Favour the reads' wrap and the mirror pair.
      if ((Next() & 3) == 0) {
        const int32_t near = TE_TAPE_SIZE - p - 1 + Range(-40, 40);
        k.pos[h] = (near << 15) | Range(0, 32767);
        if (k.pos[h] < lo) k.pos[h] = lo;
        if (k.pos[h] > hi) k.pos[h] = hi;
      } else {
        k.pos[h] = lo + static_cast<int32_t>(Next() % static_cast<uint32_t>(hi - lo));
      }
      k.w[h] = RandomPairValue();
      k.ws[h] = static_cast<int32_t>(Next());
    }
    k.gain = static_cast<int32_t>(Next());
    k.gstep = static_cast<int32_t>(Next()) >> Range(4, 20);
    k.hp_l = static_cast<int32_t>(Next());
    k.hp_r = static_cast<int32_t>(Next());
    k.lp = RandomPairValue();
    k.k = RandomPairValue();
    // The run as TapeEcho::Process makes it: cut where a read wraps.
    n = op1fx::TapeEchoPrepareRun(&k, p, n);
    int32_t xc[64], xa[64];
    for (int32_t i = 0; i < 64; ++i) xc[i] = xa[i] = RandomPairValue();
    TapeEchoKernel kc = k;
    int16_t in[64], wc[64], wa[64];
    for (int32_t i = 0; i < 64; ++i) in[i] = static_cast<int16_t>(xc[i]);
    if (mono) {
      op1fx::TapeEchoRunMonoC(&kc, in, wc, tape_c + kGuardWords, p, n);
    } else {
      op1fx::TapeEchoRunC(&kc, xc, tape_c + kGuardWords, p, n, false);
    }
#ifdef __bfin__
    if (mono) {
      op1fx::TapeEchoRunMonoAsm(&g.k, in, wa, tape_a + kGuardWords, tape_a + kGuardWords + p, n);
    } else {
      op1fx::TapeEchoRunAsm(&g.k, xa, tape_a + kGuardWords, tape_a + kGuardWords + p, n);
    }
    TapeEchoKernel& ka = g.k;
    bool ok = (mono ? memcmp(wc, wa, 2 * n) == 0 : memcmp(xc, xa, 4 * n) == 0) &&
              memcmp(tape_c, tape_a, sizeof(tape_c)) == 0;
    for (int32_t h = 0; h < 3; ++h) ok = ok && kc.pos[h] == ka.pos[h] && kc.w[h] == ka.w[h];
    ok = ok && kc.gain == ka.gain && kc.hp_l == ka.hp_l && kc.lp == ka.lp &&
         (mono || kc.hp_r == ka.hp_r);
    for (int32_t i = 0; i < 8; ++i) {
      if (g.before[i] != kGuard || g.after[i] != kGuard) guards = false;
    }
    if (!ok) {
      if (bad < 4) {
        printf("kernel %s: MISMATCH trial %ld n %ld p %ld\n", mono ? "mono" : "stereo",
               static_cast<long>(trial), static_cast<long>(n), static_cast<long>(p));
        for (int32_t i = 0; i < n && i < 3; ++i) {
          printf("  x[%ld] c %08lx asm %08lx\n", static_cast<long>(i),
                 static_cast<unsigned long>(xc[i]), static_cast<unsigned long>(xa[i]));
        }
        printf("  hp %08lx/%08lx %08lx/%08lx lp %08lx/%08lx gain %08lx/%08lx\n",
               static_cast<unsigned long>(kc.hp_l), static_cast<unsigned long>(ka.hp_l),
               static_cast<unsigned long>(kc.hp_r), static_cast<unsigned long>(ka.hp_r),
               static_cast<unsigned long>(kc.lp), static_cast<unsigned long>(ka.lp),
               static_cast<unsigned long>(kc.gain), static_cast<unsigned long>(ka.gain));
        for (int32_t i = 0; i < TE_TAPE_PAIRS; ++i) {
          if (tape_c[kGuardWords + i] != tape_a[kGuardWords + i]) {
            printf("  tape[%ld] c %08lx asm %08lx\n", static_cast<long>(i),
                   static_cast<unsigned long>(tape_c[kGuardWords + i]),
                   static_cast<unsigned long>(tape_a[kGuardWords + i]));
            break;
          }
        }
      }
      ++bad;
      memcpy(tape_a, tape_c, sizeof(tape_c));
    }
#else
    (void)xa;
    (void)wa;
#endif
    for (int32_t i = 0; i < n; ++i) Hash(mono ? static_cast<uint16_t>(wc[i]) : static_cast<uint32_t>(xc[i]));
  }
  guards = guards && Guarded(tape_c, TE_TAPE_PAIRS) && Guarded(tape_a, TE_TAPE_PAIRS);
#ifdef __bfin__
  printf("kernel %-6s 3000 runs: %s, guards %s\n", mono ? "mono" : "stereo",
         bad ? "MISMATCH" : "asm = C", guards ? "intact" : "HIT");
#endif
  if (bad || !guards) ++failures;
}

// --- Whole echo -----------------------------------------------------------

struct GuardedEcho {
  uint32_t before[16];
  TapeEcho echo;
  uint32_t after[16];
};
GuardedEcho echo_c, echo_a;
const uint32_t kGuardWord = 0xa5c3a5c3u;

void ArmGuards(GuardedEcho* g) {
  for (int32_t i = 0; i < 16; ++i) g->before[i] = g->after[i] = kGuardWord;
}
bool GuardsOk(const GuardedEcho& g) {
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

// A sine from a phase (Q15), parabolic plus a correction: close enough
// for a test signal.
int16_t SineOf(uint32_t phase, int32_t amp) {
  const int32_t x = static_cast<int32_t>(phase) >> 16;    // [-32768, 32767]
  const int32_t a = x < 0 ? -x : x;
  int32_t y = (x * (32768 - a)) >> 13;                     // 4 x (1 - |x|), Q15
  const int32_t ay = y < 0 ? -y : y;
  y = (y * 7 + ((y * ay) >> 15) * 2 + 4) >> 3;   // ~0.225 correction
  if (y > 32767) y = 32767;
  if (y < -32767) y = -32767;
  return static_cast<int16_t>((y * amp) >> 15);
}

TapeEchoParams DefaultParams() {
  TapeEchoParams p;
  p.time = 16384;
  p.feedback = 16384;
  p.age = 8192;
  p.heads = 0;
  p.hold = false;
  return p;
}

// FEEDBACK value for a loop gain in permille (0..1100).
int32_t FeedbackFor(int32_t permille) {
  int32_t v = (permille * 32767 + 550) / 1100;
  return v > 32767 ? 32767 : v;
}

void RunScenario(const char* name, int32_t scenario, int32_t calls, bool mono_in, bool mono_out) {
  ArmGuards(&echo_c);
  ArmGuards(&echo_a);
  echo_c.echo.Init();
  echo_a.echo.Init();
  echo_c.echo.set_use_asm(false);
#ifdef __bfin__
  echo_a.echo.set_use_asm(true);
#endif
  Source src;
  src.Init();
  checksum = 2166136261u;
  int32_t mismatch_call = -1;
  int32_t peak = 0;
  for (int32_t call = 0; call < calls; ++call) {
    TapeEchoParams p = DefaultParams();
    const int32_t n = (scenario == 3) ? 2 + 2 * (call % 32) : 32;
    int16_t l[64], r[64];
    for (int32_t i = 0; i < n; ++i) {
      int16_t s = 0;
      switch (scenario) {
        case 0:   // noise bursts, the pattern changes every 150 calls
          s = (call % 300) < 30 ? src.Noise(16000) : 0;
          p.heads = (call / 150) % op1fx::kTapeEchoNumPatterns;
          p.feedback = FeedbackFor(700);
          p.time = 6000;
          break;
        case 1:   // a saw, time sweeps up and down, age at the top
          s = src.Saw(0x00a3d70a, 12000);
          p.time = ((call / 200) % 2) ? 32767 : 2000;
          p.feedback = FeedbackFor(800);
          p.age = 32767;
          p.heads = 2;
          break;
        case 2:   // feedback 1.1 self-oscillating, then hold, then off
          s = call < 100 ? src.Noise(20000) : 0;
          p.feedback = FeedbackFor(1100);
          p.hold = call >= 800 && call < 1300;
          p.time = 3000;
          p.heads = 3;
          p.age = (call * 37) & 32767;
          break;
        case 4:   // feedback 1.1 while time sweeps, full-scale noise, R = -L
          s = src.Noise(32767);
          p.feedback = 32767;
          p.age = (call * 13) & 32767;
          p.time = (call / 900) % 2 ? 32767 : 0;
          p.heads = (call / 2000) % op1fx::kTapeEchoNumPatterns;
          break;
        default:  // full-scale noise, odd n, every knob jumping
          s = src.Noise(32767);
          p.time = (call * 977) & 32767;
          p.feedback = (call * 331) & 32767;
          p.age = (call * 1231) & 32767;
          p.heads = (call / 37) % op1fx::kTapeEchoNumPatterns;
          p.hold = (call / 90) % 5 == 4;
          break;
      }
      l[i] = s;
      r[i] = static_cast<int16_t>(scenario == 4 ? -s : -s / 2 + (s >> 3));
    }
    int16_t lc[64], rc[64], la[64], ra[64];
    echo_c.echo.Process(l, mono_in ? nullptr : r, lc, mono_out ? nullptr : rc, n, p);
    echo_a.echo.Process(l, mono_in ? nullptr : r, la, mono_out ? nullptr : ra, n, p);
    for (int32_t i = 0; i < n; ++i) {
      Hash(static_cast<uint16_t>(lc[i]));
      if (!mono_out) Hash(static_cast<uint16_t>(rc[i]));
      const int32_t a = lc[i] < 0 ? -lc[i] : lc[i];
      if (a > peak) peak = a;
      if (mismatch_call < 0 && (lc[i] != la[i] || (!mono_out && rc[i] != ra[i]))) mismatch_call = call;
    }
  }
  const bool tapes = memcmp(echo_c.echo.tape(), echo_a.echo.tape(), 4 * TE_TAPE_PAIRS) == 0;
  const bool guards = GuardsOk(echo_c) && GuardsOk(echo_a);
  printf("echo %-30s checksum %08lx peak %5ld", name, static_cast<unsigned long>(checksum),
         static_cast<long>(peak));
#ifdef __bfin__
  printf("  asm %s", mismatch_call < 0 && tapes ? "= C" : "DIFFERS");
  if (mismatch_call >= 0) printf(" (call %ld)", static_cast<long>(mismatch_call));
  if (mismatch_call >= 0 || !tapes) ++failures;
#else
  (void)tapes;
#endif
  printf("  guards %s\n", guards ? "intact" : "HIT");
  if (!guards) ++failures;
}

TapeEcho bench;

// --- Impulse response -----------------------------------------------------

void Response(int32_t pattern, int32_t time) {
  bench.Init();
  TapeEchoParams p = DefaultParams();
  p.time = time;
  p.feedback = 0;
  p.age = 0;
  p.heads = pattern;
  int32_t found = 0;
  int32_t at[3] = { 0, 0, 0 }, level[3] = { 0, 0, 0 };
  const int32_t calls = 1100;   // 1.6 s
  for (int32_t call = 0; call < calls; ++call) {
    int16_t l[32], wl[32], wr[32];
    for (int32_t i = 0; i < 32; ++i) l[i] = (call == 2 && i == 0) ? 16000 : 0;
    bench.Process(l, l, wl, wr, 32, p);
    for (int32_t i = 0; i < 32; ++i) {
      const int32_t a = wl[i] < 0 ? -wl[i] : wl[i];
      if (a > 2000 && found < 3) {
        const int32_t t = (call - 2) * 32 + i;
        if (found == 0 || t > at[found - 1] + 50) {
          at[found] = t;
          level[found] = wl[i];
          ++found;
        }
      }
      if (wl[i] != wr[i]) {
        printf("response: L != R\n");
        ++failures;
        return;
      }
    }
  }
  const int32_t d = bench.time_samples();
  printf("response pattern %ld (heads %lx) time %5ld samples:", static_cast<long>(pattern),
         static_cast<unsigned long>(op1fx::kTapeEchoPatternHeads[pattern]), static_cast<long>(d));
  for (int32_t j = 0; j < found; ++j) {
    printf("  echo at %5ld (%3ld/3 of time) level %5ld", static_cast<long>(at[j]),
           static_cast<long>((at[j] * 3 + d / 2) / d), static_cast<long>(level[j]));
  }
  printf("\n");
  // Every active head once, at its place, at the pattern's weight.
  const int32_t heads = op1fx::kTapeEchoPatternHeads[pattern];
  int32_t count = 0;
  for (int32_t h = 0; h < 3; ++h) {
    if (!((heads >> h) & 1)) continue;
    const int32_t want = (d * (h + 1)) / 3;
    bool ok = false;
    for (int32_t j = 0; j < found; ++j) ok = ok || (at[j] >= want - 1 && at[j] <= want + 1);
    if (!ok) {
      printf("response: head %ld missing at %ld\n", static_cast<long>(h + 1), static_cast<long>(want));
      ++failures;
    }
    ++count;
  }
  if (found != count) {
    printf("response: %ld echoes, %ld heads\n", static_cast<long>(found), static_cast<long>(count));
    ++failures;
  }
}

// --- Glide: a time jump bends, it doesn't click ---------------------------

void Glide(const char* name, int32_t from, int32_t to, int32_t age) {
  bench.Init();
  TapeEchoParams p = DefaultParams();
  p.time = from;
  p.feedback = 0;   // one pass: a pitch r gives r^3 x the third difference
  p.age = age;
  p.heads = 0;
  uint32_t phase = 0;
  const uint32_t inc = 85700000u;   // ~440 Hz
  int32_t prev[3] = { 0, 0, 0 };
  int32_t steady = 0, moving = 0, vmax = 0;
  for (int32_t call = 0; call < 6000; ++call) {
    if (call == 3000) p.time = to;
    int16_t l[32], wl[32];
    for (int32_t i = 0; i < 32; ++i) {
      l[i] = SineOf(phase, 12000);
      phase += inc;
    }
    bench.Process(l, nullptr, wl, nullptr, 32, p);
    const int32_t v = bench.velocity() < 0 ? -bench.velocity() : bench.velocity();
    if (v > vmax) vmax = v;
    for (int32_t i = 0; i < 32; ++i) {
      const int32_t d3 = wl[i] - 3 * prev[0] + 3 * prev[1] - prev[2];
      prev[2] = prev[1];
      prev[1] = prev[0];
      prev[0] = wl[i];
      const int32_t a = d3 < 0 ? -d3 : d3;
      if (call >= 2000 && call < 3000 && a > steady) steady = a;
      if (call >= 3000 && a > moving) moving = a;
    }
  }
  // A sine at pitch r has r^3 x the third difference.
  printf("glide %-24s third diff steady %5ld, while gliding %5ld (x %ld.%02ld); top speed %ld.%02ld sample/sample\n",
         name, static_cast<long>(steady), static_cast<long>(moving),
         static_cast<long>(moving / (steady ? steady : 1)),
         static_cast<long>((moving * 100 / (steady ? steady : 1)) % 100),
         static_cast<long>(vmax >> 15), static_cast<long>(((vmax & 32767) * 100) >> 15));
  // 1.75^3 = 5.4: allow the wow and some margin, far below a click.
  if (moving > 7 * steady) {
    printf("glide: a click?\n");
    ++failures;
  }
}

// --- Cycles ----------------------------------------------------------------

void Cycles(const char* name, bool use_asm, bool mono, int32_t pattern, int32_t scenario) {
  bench.Init();
  bench.set_use_asm(use_asm);
  Source src;
  src.Init();
  TapeEchoParams p = DefaultParams();
  p.heads = pattern;
  p.age = 32767;
  p.feedback = FeedbackFor(800);
  harness::CycleStats stats;
  const int32_t calls = 1600;
  for (int32_t call = 0; call < calls; ++call) {
    if (scenario == 1) {   // everything moving: time sweeps, pattern changes, hold
      p.time = (call / 100) % 2 ? 30000 : 3000;
      p.heads = (call / 50) % op1fx::kTapeEchoNumPatterns;
      p.feedback = (call * 97) & 32767;
      p.age = (call * 53) & 32767;
      p.hold = (call / 70) % 4 == 3;
    }
    int16_t l[32], r[32], wl[32], wr[32];
    for (int32_t i = 0; i < 32; ++i) {
      l[i] = src.Noise(12000);
      r[i] = src.Noise(12000);
    }
    const uint64_t t0 = harness::ReadCycles();
    bench.Process(l, mono ? nullptr : r, wl, mono ? nullptr : wr, 32, p);
    const uint64_t t1 = harness::ReadCycles();
    if (call >= 100) stats.Add(t1 - t0);
  }
#ifdef __bfin__
  const unsigned long avg = static_cast<unsigned long>(stats.total / stats.n);
  const unsigned long khz_avg = static_cast<unsigned long>(stats.total * 689 / stats.n / 1000);
  const unsigned long khz_max = static_cast<unsigned long>(stats.max * 689 / 1000);
  printf("cycles %-30s per call avg %6lu min %6lu max %6lu  per sample %4lu  => %lu.%02lu MHz avg, %lu.%02lu MHz peak\n",
         name, avg, static_cast<unsigned long>(stats.min), static_cast<unsigned long>(stats.max),
         avg / 32, khz_avg / 1000, (khz_avg % 1000) / 10, khz_max / 1000, (khz_max % 1000) / 10);
#else
  (void)name;
#endif
}

// --- Stability ---------------------------------------------------------------

void Stability(const char* name, int32_t scenario) {
  bench.Init();
#ifdef __bfin__
  bench.set_use_asm(true);
#endif
  Source src;
  src.Init();
  TapeEchoParams p = DefaultParams();
  const int32_t calls_per_10s = 6891;
  checksum = 2166136261u;
  printf("stability %s:\n", name);
  for (int32_t seg = 0; seg < 6; ++seg) {
    int32_t peak = 0;
    int64_t sum = 0, sum2 = 0;
    for (int32_t call = 0; call < calls_per_10s; ++call) {
      const int32_t t = seg * calls_per_10s + call;
      int16_t l[32], r[32], wl[32], wr[32];
      for (int32_t i = 0; i < 32; ++i) {
        int16_t s = 0;
        switch (scenario) {
          case 0:   // feedback 1.1 after a short burst, 3 heads, worn
            s = t < 100 ? src.Noise(16384) : 0;
            p.feedback = 32767;
            p.age = 32767;
            p.heads = 2;
            p.time = 12000;
            break;
          case 1:   // feedback 1.1, new tape, one head, a held -6 dBFS saw
            s = src.Saw(0x00a3d70a, 16384);
            p.feedback = 32767;
            p.age = 0;
            p.time = 20000;
            break;
          case 2:   // hold after 2 s of noise, mid age
            s = src.Noise(16384);
            p.hold = t >= 1378;
            p.age = 16384;
            p.feedback = FeedbackFor(600);
            p.time = 26000;
            p.heads = 1;
            break;
          default:  // feedback 1.1 while time sweeps all the way, full-scale noise
            s = src.Noise(32767);
            p.feedback = 32767;
            p.age = (t * 13) & 32767;
            p.time = (t / 900) % 2 ? 32767 : 0;
            p.heads = (t / 2000) % op1fx::kTapeEchoNumPatterns;
            break;
        }
        l[i] = s;
        r[i] = static_cast<int16_t>(-s);
      }
      bench.Process(l, r, wl, wr, 32, p);
      for (int32_t i = 0; i < 32; ++i) {
        const int32_t a = wl[i] < 0 ? -wl[i] : wl[i];
        const int32_t b = wr[i] < 0 ? -wr[i] : wr[i];
        if (a > peak) peak = a;
        if (b > peak) peak = b;
        sum += 2 * wl[i];   // DC of the left channel (R carries -L)
        sum2 += static_cast<int64_t>(wl[i]) * wl[i] + static_cast<int64_t>(wr[i]) * wr[i];
        Hash(static_cast<uint16_t>(wl[i]));
      }
    }
    const int64_t count = 2 * 32 * static_cast<int64_t>(calls_per_10s);
    const int32_t dc = static_cast<int32_t>(sum / count);
    const int32_t rms = Isqrt(sum2 / count);
    int64_t tape_sum = 0;
    const int32_t* tb = bench.tape();
    for (int32_t i = 0; i < TE_TAPE_SIZE; ++i) tape_sum += static_cast<int16_t>(tb[i]);
    printf("  %2ld-%2ld s: wet peak %5ld rms %5ld dc %4ld  tape mean (L) %4ld\n",
           static_cast<long>(seg * 10), static_cast<long>(seg * 10 + 10), static_cast<long>(peak),
           static_cast<long>(rms), static_cast<long>(dc),
           static_cast<long>(tape_sum / TE_TAPE_SIZE));
    if (scenario == 0 && seg == 5 && rms < 1000) {
      printf("stability: feedback 1.1 doesn't self-oscillate\n");
      ++failures;
    }
  }
  printf("stability %s checksum %08lx\n", name, static_cast<unsigned long>(checksum));
}

}  // namespace

int main() {
  harness::EnableCycleCounter();
  TestKernel(false);
  TestKernel(true);
  printf("kernels checksum %08lx\n", static_cast<unsigned long>(checksum));

  RunScenario("noise bursts, pattern steps", 0, 2500, false, false);
  RunScenario("saw, time sweeps, age max", 1, 2500, false, false);
  RunScenario("fb 1.1, hold, age moving", 2, 2000, false, false);
  RunScenario("full-scale, odd n, all knobs", 3, 2500, false, false);
  RunScenario("mono in, mono out", 1, 2000, true, true);
  RunScenario("mono in, stereo out", 0, 1500, true, false);
  RunScenario("stereo in, mono out", 3, 1500, false, true);
  RunScenario("fb 1.1, time sweeps, R = -L", 4, 8000, false, false);

  for (int32_t pat = 0; pat < op1fx::kTapeEchoNumPatterns; ++pat) Response(pat, 16384);
  Response(2, 0);
  Response(0, 32767);

  Glide("30% -> 70%, new tape", 9830, 22937, 0);
  Glide("70% -> 30%, new tape", 22937, 9830, 0);
  Glide("0 -> 100%, worn", 0, 32767, 32767);
  Glide("100% -> 0, worn", 32767, 0, 32767);

#ifdef __bfin__
  Cycles("stereo, 3 heads, age max", true, false, 2, 0);
  Cycles("stereo, 1 head", true, false, 0, 0);
  Cycles("stereo, everything moving", true, false, 2, 1);
  Cycles("mono, 3 heads, age max", true, true, 2, 0);
  Cycles("mono, everything moving", true, true, 2, 1);
  Cycles("C stereo, 3 heads", false, false, 2, 0);
  Cycles("C mono, 3 heads", false, true, 2, 0);
#endif

  Stability("fb 1.1 after a burst, 3 heads, worn", 0);
  Stability("fb 1.1, new tape, held saw", 1);
  Stability("hold after 2 s of noise", 2);
  Stability("fb 1.1, time sweeping, full-scale", 3);

  printf("%s: %d failure(s)\n", failures ? "FAIL" : "ok", failures);
  return failures ? 1 : 0;
}
