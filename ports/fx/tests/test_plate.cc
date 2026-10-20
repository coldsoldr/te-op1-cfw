// The plate's checks (make test-plate), built for the Blackfin simulator
// and for the host:
//   kernels   each assembly run kernel against its C reference on random
//             runs (random buffers with full-scale samples, random
//             coefficients, every size, the mirrored region), sample for
//             sample, with guard words round the buffers;
//   reverb    a C instance and an assembly instance through scenarios
//             (size changes, freeze, motion, pre-delay glides, mono), more
//             than one trip round the tank buffer: outputs and buffers
//             equal, guard words round the objects intact; the C output's
//             checksums, which the host build prints too;
//   cycles    per call (32 samples at 22050 Hz, 689 calls/s): stereo and
//             mono, motion on and off, assembly and C;
//   stability 60 s at the largest decay (noise in), and frozen after a
//             noise burst: peak, RMS and DC per 10 s.
//
// Integer only: float on the Blackfin is soft-float.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "rings/harness/cycles.h"
#include "src/plate/plate.h"

using op1fx::PlateParams;
using op1fx::PlateReverb;

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

int16_t in_c[kGuardWords + PL_IN_WORDS + kGuardWords];
int16_t in_a[kGuardWords + PL_IN_WORDS + kGuardWords];
int16_t tank_c[kGuardWords + PL_TANK_WORDS + kGuardWords];
int16_t tank_a[kGuardWords + PL_TANK_WORDS + kGuardWords];

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

// A run as PlateReverb makes them: p in [0, size), not crossing the wrap or
// the start of the mirroring.
void RandomRun(int32_t size, int32_t max_line, int32_t* ptr, int32_t* n, int32_t* alias_bytes) {
  *ptr = Range(1, size);
  const int32_t next = *ptr - 1;
  const bool alias = next < max_line;
  int32_t m = next + 1;
  if (!alias) m = next - max_line + 1;
  if (m > 64) m = 64;
  *n = Range(1, m);
  *alias_bytes = alias ? 2 * size : 0;
}

int32_t RandomCoef() {
  const uint32_t r = Next();
  if ((r & 7) == 0) return (r & 8) ? 32767 : -32768;
  return static_cast<int16_t>(r >> 16);
}
int32_t PackCoefs(int32_t lo, int32_t hi) {
  return (lo & 0xffff) | static_cast<int32_t>(static_cast<uint32_t>(hi) << 16);
}
// Two random coefficients, packed (sequenced: the order of a call's
// arguments is unspecified, and the compilers differ).
int32_t RandomPair() {
  const int32_t lo = RandomCoef();
  const int32_t hi = RandomCoef();
  return PackCoefs(lo, hi);
}
int32_t RandomPos(int32_t len) {
  const int32_t i = len + Range(-20, 20);
  return (i << 16) | Range(0, 0xffff);
}

