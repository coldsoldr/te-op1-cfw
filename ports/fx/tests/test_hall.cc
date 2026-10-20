// The hall's checks (make test-hall), built for the Blackfin simulator and
// for the host:
//   kernels   each assembly run kernel against its C reference on random
//             runs (random buffers with full-scale samples, random
//             coefficients, every size, the mirrored region), sample for
//             sample, with guard words round the buffers;
//   reverb    a C instance and an assembly instance through scenarios
//             (size changes, freeze, infinite decay, motion, pre-delay
//             glides, width and early moving, odd n, mono), more than one
//             trip round the buffer: outputs and buffers equal, guard
//             words round the objects intact; the C output's checksums,
//             which the host build prints too;
//   cycles    per call (32 samples at 22050 Hz, 689 calls/s): stereo and
//             mono, motion on and off, during size changes, and C;
//   stability 60 s at the longest finite decay (noise in), frozen after a
//             noise burst (motion 0 and at the top), infinite decay with a
//             held saw: peak, RMS and DC per 10 s.
//
// Integer only: float on the Blackfin is soft-float.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "rings/harness/cycles.h"
#include "src/hall/hall.h"

using op1fx::HallKernel;
using op1fx::HallParams;
using op1fx::HallReverb;

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

uint32_t checksum = 2166136261u;
void Hash(uint32_t x) {
  checksum ^= x;
  checksum *= 16777619u;
}

int failures = 0;

const int32_t kGuardWords = 32;
const int16_t kGuard = 0x5a5a;

// --- Kernels ------------------------------------------------------------

int16_t buf_c[kGuardWords + HL_WORDS + kGuardWords];
int16_t buf_a[kGuardWords + HL_WORDS + kGuardWords];

bool Guarded(const int16_t* b, int32_t words) {
  for (int32_t i = 0; i < kGuardWords; ++i) {
    if (b[i] != kGuard || b[kGuardWords + words + i] != kGuard) return false;
  }
  return true;
}

void FillBuffer(int16_t* c, int16_t* a, int32_t words) {
  for (int32_t i = 0; i < kGuardWords; ++i) {
    c[i] = a[i] = c[kGuardWords + words + i] = a[kGuardWords + words + i] = kGuard;
  }
  for (int32_t i = 0; i < words; ++i) c[kGuardWords + i] = a[kGuardWords + i] = RandomSample();
}

// A run as HallReverb makes them: p in [1, SIZE], not crossing the wrap or
// the start of the mirroring.
void RandomRun(int32_t* ptr, int32_t* n, int32_t* alias_bytes) {
  *ptr = Range(1, HL_SIZE);
  const int32_t next = *ptr - 1;
  const bool alias = next < HL_MAX_LINE;
  int32_t m = next + 1;
  if (!alias) m = next - HL_MAX_LINE + 1;
  if (m > 64) m = 64;
  *n = Range(1, m);
  *alias_bytes = alias ? 2 * HL_SIZE : 0;
}

int32_t RandomCoef() {
  const uint32_t r = Next();
  if ((r & 7) == 0) return (r & 8) ? 32767 : -32768;
  return static_cast<int16_t>(r >> 16);
}
int32_t PackCoefs(int32_t lo, int32_t hi) {
  return (lo & 0xffff) | static_cast<int32_t>(static_cast<uint32_t>(hi) << 16);
}
// Two random coefficients, packed (sequenced: argument order is unspecified).
int32_t RandomPair() {
  const int32_t lo = RandomCoef();
  const int32_t hi = RandomCoef();
  return PackCoefs(lo, hi);
}

const int32_t kLineBase[8] = { HL_L0, HL_L1, HL_L2, HL_L3, HL_L4, HL_L5, HL_L6, HL_L7 };

void Offsets(int32_t size, int32_t* off, int32_t* eroff) {
  const op1fx::HallSizeTable& t = op1fx::kHallSizes[size];
  for (int32_t s = 0; s < 8; ++s) {
    const int32_t line = op1fx::kHallSlotLine[s];
    off[s] = 2 * (kLineBase[line] + t.len[line]);
  }
  for (int32_t j = 0; j < 5; ++j) {
    eroff[j] = 2 * (HL_ER + t.er_l[j]);
    eroff[5 + j] = 2 * (HL_ER + t.er_r[j]);
  }
}