void TestInputKernel() {
  int bad = 0;
  for (int trial = 0; trial < 1500; ++trial) {
    if (trial % 50 == 0) FillBuffer(in_c, in_a, PL_IN_WORDS);
    op1fx::PlateInputKernel k;
    k.g1 = RandomPair();
    k.g2 = RandomPair();
    k.khp = Range(0, 32767);
    k.gain = Range(0, 0x3fffffff);
    k.gstep = static_cast<int32_t>(Next()) >> 12;
    k.hp = static_cast<int32_t>(Next());
    int32_t ptr, n, alias;
    RandomRun(PL_IN_SIZE, PL_IN_MAX_LINE, &ptr, &n, &alias);
    if (k.gstep < 0 && k.gain + k.gstep * n < 0) k.gstep = -k.gstep;
    int16_t x[64], out_c[64], out_a[64];
    for (int32_t i = 0; i < 64; ++i) x[i] = RandomSample();
    op1fx::PlateInputKernel kc = k, ka = k;
    op1fx::PlateInputRunC(&kc, x, out_c, in_c + kGuardWords + ptr, n, alias);
#ifdef __bfin__
    op1fx::PlateInputRunAsm(&ka, x, out_a, in_a + kGuardWords + ptr, n, alias);
    bool ok = kc.gain == ka.gain && kc.hp == ka.hp && memcmp(out_c, out_a, 2 * n) == 0 &&
              memcmp(in_c, in_a, sizeof(in_c)) == 0;
    if (!ok) {
      if (bad < 4) {
        printf("input kernel: MISMATCH trial %d n %ld ptr %ld\n", trial, static_cast<long>(n),
               static_cast<long>(ptr));
        for (int32_t i = 0; i < n && i < 4; ++i) {
          printf("  [%ld] c %d asm %d\n", static_cast<long>(i), out_c[i], out_a[i]);
        }
        printf("  gain %ld %ld hp %ld %ld\n", static_cast<long>(kc.gain), static_cast<long>(ka.gain),
               static_cast<long>(kc.hp), static_cast<long>(ka.hp));
      }
      ++bad;
      memcpy(in_a, in_c, sizeof(in_c));
    }
#endif
    for (int32_t i = 0; i < n; ++i) Hash(static_cast<uint16_t>(out_c[i]));
    Hash(static_cast<uint32_t>(kc.hp));
  }
  const bool guards = Guarded(in_c, PL_IN_WORDS) && Guarded(in_a, PL_IN_WORDS);
#ifdef __bfin__
  printf("input kernel: %s (1500 runs, asm = C), guards %s\n", bad ? "FAIL" : "ok",
         guards ? "intact" : "HIT");
#else
  printf("input kernel: C only, guards %s\n", guards ? "intact" : "HIT");
#endif
  if (bad || !guards) ++failures;
}

const int32_t kTapBaseL[7] = { PL_D3, PL_D3, PL_A4, PL_D4, PL_D1, PL_A2, PL_D2 };
const int32_t kTapBaseR[7] = { PL_D1, PL_D1, PL_A2, PL_D2, PL_D3, PL_A4, PL_D4 };
const int32_t kTailBase[6] = { PL_D4, PL_D1, PL_A2, PL_D2, PL_D3, PL_A4 };

void TestTankKernel(bool mono) {
  int bad = 0;
  for (int trial = 0; trial < 1500; ++trial) {
    if (trial % 50 == 0) FillBuffer(tank_c, tank_a, PL_TANK_WORDS);
    op1fx::PlateTankKernel k;
    const op1fx::PlateSizeTable& t = op1fx::kPlateSizes[Range(0, 7)];
    for (int32_t j = 0; j < 6; ++j) k.off[j] = 2 * (kTailBase[j] + t.tail[j]);
    for (int32_t j = 0; j < 7; ++j) {
      k.off[PT_TL + j] = 2 * (kTapBaseL[j] + t.tap_l[j]);
      k.off[PT_TR + j] = 2 * (kTapBaseR[j] + t.tap_r[j]);
    }
    k.decay_klp = RandomPair();
    k.dd1 = RandomPair();
    k.dd2 = RandomPair();
    k.gout = RandomPair();
    k.lp = static_cast<int32_t>(Next());
    k.pos1 = RandomPos(t.a1);
    k.pos2 = RandomPos(t.a3);
    k.step1 = static_cast<int32_t>(Next()) >> 20;    // <= 1/32 sample per sample
    k.step2 = static_cast<int32_t>(Next()) >> 20;
    if (trial % 5 == 0) k.step1 = k.step2 = 0;
    int32_t ptr, n, alias;
    RandomRun(PL_TANK_SIZE, PL_TANK_MAX_LINE, &ptr, &n, &alias);
    int16_t x[64], lc[64], rc[64], la[64], ra[64];
    for (int32_t i = 0; i < 64; ++i) x[i] = RandomSample();
    op1fx::PlateTankKernel kc = k, ka = k;
    op1fx::PlateTankRunC(&kc, x, lc, mono ? nullptr : rc, tank_c + kGuardWords + ptr, n, alias);
#ifdef __bfin__
    if (mono) {
      op1fx::PlateTankRunMonoAsm(&ka, x, la, tank_a + kGuardWords + ptr, n, alias);
    } else {
      op1fx::PlateTankRunAsm(&ka, x, la, ra, tank_a + kGuardWords + ptr, n, alias);
    }
    bool ok = kc.lp == ka.lp && kc.pos1 == ka.pos1 && kc.pos2 == ka.pos2 &&
              memcmp(lc, la, 2 * n) == 0 && (mono || memcmp(rc, ra, 2 * n) == 0) &&
              memcmp(tank_c, tank_a, sizeof(tank_c)) == 0;
    if (!ok) {
      if (bad < 4) {
        printf("tank kernel%s: MISMATCH trial %d n %ld ptr %ld\n", mono ? " mono" : "", trial,
               static_cast<long>(n), static_cast<long>(ptr));
        for (int32_t i = 0; i < n && i < 4; ++i) {
          printf("  [%ld] c %d %d asm %d %d\n", static_cast<long>(i), lc[i], mono ? 0 : rc[i], la[i],
                 mono ? 0 : ra[i]);
        }
        printf("  lp %08lx %08lx\n", static_cast<unsigned long>(kc.lp), static_cast<unsigned long>(ka.lp));
        for (int32_t i = 0; i < PL_TANK_WORDS + 2 * kGuardWords; ++i) {
          if (tank_c[i] != tank_a[i]) {
            printf("  first buffer difference at word %ld (c %d asm %d)\n",
                   static_cast<long>(i - kGuardWords - ptr), tank_c[i], tank_a[i]);
            break;
          }
        }
      }
      ++bad;
      memcpy(tank_a, tank_c, sizeof(tank_c));
    }
#endif
    for (int32_t i = 0; i < n; ++i) {
      Hash(static_cast<uint16_t>(lc[i]));
      if (!mono) Hash(static_cast<uint16_t>(rc[i]));
    }
    Hash(static_cast<uint32_t>(kc.lp));
  }
  const bool guards = Guarded(tank_c, PL_TANK_WORDS) && Guarded(tank_a, PL_TANK_WORDS);
#ifdef __bfin__
  printf("tank kernel%s: %s (1500 runs, asm = C), guards %s\n", mono ? " mono" : "",
         bad ? "FAIL" : "ok", guards ? "intact" : "HIT");
#else
  printf("tank kernel%s: C only, guards %s\n", mono ? " mono" : "", guards ? "intact" : "HIT");
#endif
  if (bad || !guards) ++failures;
}