// A random kernel block: random sizes, positions within +-16 samples (the
// steps keep them within 17 over a run), random coefficients and states.
void RandomKernel(HallKernel* k) {
  Offsets(Range(0, 15), k->off, k->eroff);
  Offsets(Range(0, 15), k->xoff, k->xeroff);
  for (int32_t i = 0; i < 4; ++i) {
    k->z[i] = RandomPair();
    k->ca[i] = RandomPair();
    k->cb[i] = RandomPair();
  }
  k->posa = Range(-(16 << 16), 16 << 16);
  k->posb = Range(-(16 << 16), 16 << 16);
  k->stepa = static_cast<int32_t>(Next()) >> 21;   // <= 1/64 sample per sample
  k->stepb = static_cast<int32_t>(Next()) >> 21;
  if (Next() % 5 == 0) k->stepa = k->stepb = 0;
  k->gain = Range(0, 0x3fffffff);
  k->gstep = static_cast<int32_t>(Next()) >> 12;
  k->dg = RandomPair();
  k->c01 = RandomPair();
  k->c23 = RandomPair();
  k->glc4 = RandomPair();
  k->width = RandomPair();
  k->xf = Range(0, 0x7fff0000 - (64 << 24));
  k->xfstep = Range(0, 1 << 24);
  k->e = k->o = 0;
  k->rng = Next() | 1;
  k->dk = (Next() & 1) ? RandomPair() : PackCoefs(1, 1);
}

bool SameKernel(const HallKernel& a, const HallKernel& b) {
  return memcmp(a.z, b.z, sizeof(a.z)) == 0 && a.posa == b.posa && a.posb == b.posb &&
         a.gain == b.gain && a.xf == b.xf && a.rng == b.rng;
}

// kind 0 stereo, 1 mono, 2 crossfade.
void TestKernel(int kind, int32_t trials) {
  static const char* const kNames[3] = { "run kernel", "run kernel mono", "xfade kernel" };
  int bad = 0;
  for (int32_t trial = 0; trial < trials; ++trial) {
    if (trial % 50 == 0) FillBuffer(buf_c, buf_a, HL_WORDS);
    HallKernel k;
    RandomKernel(&k);
    int32_t ptr, n, alias;
    RandomRun(&ptr, &n, &alias);
    if (k.gstep < 0 && k.gain + k.gstep * n < 0) k.gstep = -k.gstep;
    int16_t x[64], lc[64], rc[64], la[64], ra[64];
    for (int32_t i = 0; i < 64; ++i) x[i] = RandomSample();
    HallKernel kc = k, ka = k;
    (void)ka;
    if (kind == 2) {
      op1fx::HallXfadeRunC(&kc, x, lc, rc, buf_c + kGuardWords + ptr, n, alias);
    } else {
      op1fx::HallRunC(&kc, x, lc, kind == 1 ? nullptr : rc, buf_c + kGuardWords + ptr, n, alias);
    }
    const bool mono = kind == 1;
#ifdef __bfin__
    if (kind == 2) {
      op1fx::HallXfadeRunAsm(&ka, x, la, ra, buf_a + kGuardWords + ptr, n, alias);
    } else if (kind == 1) {
      // Outside the mirror, the no-mirror kernel (mostly) or the other.
      if (alias == 0 && (trial & 3) != 0) {
        op1fx::HallRunMonoNmAsm(&ka, x, la, buf_a + kGuardWords + ptr, n, 0);
      } else {
        op1fx::HallRunMonoAsm(&ka, x, la, buf_a + kGuardWords + ptr, n, alias);
      }
    } else if (alias == 0 && (trial & 3) != 0) {
      op1fx::HallRunNmAsm(&ka, x, la, ra, buf_a + kGuardWords + ptr, n, 0);
    } else {
      op1fx::HallRunAsm(&ka, x, la, ra, buf_a + kGuardWords + ptr, n, alias);
    }
    const bool ok = SameKernel(kc, ka) && memcmp(lc, la, 2 * n) == 0 &&
                    (mono || memcmp(rc, ra, 2 * n) == 0) && memcmp(buf_c, buf_a, sizeof(buf_c)) == 0;
    if (!ok) {
      if (bad < 4) {
        printf("%s: MISMATCH trial %ld n %ld ptr %ld\n", kNames[kind], static_cast<long>(trial),
               static_cast<long>(n), static_cast<long>(ptr));
        for (int32_t i = 0; i < n && i < 4; ++i) {
          printf("  [%ld] c %d %d asm %d %d\n", static_cast<long>(i), lc[i], mono ? 0 : rc[i], la[i],
                 mono ? 0 : ra[i]);
        }
        for (int32_t i = 0; i < 4; ++i) {
          printf("  z[%ld] %08lx %08lx\n", static_cast<long>(i), static_cast<unsigned long>(kc.z[i]),
                 static_cast<unsigned long>(ka.z[i]));
        }
        printf("  pos %08lx %08lx / %08lx %08lx gain %08lx %08lx xf %08lx %08lx\n",
               static_cast<unsigned long>(kc.posa), static_cast<unsigned long>(ka.posa),
               static_cast<unsigned long>(kc.posb), static_cast<unsigned long>(ka.posb),
               static_cast<unsigned long>(kc.gain), static_cast<unsigned long>(ka.gain),
               static_cast<unsigned long>(kc.xf), static_cast<unsigned long>(ka.xf));
        int32_t shown = 0;
        for (int32_t i = 0; i < HL_WORDS + 2 * kGuardWords && shown < 12; ++i) {
          if (buf_c[i] != buf_a[i]) {
            printf("  buffer difference at word %ld from p (c %d asm %d)\n",
                   static_cast<long>(i - kGuardWords - ptr), buf_c[i], buf_a[i]);
            ++shown;
          }
        }
      }
      ++bad;
      memcpy(buf_a, buf_c, sizeof(buf_c));
    }
#endif
    for (int32_t i = 0; i < n; ++i) {
      Hash(static_cast<uint16_t>(lc[i]));
      if (!mono) Hash(static_cast<uint16_t>(rc[i]));
    }
    for (int32_t i = 0; i < 4; ++i) Hash(static_cast<uint32_t>(kc.z[i]));
  }
  uint32_t h = 0;
  for (int32_t i = 0; i < HL_WORDS; ++i) h = h * 31 + static_cast<uint16_t>(buf_c[kGuardWords + i]);
  Hash(h);
  const bool guards = Guarded(buf_c, HL_WORDS) && Guarded(buf_a, HL_WORDS);
#ifdef __bfin__
  printf("%s: %s (%ld runs, asm = C), guards %s\n", kNames[kind], bad ? "FAIL" : "ok",
         static_cast<long>(trials), guards ? "intact" : "HIT");
#else
  printf("%s: C only, guards %s\n", kNames[kind], guards ? "intact" : "HIT");
#endif
  if (bad || !guards) ++failures;
}

int16_t pre_c[HL_PRE_WORDS + kGuardWords], pre_a[HL_PRE_WORDS + kGuardWords];