void TestXfadeKernel() {
  int bad = 0;
  for (int trial = 0; trial < 1000; ++trial) {
    if (trial % 50 == 0) FillBuffer(tank_c, tank_a, PL_TANK_WORDS);
    op1fx::PlateTankKernel k;
    op1fx::PlateXfadeKernel xk;
    const int32_t from = Range(0, 7), to = Range(0, 7);
    const op1fx::PlateSizeTable& t = op1fx::kPlateSizes[from];
    const op1fx::PlateSizeTable& tn = op1fx::kPlateSizes[to];
    for (int32_t j = 0; j < 6; ++j) {
      k.off[j] = 2 * (kTailBase[j] + t.tail[j]);
      xk.off[j] = 2 * (kTailBase[j] + tn.tail[j]);
    }
    for (int32_t j = 0; j < 7; ++j) k.off[PT_TL + j] = k.off[PT_TR + j] = 0;
    k.decay_klp = RandomPair();
    k.dd1 = RandomPair();
    k.dd2 = RandomPair();
    k.gout = 0;
    k.lp = static_cast<int32_t>(Next());
    k.pos1 = RandomPos(t.a1);
    k.pos2 = RandomPos(t.a3);
    xk.pos1 = RandomPos(tn.a1);
    xk.pos2 = RandomPos(tn.a3);
    k.step1 = static_cast<int32_t>(Next()) >> 20;
    k.step2 = static_cast<int32_t>(Next()) >> 20;
    xk.step1 = static_cast<int32_t>(Next()) >> 20;
    xk.step2 = static_cast<int32_t>(Next()) >> 20;
    xk.xf = Range(0, 0x7fff0000 - (64 << 24));
    xk.xf_step = Range(0, 1 << 24);
    xk.w = xk.tmp = 0;
    int32_t ptr, n, alias;
    RandomRun(PL_TANK_SIZE, PL_TANK_MAX_LINE, &ptr, &n, &alias);
    int16_t x[64];
    for (int32_t i = 0; i < 64; ++i) x[i] = RandomSample();
    op1fx::PlateTankKernel kc = k, ka = k;
    op1fx::PlateXfadeKernel xc = xk, xa = xk;
    op1fx::PlateXfadeRunC(&kc, &xc, x, tank_c + kGuardWords + ptr, n, alias);
#ifdef __bfin__
    op1fx::PlateXfadeRunAsm(&ka, &xa, x, tank_a + kGuardWords + ptr, n, alias);
    const bool ok = kc.lp == ka.lp && kc.pos1 == ka.pos1 && kc.pos2 == ka.pos2 &&
                    xc.pos1 == xa.pos1 && xc.pos2 == xa.pos2 && xc.xf == xa.xf &&
                    memcmp(tank_c, tank_a, sizeof(tank_c)) == 0;
    if (!ok) {
      if (bad < 4) {
        printf("xfade kernel: MISMATCH trial %d n %ld ptr %ld lp %08lx %08lx\n", trial,
               static_cast<long>(n), static_cast<long>(ptr), static_cast<unsigned long>(kc.lp),
               static_cast<unsigned long>(ka.lp));
        for (int32_t i = 0; i < PL_TANK_WORDS + 2 * kGuardWords; ++i) {
          if (tank_c[i] != tank_a[i]) {
            printf("  first buffer difference at word %ld (c %d asm %d)\n",
                   static_cast<long>(i - kGuardWords - ptr), tank_c[i], tank_a[i]);
            break;
          }
        }
      }
      ++bad;
      memcpy(tank_a, tank_c, sizeof(tank_c));
    }
#endif
    Hash(static_cast<uint32_t>(kc.lp));
    Hash(static_cast<uint32_t>(xc.xf));
  }
  uint32_t h = 0;
  for (int32_t i = 0; i < PL_TANK_WORDS; ++i) h = h * 31 + static_cast<uint16_t>(tank_c[kGuardWords + i]);
  Hash(h);
  const bool guards = Guarded(tank_c, PL_TANK_WORDS) && Guarded(tank_a, PL_TANK_WORDS);
#ifdef __bfin__
  printf("xfade kernel: %s (1000 runs, asm = C), guards %s\n", bad ? "FAIL" : "ok",
         guards ? "intact" : "HIT");
#else
  printf("xfade kernel: C only, guards %s\n", guards ? "intact" : "HIT");
#endif
  if (bad || !guards) ++failures;
}

int16_t pre_c[PL_PRE_WORDS + kGuardWords], pre_a[PL_PRE_WORDS + kGuardWords];

void TestPreKernel() {
  int bad = 0;
  for (int32_t i = 0; i < PL_PRE_WORDS + kGuardWords; ++i) {
    pre_c[i] = pre_a[i] = i < PL_PRE_WORDS ? RandomSample() : kGuard;
  }
  for (int trial = 0; trial < 1000; ++trial) {
    const int32_t n = Range(1, 64);
    const int32_t w = Range(0, PL_PRE_WORDS - n);
    int32_t d = (Range(0, PL_PRE_MAX - 4) << 16) | Range(0, 0xffff);
    int32_t step = static_cast<int32_t>(Next()) >> 17;   // <= 1/4 sample per sample
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
    op1fx::PlatePreRunC(l, rr, pre_c, xc, w, d, step, n);
#ifdef __bfin__
    op1fx::PlatePreRunAsm(l, rr, pre_a, xa, w, d, step, n);
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
  for (int32_t i = PL_PRE_WORDS; i < PL_PRE_WORDS + kGuardWords; ++i) {
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

struct GuardedPlate {
  uint32_t before[16];
  PlateReverb plate;
  uint32_t after[16];
};
GuardedPlate plate_c, plate_a;
const uint32_t kGuardWord = 0xa5c3a5c3u;

void ArmGuards(GuardedPlate* g) {
  for (int32_t i = 0; i < 16; ++i) g->before[i] = g->after[i] = kGuardWord;
}
bool GuardsOk(const GuardedPlate& g) {
  for (int32_t i = 0; i < 16; ++i) {
    if (g.before[i] != kGuardWord || g.after[i] != kGuardWord) return false;
  }
  return true;
}

// Input signals (integer): white noise, a saw, an impulse.
struct Source {
  uint32_t noise;
  uint32_t phase;
  int32_t t;
  void Init() {
    noise = 1;
    phase = 0;
    t = 0;
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

PlateParams DefaultParams() {
  PlateParams p;
  p.decay = 16384;
  p.damp = 8192;
  p.predelay = 0;
  p.size = 7;
  p.diffusion = 32767;
  p.motion = 16384;
  p.lowcut = 0;
  p.freeze = false;
  return p;
}

// Tank gain g in [0.2, 0.98] -> the DECAY value.
int32_t DecayFor(int32_t g_permille) {
  return ((g_permille - 200) * 32767 + 390) / 780;
}

// One scenario through both instances; returns the C output's checksum.
void RunScenario(const char* name, int32_t scenario, int32_t calls, bool mono_in, bool mono_out) {
  ArmGuards(&plate_c);
  ArmGuards(&plate_a);
  plate_c.plate.Init();
  plate_a.plate.Init();
  plate_c.plate.set_use_asm(false);
#ifdef __bfin__
  plate_a.plate.set_use_asm(true);
#endif
  Source src;
  src.Init();
  checksum = 2166136261u;
  int32_t mismatch_call = -1;
  int32_t peak = 0;
  for (int32_t call = 0; call < calls; ++call) {
    PlateParams p = DefaultParams();
    const int32_t n = (scenario == 3) ? 2 + 2 * (call % 32) : 32;   // any even n
    int16_t l[64], r[64];
    for (int32_t i = 0; i < n; ++i) {
      int16_t s = 0;
      switch (scenario) {
        case 0:   // noise bursts, size changes every 200 calls
          s = (call % 400) < 40 ? src.Noise(16000) : 0;
          p.size = 7 - (call / 200) % 8;
          p.decay = DecayFor(850);
          break;
        case 1:   // a saw, decay 0.95, motion at the top, pre-delay glides
          s = src.Saw(0x00a3d70a, 12000);   // ~ 220 Hz at 22050
          p.decay = DecayFor(950);
          p.motion = 32767;
          p.predelay = (call / 300) % 2 ? 32767 : 4000;
          p.lowcut = 16384;
          break;
        case 2:   // noise then freeze, diffusion and damp moving
          s = call < 300 ? src.Noise(20000) : src.Noise(4000);
          p.freeze = call >= 300 && call < 1200;
          p.damp = (call * 41) & 32767;
          p.diffusion = 32767 - ((call * 23) & 32767);
          p.decay = 32767;
          break;
        default:  // full-scale noise, odd block sizes, every knob jumping
          s = src.Noise(32767);
          p.decay = (call * 977) & 32767;
          p.damp = (call * 331) & 32767;
          p.predelay = (call * 1231) & 32767;
          p.size = (call / 37) % 8;
          p.motion = (call * 2111) & 32767;
          p.lowcut = (call * 613) & 32767;
          p.freeze = (call / 90) % 5 == 4;
          break;
      }
      l[i] = s;
      r[i] = static_cast<int16_t>(scenario == 1 ? s : -s / 2 + (s >> 3));
    }
    int16_t lc[64], rc[64], la[64], ra[64];
    plate_c.plate.Process(l, mono_in ? nullptr : r, lc, mono_out ? nullptr : rc, n, p);
    plate_a.plate.Process(l, mono_in ? nullptr : r, la, mono_out ? nullptr : ra, n, p);
    for (int32_t i = 0; i < n; ++i) {
      Hash(static_cast<uint16_t>(lc[i]));
      if (!mono_out) Hash(static_cast<uint16_t>(rc[i]));
      const int32_t a = lc[i] < 0 ? -lc[i] : lc[i];
      if (a > peak) peak = a;
      if (mismatch_call < 0 && (lc[i] != la[i] || (!mono_out && rc[i] != ra[i]))) mismatch_call = call;
    }
  }
  const bool buffers = memcmp(plate_c.plate.tank_buffer(), plate_a.plate.tank_buffer(),
                              2 * PL_TANK_WORDS) == 0;
  const bool guards = GuardsOk(plate_c) && GuardsOk(plate_a);
  printf("reverb %-28s checksum %08lx peak %5ld", name, static_cast<unsigned long>(checksum),
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

PlateReverb bench;

void Cycles(const char* name, bool use_asm, bool mono, int32_t motion) {
  bench.Init();
  bench.set_use_asm(use_asm);
  Source src;
  src.Init();
  PlateParams p = DefaultParams();
  p.decay = DecayFor(900);
  p.motion = motion;
  p.predelay = 8000;
  harness::CycleStats stats;
  const int32_t calls = 1200;   // > one trip round the tank (16053 samples = 502 calls)
  for (int32_t call = 0; call < calls; ++call) {
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
  // 689.0625 calls per second (22050 / 32).
  const unsigned long khz_avg = static_cast<unsigned long>(stats.total * 689 / stats.n / 1000);
  const unsigned long khz_max = static_cast<unsigned long>(stats.max * 689 / 1000);
  printf("cycles %-24s per call avg %5lu min %5lu max %5lu  per sample %3lu  => %lu.%02lu MHz avg, %lu.%02lu MHz peak\n",
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
  PlateParams p = DefaultParams();
  p.decay = 32767;
  p.damp = 0;
  p.motion = scenario == 3 ? 0 : 32767;
  const int32_t calls_per_10s = 6891;   // 10 s at 22050 / 32
  checksum = 2166136261u;
  printf("stability %s:\n", name);
  int32_t last_peak = 0;
  for (int32_t seg = 0; seg < 6; ++seg) {
    int32_t peak = 0;
    int64_t sum = 0, sum2 = 0;
    for (int32_t call = 0; call < calls_per_10s; ++call) {
      const int32_t t = seg * calls_per_10s + call;
      int16_t l[32], r[32], wl[32], wr[32];
      for (int32_t i = 0; i < 32; ++i) {
        int16_t s = 0;
        if (scenario == 0) s = src.Noise(16384);                                  // -6 dBFS noise
        if (scenario == 1 || scenario == 3) s = src.Noise(16384);                 // gated by freeze
        if (scenario == 2) s = src.Saw(0x00a3d70a, 16384);                        // a held saw
        l[i] = r[i] = s;
      }
      p.freeze = (scenario == 1 || scenario == 3) && t >= 1378;   // freeze after 2 s
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
    // Tank DC: the buffer's mean.
    int64_t tank_sum = 0;
    const int16_t* tb = bench.tank_buffer();
    for (int32_t i = 0; i < PL_TANK_WORDS; ++i) tank_sum += tb[i];
    printf("  %2ld-%2ld s: wet peak %5ld rms %5ld dc %4ld  tank mean %4ld\n",
           static_cast<long>(seg * 10), static_cast<long>(seg * 10 + 10), static_cast<long>(peak),
           static_cast<long>(rms), static_cast<long>(dc),
           static_cast<long>(tank_sum / PL_TANK_WORDS));
    last_peak = peak;
  }
  printf("stability %s checksum %08lx\n", name, static_cast<unsigned long>(checksum));
  (void)last_peak;
}

}  // namespace

int main() {
  harness::EnableCycleCounter();
  TestInputKernel();
  TestTankKernel(false);
  TestTankKernel(true);
  TestXfadeKernel();
  TestPreKernel();
  printf("kernels checksum %08lx\n", static_cast<unsigned long>(checksum));

  RunScenario("noise bursts, size steps", 0, 3300, false, false);
  RunScenario("saw, decay .95, motion, pre", 1, 1500, false, false);
  RunScenario("freeze, damp/diffusion move", 2, 1500, false, false);
  RunScenario("full-scale, odd n, all knobs", 3, 1500, false, false);
  RunScenario("mono in, mono out", 0, 1200, true, true);
  RunScenario("mono in, stereo out", 1, 1200, true, false);

#ifdef __bfin__
  Cycles("stereo, motion on", true, false, 16384);
  Cycles("stereo, motion off", true, false, 0);
  Cycles("mono, motion on", true, true, 16384);
  Cycles("mono, motion off", true, true, 0);
  Cycles("C stereo, motion on", false, false, 16384);
  Cycles("C mono, motion on", false, true, 16384);
#endif

  Stability("decay max, noise", 0);
  Stability("freeze after 2 s, motion max", 1);
  Stability("freeze after 2 s, motion 0", 3);
  Stability("decay max, held saw", 2);

  printf("%s: %d failure(s)\n", failures ? "FAIL" : "ok", failures);
  return failures ? 1 : 0;
}