void TestPreKernel() {
  int bad = 0;
  for (int32_t i = 0; i < HL_PRE_WORDS + kGuardWords; ++i) {
    pre_c[i] = pre_a[i] = i < HL_PRE_WORDS ? RandomSample() : kGuard;
  }
  for (int trial = 0; trial < 1000; ++trial) {
    const int32_t n = Range(1, 64);
    const int32_t w = Range(0, HL_PRE_WORDS - n);
    int32_t d = (Range(0, HL_PRE_MAX - 4) << 16) | Range(0, 0xffff);
    int32_t step = static_cast<int32_t>(Next()) >> 17;
    if (trial % 4 == 0) {
      d &= ~0xffff;
      step = 0;
    }
    if (d + step * n < 0) step = -step;
    int16_t l[64], r[64], xc[64], xa[64];
    for (int32_t i = 0; i < 64; ++i) {
      l[i] = RandomSample();
      r[i] = RandomSample();
    }
    const int16_t* rr = (trial & 1) ? l : r;
    op1fx::HallPreRunC(l, rr, pre_c, xc, w, d, step, n);
#ifdef __bfin__
    op1fx::HallPreRunAsm(l, rr, pre_a, xa, w, d, step, n);
    if (memcmp(xc, xa, 2 * n) != 0 || memcmp(pre_c, pre_a, sizeof(pre_c)) != 0) {
      if (bad < 4) printf("pre kernel: MISMATCH trial %d n %ld w %ld\n", trial, static_cast<long>(n),
                          static_cast<long>(w));
      ++bad;
      memcpy(pre_a, pre_c, sizeof(pre_c));
    }
#endif
    for (int32_t i = 0; i < n; ++i) Hash(static_cast<uint16_t>(xc[i]));
  }
  bool guards = true;
  for (int32_t i = HL_PRE_WORDS; i < HL_PRE_WORDS + kGuardWords; ++i) {
    if (pre_c[i] != kGuard || pre_a[i] != kGuard) guards = false;
  }
#ifdef __bfin__
  printf("pre kernel: %s (1000 runs, asm = C), guards %s\n", bad ? "FAIL" : "ok", guards ? "intact" : "HIT");
#else
  printf("pre kernel: C only, guards %s\n", guards ? "intact" : "HIT");
#endif
  if (bad || !guards) ++failures;
}

// --- Whole reverb ---------------------------------------------------------

struct GuardedHall {
  uint32_t before[16];
  HallReverb hall;
  uint32_t after[16];
};
GuardedHall hall_c, hall_a;
const uint32_t kGuardWord = 0xa5c3a5c3u;

void ArmGuards(GuardedHall* g) {
  for (int32_t i = 0; i < 16; ++i) g->before[i] = g->after[i] = kGuardWord;
}
bool GuardsOk(const GuardedHall& g) {
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

HallParams DefaultParams() {
  HallParams p;
  p.size = 24576;
  p.decay = 16384;
  p.damp = 16384;
  p.predelay = 0;
  p.early = 16384;
  p.motion = 16384;
  p.width = 32767;
  p.freeze = false;
  return p;
}

void RunScenario(const char* name, int32_t scenario, int32_t calls, bool mono_in, bool mono_out) {
  ArmGuards(&hall_c);
  ArmGuards(&hall_a);
  hall_c.hall.Init();
  hall_a.hall.Init();
  hall_c.hall.set_use_asm(false);
#ifdef __bfin__
  hall_a.hall.set_use_asm(true);
#endif
  Source src;
  src.Init();
  checksum = 2166136261u;
  int32_t mismatch_call = -1;
  int32_t peak = 0;
  for (int32_t call = 0; call < calls; ++call) {
    HallParams p = DefaultParams();
    const int32_t n = (scenario == 3) ? 2 + 2 * (call % 32) : 32;
    int16_t l[64], r[64];
    for (int32_t i = 0; i < n; ++i) {
      int16_t s = 0;
      switch (scenario) {
        case 0:   // noise bursts, size steps every 150 calls
          s = (call % 300) < 40 ? src.Noise(16000) : 0;
          p.size = 32767 - ((call / 150) % 16) * 2048 - 1000;
          p.decay = 26000;
          break;
        case 1:   // a saw, long decay, motion at the top, pre-delay glides, width and early move
          s = src.Saw(0x00a3d70a, 12000);
          p.decay = 30000;
          p.motion = 32767;
          p.predelay = (call / 300) % 2 ? 32767 : 4000;
          p.width = (call * 97) & 32767;
          p.early = (call * 53) & 32767;
          break;
        case 2:   // noise then freeze, damp moving; infinite decay after
          s = call < 300 ? src.Noise(20000) : src.Noise(4000);
          p.freeze = call >= 300 && call < 1000;
          p.damp = (call * 41) & 32767;
          p.decay = call >= 1000 ? 32767 : 30000;
          break;
        default:  // full-scale noise, odd block sizes, every knob jumping
          s = src.Noise(32767);
          p.decay = (call * 977) & 32767;
          p.damp = (call * 331) & 32767;
          p.predelay = (call * 1231) & 32767;
          p.size = (call / 23) * 5003 & 32767;
          p.motion = (call * 2111) & 32767;
          p.early = (call * 613) & 32767;
          p.width = (call * 1709) & 32767;
          p.freeze = (call / 90) % 5 == 4;
          break;
      }
      l[i] = s;
      r[i] = static_cast<int16_t>(scenario == 1 ? s : -s / 2 + (s >> 3));
    }
    int16_t lc[64], rc[64], la[64], ra[64];
    hall_c.hall.Process(l, mono_in ? nullptr : r, lc, mono_out ? nullptr : rc, n, p);
    hall_a.hall.Process(l, mono_in ? nullptr : r, la, mono_out ? nullptr : ra, n, p);
    for (int32_t i = 0; i < n; ++i) {
      Hash(static_cast<uint16_t>(lc[i]));
      if (!mono_out) Hash(static_cast<uint16_t>(rc[i]));
      const int32_t a = lc[i] < 0 ? -lc[i] : lc[i];
      if (a > peak) peak = a;
      if (mismatch_call < 0 && (lc[i] != la[i] || (!mono_out && rc[i] != ra[i]))) mismatch_call = call;
    }
  }
  const bool buffers = memcmp(hall_c.hall.buffer(), hall_a.hall.buffer(), 2 * HL_WORDS) == 0;
  const bool guards = GuardsOk(hall_c) && GuardsOk(hall_a);
  printf("reverb %-30s checksum %08lx peak %5ld", name, static_cast<unsigned long>(checksum),
         static_cast<long>(peak));
#ifdef __bfin__
  printf("  asm %s", mismatch_call < 0 && buffers ? "= C" : "DIFFERS");
  if (mismatch_call >= 0) printf(" (call %ld)", static_cast<long>(mismatch_call));
  if (mismatch_call >= 0 || !buffers) ++failures;
#endif
  printf("  guards %s\n", guards ? "intact" : "HIT");
  if (!guards) ++failures;
}

// --- Cycles ----------------------------------------------------------------

HallReverb bench;

// sizes: true steps the size every 40 calls (each step's crossfade is 8
// calls); the max is then a crossfade call's.
void Cycles(const char* name, bool use_asm, bool mono, int32_t motion, bool sizes) {
  bench.Init();
  bench.set_use_asm(use_asm);
  Source src;
  src.Init();
  HallParams p = DefaultParams();
  p.decay = 28000;
  p.motion = motion;
  p.predelay = 8000;
  harness::CycleStats stats, xstats;
  const int32_t calls = 1400;   // > one trip round the buffer (18986 samples = 594 calls)
  for (int32_t call = 0; call < calls; ++call) {
    if (sizes) p.size = ((call / 40) % 2) ? 8192 : 30000;
    int16_t l[32], r[32], wl[32], wr[32];
    for (int32_t i = 0; i < 32; ++i) {
      l[i] = src.Noise(12000);
      r[i] = src.Noise(12000);
    }
    const uint64_t t0 = harness::ReadCycles();
    bench.Process(l, mono ? nullptr : r, wl, mono ? nullptr : wr, 32, p);
    const uint64_t t1 = harness::ReadCycles();
    if (call >= 100) {
      if (bench.coefs().xfade) {
        xstats.Add(t1 - t0);
      } else {
        stats.Add(t1 - t0);
      }
    }
  }
#ifdef __bfin__
  const harness::CycleStats& s = sizes ? xstats : stats;
  const unsigned long avg = static_cast<unsigned long>(s.total / s.n);
  const unsigned long khz_avg = static_cast<unsigned long>(s.total * 689 / s.n / 1000);
  const unsigned long khz_max = static_cast<unsigned long>(s.max * 689 / 1000);
  printf("cycles %-28s per call avg %5lu min %5lu max %5lu  per sample %3lu  => %lu.%02lu MHz avg, %lu.%02lu MHz peak\n",
         name, avg, static_cast<unsigned long>(s.min), static_cast<unsigned long>(s.max), avg / 32,
         khz_avg / 1000, (khz_avg % 1000) / 10, khz_max / 1000, (khz_max % 1000) / 10);
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
  HallParams p = DefaultParams();
  p.decay = op1fx::kHallInfinite - 1;
  p.damp = 0;
  p.size = 32767;
  p.motion = scenario == 3 ? 0 : 32767;
  if (scenario == 2) p.decay = 32767;
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
        if (scenario == 0) s = src.Noise(16384);                          // -6 dBFS noise
        if (scenario == 1 || scenario == 3) s = src.Noise(16384);         // gated by freeze
        if (scenario == 2) s = t < 689 ? src.Saw(0x00a3d70a, 8192) : 0;  // 1 s of saw, then silence
        l[i] = r[i] = s;
      }
      p.freeze = (scenario == 1 || scenario == 3) && t >= 1378;          // freeze after 2 s
      bench.Process(l, r, wl, wr, 32, p);
      for (int32_t i = 0; i < 32; ++i) {
        const int32_t a = wl[i] < 0 ? -wl[i] : wl[i];
        const int32_t b = wr[i] < 0 ? -wr[i] : wr[i];
        if (a > peak) peak = a;
        if (b > peak) peak = b;
        sum += wl[i] + wr[i];
        sum2 += static_cast<int64_t>(wl[i]) * wl[i] + static_cast<int64_t>(wr[i]) * wr[i];
        Hash(static_cast<uint16_t>(wl[i]));
      }
    }
    const int64_t count = 2 * 32 * static_cast<int64_t>(calls_per_10s);
    const int32_t dc = static_cast<int32_t>(sum / count);
    const int32_t ms = static_cast<int32_t>(sum2 / count);
    int32_t rms = 0;
    while ((rms + 1) * (rms + 1) <= ms) ++rms;
    int64_t buf_sum = 0;
    const int16_t* b = bench.buffer();
    for (int32_t i = HL_L0; i < HL_REACH; ++i) buf_sum += b[i];
    printf("  %2ld-%2ld s: wet peak %5ld rms %5ld dc %4ld  lines' mean %4ld\n",
           static_cast<long>(seg * 10), static_cast<long>(seg * 10 + 10), static_cast<long>(peak),
           static_cast<long>(rms), static_cast<long>(dc),
           static_cast<long>(buf_sum / (HL_REACH - HL_L0)));
  }
  printf("stability %s checksum %08lx\n", name, static_cast<unsigned long>(checksum));
}

}  // namespace

int main() {
  harness::EnableCycleCounter();
  printf("sizeof(HallReverb) %lu bytes\n", static_cast<unsigned long>(sizeof(HallReverb)));
  TestKernel(0, 1500);
  TestKernel(1, 1500);
  TestKernel(2, 1000);
  TestPreKernel();
  printf("kernels checksum %08lx\n", static_cast<unsigned long>(checksum));

  RunScenario("noise bursts, size steps", 0, 2600, false, false);
  RunScenario("saw, motion, pre, width, early", 1, 1500, false, false);
  RunScenario("freeze, damp moves, infinite", 2, 1500, false, false);
  RunScenario("full-scale, odd n, all knobs", 3, 1500, false, false);
  RunScenario("mono in, mono out", 0, 1300, true, true);
  RunScenario("mono in, stereo out", 1, 1200, true, false);
  RunScenario("odd n, all knobs, mono out", 3, 1200, false, true);

#ifdef __bfin__
  Cycles("stereo, motion on", true, false, 16384, false);
  Cycles("stereo, motion off", true, false, 0, false);
  Cycles("mono, motion on", true, true, 16384, false);
  Cycles("mono, motion off", true, true, 0, false);
  Cycles("stereo, size crossfade calls", true, false, 16384, true);
  Cycles("mono, size crossfade calls", true, true, 16384, true);
  Cycles("C stereo, motion on", false, false, 16384, false);
  Cycles("C mono, motion on", false, true, 16384, false);
#endif

  Stability("decay 20 s, noise held", 0);
  Stability("freeze after 2 s, motion max", 1);
  Stability("freeze after 2 s, motion 0", 3);
  Stability("infinite decay, 1 s of saw", 2);

  printf("%s: %d failure(s)\n", failures ? "FAIL" : "ok", failures);
  return failures ? 1 : 0;
}
