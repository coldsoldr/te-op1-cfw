// TAPE's checks (make test-tape), built for the Blackfin simulator and for
// the host:
//   kernels   each assembly kernel against its C reference on random runs
//             (random states, coefficients and full-scale samples, n 1..64,
//             guard words round the buffers), sample for sample (Blackfin);
//   effect    a C instance against an assembly instance through scenarios
//             (knobs jumping and gliding, mono, stereo, mono -> stereo, odd
//             n), outputs equal (Blackfin); the C output's checksums, which
//             the host prints too (the Makefile compares them);
//   identity  mix 0 returns the input bit for bit, every other knob
//             anywhere, also after a ramp down to 0;
//   silence   no DC and no limit cycles: silence in -> exact silence out
//             after loud noise;
//   cycles    per 64-sample call (689 calls/s): stereo and mono, assembly
//             and C (Blackfin);
//   analysis  (host, float) harmonics and level against SATURATE, pitch
//             deviation against WOBBLE, the response against AGE, the DC.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "rings/harness/cycles.h"
#include "src/tape/tape.h"

#ifndef __bfin__
#include <cmath>
#endif

using op1fx::TapeParams;
using op1fx::TapeSat;

namespace {

uint32_t rng = 12345;
uint32_t Next() {
  rng = rng * 1664525u + 1013904223u;
  return rng;
}
int32_t Range(int32_t lo, int32_t hi) {
  return lo + static_cast<int32_t>(Next() % static_cast<uint32_t>(hi - lo + 1));
}
int16_t RandomSample() {
  const uint32_t r = Next();
  if ((r & 15) == 0) return (r & 16) ? 32767 : -32768;
  return static_cast<int16_t>(r >> 16);
}
int32_t RandomWord() {
  return static_cast<int32_t>((static_cast<uint32_t>(RandomSample()) << 16) |
                              (static_cast<uint16_t>(RandomSample())));
}

uint32_t checksum = 2166136261u;
void Hash(uint32_t x) {
  checksum ^= x;
  checksum *= 16777619u;
}

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL: %s\n", what);
    ++failures;
  }
}

// A test signal: a saw-ish chord plus noise, int16, deterministic.
struct Source {
  uint32_t ph[3] = { 0, 0, 0 };
  int16_t Next(int32_t level) {
    const uint32_t inc[3] = { 9739155u, 14608732u, 21913098u };   // ~100, 150, 225 Hz
    int32_t s = 0;
    for (int32_t i = 0; i < 3; ++i) {
      ph[i] += inc[i];
      s += static_cast<int32_t>(ph[i] >> 18) - 8192;
    }
    s += static_cast<int32_t>(::Next() >> 20) - 2048;
    return static_cast<int16_t>((s * level) >> 15);
  }
};

TapeParams RandomParams() {
  TapeParams p;
  p.saturate = Range(0, 32767);
  p.wobble = Range(0, 32767);
  p.age = Range(0, 32767);
  p.mix = Range(0, 32767);
  return p;
}

// --- Kernels: assembly = C (Blackfin) --------------------------------------

#ifdef __bfin__
const int32_t kGuardWords = 16;
const int32_t kGuard = 0x5a5a5a5a;

bool GuardsOk(const int32_t* b, int32_t words) {
  for (int32_t i = 0; i < kGuardWords; ++i) {
    if (b[i] != kGuard || b[kGuardWords + words + i] != kGuard) return false;
  }
  return true;
}
void SetGuards(int32_t* b, int32_t words) {
  for (int32_t i = 0; i < kGuardWords; ++i) b[i] = b[kGuardWords + words + i] = kGuard;
}

int32_t ring_c[2 * kGuardWords + TAPE_RING_WORDS], ring_a[2 * kGuardWords + TAPE_RING_WORDS];
// Data blocks of kData words between guards: a kernel's block starts one
// word in (its scratch word before) and may read one past its end.
const int32_t kData = 68;
int32_t buf_c[2 * kGuardWords + kData], buf_a[2 * kGuardWords + kData];
int32_t buf2_c[2 * kGuardWords + kData], buf2_a[2 * kGuardWords + kData];
int16_t io_c[66], io_a[66], in_l[64], in_r[64];

// The first difference between two int16 arrays (or -1).
int32_t FirstDiff16(const void* a, const void* b, int32_t count) {
  const int16_t* x = static_cast<const int16_t*>(a);
  const int16_t* y = static_cast<const int16_t*>(b);
  for (int32_t i = 0; i < count; ++i) {
    if (x[i] != y[i]) {
      printf("    [%ld] C %d asm %d\n", static_cast<long>(i), x[i], y[i]);
      return i;
    }
  }
  return -1;
}

void FillBuf(int32_t* c, int32_t* a) {
  SetGuards(c, kData);
  for (int32_t i = 0; i < kData; ++i) c[kGuardWords + i] = RandomWord();
  memcpy(a, c, (2 * kGuardWords + kData) * 4);
}

void TestDelayKernel() {
  int bad = 0;
  for (int32_t run = 0; run < 1500; ++run) {
    const bool mono = (run & 3) == 3;
    SetGuards(ring_c, TAPE_RING_WORDS);
    for (int32_t i = 0; i < TAPE_RING_WORDS; ++i) ring_c[kGuardWords + i] = RandomWord();
    memcpy(ring_a, ring_c, sizeof(ring_c));
    FillBuf(buf_c, buf_a);
    FillBuf(buf2_c, buf2_a);
    const int32_t n = Range(1, 64);
    for (int32_t i = 0; i < n; ++i) {
      in_l[i] = RandomSample();
      in_r[i] = RandomSample();
    }
    op1fx::TapeDelayKernel kc, ka;
    kc.widx = Range(n, TAPE_RING - 1);   // (the assembly needs no wrap within a run)
    const int32_t d0 = run % 11 == 0 ? 0 : Range(0, 250 << 16);
    const int32_t d1 = Range(0, 250 << 16);
    kc.pos = (static_cast<uint32_t>(kc.widx) << 16) + static_cast<uint32_t>(d0) + (Next() & 0xff000000u);
    kc.step = run % 7 == 0 ? 0 : (d1 - d0) / n;
    ka = kc;
    int16_t* wlc = reinterpret_cast<int16_t*>(buf_c + kGuardWords + 1);
    int16_t* wla = reinterpret_cast<int16_t*>(buf_a + kGuardWords + 1);
    int16_t* wrc = reinterpret_cast<int16_t*>(buf2_c + kGuardWords + 1);
    int16_t* wra = reinterpret_cast<int16_t*>(buf2_a + kGuardWords + 1);
    op1fx::TapeDelayRunC(&kc, ring_c + kGuardWords, in_l, mono ? nullptr : in_r, wlc, wrc, n);
    if (mono) {
      op1fx::TapeDelayRunMonoAsm(&ka, ring_a + kGuardWords, in_l, wla, n);
    } else {
      op1fx::TapeDelayRunAsm(&ka, ring_a + kGuardWords, in_l, in_r, wla, wra, n);
    }
    const bool ok = memcmp(ring_c, ring_a, sizeof(ring_c)) == 0 && memcmp(buf_c, buf_a, sizeof(buf_c)) == 0 &&
                    memcmp(buf2_c, buf2_a, sizeof(buf2_c)) == 0 && kc.pos == ka.pos &&
                    kc.widx == ka.widx && GuardsOk(ring_a, TAPE_RING_WORDS) && GuardsOk(buf_a, kData) &&
                    GuardsOk(buf2_a, kData);
    if (!ok && bad++ < 3) {
      printf("  delay run %ld (n %ld, mono %d) differs: pos %08lx/%08lx widx %ld/%ld\n", (long)run,
             (long)n, mono, (unsigned long)kc.pos, (unsigned long)ka.pos, (long)kc.widx, (long)ka.widx);
      FirstDiff16(ring_c, ring_a, 2 * (2 * kGuardWords + TAPE_RING_WORDS));
      FirstDiff16(buf_c, buf_a, 2 * (2 * kGuardWords + kData));
      FirstDiff16(buf2_c, buf2_a, 2 * (2 * kGuardWords + kData));
    }
  }
  printf("kernel delay (stereo + mono): 1500 runs, %s\n", bad ? "DIFFER" : "equal");
  Check(!bad, "delay kernel");
}

int32_t RandomPair(int32_t lo, int32_t hi) {
  return static_cast<int32_t>((static_cast<uint32_t>(Range(lo, hi)) << 16) |
                              static_cast<uint16_t>(Range(lo, hi)));
}

void RandomHb(op1fx::TapeHbKernel* k, bool up) {
  k->x = RandomWord();
  k->s1 = RandomWord();
  k->s2 = RandomWord();
  k->c1 = RandomPair(0, 32767);
  k->c2 = RandomPair(0, 32767);
  if (Next() & 1) {   // the real coefficients
    k->c1 = up ? (9301 << 16) | 2617 : (2617 << 16) | 9301;
    k->c2 = up ? (27342 << 16) | 17869 : (17869 << 16) | 27342;
  }
  if (up) {   // x | x
    const uint32_t x = static_cast<uint16_t>(k->x);
    k->x = static_cast<int32_t>((x << 16) | x);
  }
}

void RandomTone(op1fx::TapeToneKernel* k, int32_t n) {
  k->a0 = static_cast<int32_t>(RandomSample()) * 65536;
  k->a1 = static_cast<int32_t>(RandomSample()) * 65536 + static_cast<int32_t>(Next() & 0xffff);
  k->lp = RandomWord();
  k->s1 = static_cast<int32_t>(Next());
  k->s2 = static_cast<int32_t>(Next() >> (Next() & 15));
  k->kab = RandomPair(0, 32767);
  k->gk = RandomPair(0, 32767);
  const int32_t m = Range(0, 32767);
  int32_t step = Range(-512, 512);
  if (m + step * n > 32767 || m + step * n < 0) step = 0;
  k->mix = static_cast<int32_t>((static_cast<uint32_t>(step) << 16) | static_cast<uint32_t>(m));
}

void RandomSat(op1fx::TapeSatKernel* k) {
  const int32_t h = Range(-32767, 32767), sh = Range(0, 5);
  k->hsh = static_cast<int32_t>((static_cast<uint32_t>(h) << 16) | static_cast<uint32_t>(sh));
  k->k12 = RandomPair(-32767, 32767);
  k->k0 = static_cast<int32_t>((static_cast<uint32_t>(Range(-32767, 32767)) << 16) | 32767u);
  k->j = Range(0, 3);
}

// The lane kernel (up, sat, down, tone, mix) against its C reference:
// random states, coefficients and samples, n even 2..64, an unaligned io
// half the time, guard words round w, u and io.
void TestLaneKernel() {
  int bad = 0;
  for (int32_t run = 0; run < 2000; ++run) {
    const int32_t n = 2 * Range(1, 32);
    FillBuf(buf_c, buf_a);     // w at word 1
    FillBuf(buf2_c, buf2_a);   // u at word 1
    if (run & 2) {   // the wet path's input at half scale, as the delay makes it
      int16_t* w = reinterpret_cast<int16_t*>(buf_c + kGuardWords + 1);
      for (int32_t i = 0; i < n + 2; ++i) w[i] = static_cast<int16_t>(w[i] >> 1);
      memcpy(buf_a, buf_c, sizeof(buf_c));
    }
    int16_t io_buf_c[2 + 64 + 2], io_buf_a[2 + 64 + 2];
    for (int32_t i = 0; i < 68; ++i) io_buf_c[i] = io_buf_a[i] = RandomSample();
    const int32_t off = (run & 1) ? 1 : 2;   // unaligned / aligned
    op1fx::TapeLaneKernel kc, ka;
    RandomHb(&kc.up, true);
    RandomHb(&kc.down, false);
    RandomTone(&kc.tone, n);
    ka = kc;
    op1fx::TapeSatKernel sat;
    RandomSat(&sat);
    op1fx::TapeLaneRunC(&kc, &sat, reinterpret_cast<int16_t*>(buf_c + kGuardWords + 1),
                        buf2_c + kGuardWords + 1, io_buf_c + off, n);
    op1fx::TapeLaneAsm(&ka, &sat, reinterpret_cast<int16_t*>(buf_a + kGuardWords + 1),
                       buf2_a + kGuardWords + 1, io_buf_a + off, n);
    const bool ok = memcmp(buf_c, buf_a, sizeof(buf_c)) == 0 && memcmp(buf2_c, buf2_a, sizeof(buf2_c)) == 0 &&
                    memcmp(io_buf_c, io_buf_a, sizeof(io_buf_c)) == 0 &&
                    memcmp(&kc, &ka, sizeof(kc)) == 0 && GuardsOk(buf_a, kData) && GuardsOk(buf2_a, kData);
    if (!ok && bad++ < 3) {
      printf("  lane run %ld (n %ld) differs\n", (long)run, (long)n);
      printf("   w:");
      FirstDiff16(buf_c, buf_a, 2 * (2 * kGuardWords + kData));
      printf("   u:");
      FirstDiff16(buf2_c, buf2_a, 2 * (2 * kGuardWords + kData));
      printf("   io:");
      FirstDiff16(io_buf_c, io_buf_a, 68);
      printf("   kernels:");
      FirstDiff16(&kc, &ka, sizeof(kc) / 2);
      printf("\n");
    }
  }
  printf("kernel lane (up, sat, down, tone, mix): 2000 runs, %s\n", bad ? "DIFFER" : "equal");
  Check(!bad, "lane kernel");
}
#endif  // __bfin__

// --- The whole effect --------------------------------------------------------

TapeSat fx_c, fx_a;
int16_t l_c[64], r_c[64], l_a[64], r_a[64];

// Scenarios on a C instance (and an assembly one on the Blackfin): the C
// outputs go into the checksum.
void TestEffect() {
  rng = 777;   // the same stream on both builds (the kernel tests run on the Blackfin only)
  struct Scenario {
    const char* name;
    int32_t calls;
    bool mono;
  };
  const Scenario sc[] = {
    { "stereo, knobs jumping", 600, false },
    { "mono, knobs gliding", 600, true },
    { "mono -> stereo, odd n", 600, false },
    { "stereo, everything at the top", 600, false },
  };
  for (int32_t s = 0; s < 4; ++s) {
    fx_c.Init();
    fx_a.Init();
    fx_c.set_use_asm(false);
    fx_a.set_use_asm(true);
    Source src;
    TapeParams p = RandomParams();
    TapeParams glide = p;
    int32_t bad = 0;
    for (int32_t call = 0; call < sc[s].calls; ++call) {
      int32_t n = 64;
      if (s == 2) n = 2 * Range(1, 32);
      if (s == 0 && call % 100 == 0) p = RandomParams();
      if (s == 1) {
        if (call % 300 == 0) glide = RandomParams();
        p.saturate += (glide.saturate - p.saturate) >> 4;
        p.wobble += (glide.wobble - p.wobble) >> 4;
        p.age += (glide.age - p.age) >> 4;
        p.mix += (glide.mix - p.mix) >> 4;
      }
      if (s == 2 && call % 50 == 0) p = RandomParams();
      if (s == 3) p.saturate = p.wobble = p.age = p.mix = 32767;
      const bool mono = sc[s].mono || (s == 2 && call < 500);
      for (int32_t i = 0; i < n; ++i) {
        l_c[i] = l_a[i] = (s == 3 && (call & 64)) ? RandomSample() : src.Next(32767);
        r_c[i] = r_a[i] = (s == 3) ? RandomSample() : src.Next(20000);
      }
      fx_c.Process(l_c, mono ? nullptr : r_c, n, p);
#ifdef __bfin__
      fx_a.Process(l_a, mono ? nullptr : r_a, n, p);
      if (memcmp(l_c, l_a, n * 2) != 0 || (!mono && memcmp(r_c, r_a, n * 2) != 0)) {
        if (bad++ < 3) printf("  %s: call %ld differs\n", sc[s].name, (long)call);
      }
#endif
      for (int32_t i = 0; i < n; ++i) Hash(static_cast<uint16_t>(l_c[i]) | (mono ? 0 : r_c[i] << 16));
    }
#ifdef __bfin__
    printf("effect %-32s asm %s C\n", sc[s].name, bad ? "DIFFERS from" : "=");
    Check(!bad, sc[s].name);
#endif
    printf("effect %-32s checksum %08lx\n", sc[s].name, static_cast<unsigned long>(checksum));
  }
}

// Mix 0: the output is the input, whatever the other knobs do.
void TestIdentity() {
  TapeSat* fx[2] = { &fx_c, &fx_a };
  int32_t bad = 0;
  // The C instance on the host, the assembly one on the Blackfin.
#ifdef __bfin__
  for (int32_t which = 1; which < 2; ++which) {
#else
  for (int32_t which = 0; which < 1; ++which) {
#endif
    for (int32_t mono = 0; mono < 2; ++mono) {
      fx[which]->Init();
      fx[which]->set_use_asm(which == 1);
      Source src;
      TapeParams p = RandomParams();
      p.mix = 0;
      for (int32_t call = 0; call < 2000; ++call) {
        if (call % 37 == 0) {
          p = RandomParams();
          p.mix = 0;
        }
        const int32_t n = 2 * Range(1, 32);
        int16_t l[64], r[64], l0[64], r0[64];
        for (int32_t i = 0; i < n; ++i) {
          l[i] = l0[i] = (call & 8) ? RandomSample() : src.Next(32767);
          r[i] = r0[i] = RandomSample();
        }
        fx[which]->Process(l, mono ? nullptr : r, n, p);
        if (memcmp(l, l0, n * 2) != 0 || (!mono && memcmp(r, r0, n * 2) != 0)) ++bad;
      }
      // A ramp from full wet down to 0: exact again once it lands.
      p.mix = 32767;
      int32_t landed = -1;
      for (int32_t call = 0; call < 400; ++call) {
        if (call == 100) p.mix = 0;
        int16_t l[64], r[64], l0[64], r0[64];
        for (int32_t i = 0; i < 64; ++i) {
          l[i] = l0[i] = src.Next(32767);
          r[i] = r0[i] = src.Next(32767);
        }
        fx[which]->Process(l, mono ? nullptr : r, 64, p);
        const bool same = memcmp(l, l0, 128) == 0 && (mono || memcmp(r, r0, 128) == 0);
        if (call > 100 && same && landed < 0) landed = call - 100;
        if (landed >= 0 && !same) ++bad;
      }
      if (landed < 0 || landed > 60) ++bad;
      printf("identity (%s, %s): mix 0 = input; a ramp to 0 lands after %ld calls\n",
             which ? "asm" : "C", mono ? "mono" : "stereo", (long)landed);
    }
  }
  Check(!bad, "mix 0 identity");
}

// Silence after noise: the output must come back to exact 0 (no DC, no
// limit cycles in the allpasses or the filters).
void TestSilence() {
  TapeSat* fx[2] = { &fx_c, &fx_a };
  int32_t bad = 0;
  // The C instance on the host, the assembly one on the Blackfin.
#ifdef __bfin__
  for (int32_t which = 1; which < 2; ++which) {
#else
  for (int32_t which = 0; which < 1; ++which) {
#endif
    for (int32_t setting = 0; setting < 3; ++setting) {
      fx[which]->Init();
      fx[which]->set_use_asm(which == 1);
      TapeParams p;
      p.saturate = setting == 0 ? 0 : (setting == 1 ? 16384 : 32767);
      p.wobble = setting * 16000;
      p.age = setting * 16000;
      p.mix = 32767;
      int32_t last_nonzero = -1;
      for (int32_t call = 0; call < 1000; ++call) {
        int16_t l[64], r[64];
        for (int32_t i = 0; i < 64; ++i) {
          l[i] = call < 300 ? RandomSample() : 0;
          r[i] = call < 300 ? RandomSample() : 0;
        }
        fx[which]->Process(l, r, 64, p);
        for (int32_t i = 0; i < 64; ++i) {
          if (l[i] || r[i]) last_nonzero = call;
        }
      }
      printf("silence (%s, setting %ld): last non-zero output %ld calls after the noise\n",
             which ? "asm" : "C", (long)setting, (long)(last_nonzero - 300));
      if (last_nonzero > 900) ++bad;
    }
  }
  Check(!bad, "silence");
}

#ifdef __bfin__
void TestCycles() {
  for (int32_t asm_ = 1; asm_ >= 0; --asm_) {
    for (int32_t mode = 0; mode < (asm_ ? 4 : 2); ++mode) {
      const int32_t mono = mode & 1;
      const bool moving = mode >= 2;   // every knob moving every call (an LFO on each)
      harness::CycleStats st;
      fx_a.Init();
      fx_a.set_use_asm(asm_ != 0);
      Source src;
      TapeParams p = { 20000, 12000, 16000, 24000 };
      for (int32_t call = 0; call < (asm_ ? 1000 : 100); ++call) {
        if (call % 200 == 100) p.saturate ^= 4096;   // knobs moving now and then
        if (moving) {
          p.saturate = 16384 + ((call * 97) & 8191);
          p.wobble = 8192 + ((call * 131) & 8191);
          p.age = 8192 + ((call * 61) & 8191);
          p.mix = 16384 + ((call * 37) & 8191);
        }
        for (int32_t i = 0; i < 64; ++i) {
          l_a[i] = src.Next(30000);
          r_a[i] = src.Next(30000);
        }
        const uint64_t t0 = harness::ReadCycles();
        fx_a.Process(l_a, mono ? nullptr : r_a, 64, p);
        const uint64_t t1 = harness::ReadCycles();
        if (call >= 10) st.Add(t1 - t0);
      }
      const double avg = static_cast<double>(st.total) / st.n;
      printf("cycles %s %-6s%s per 64-sample call: avg %6.0f max %6lu  => %5.2f MHz (peak %5.2f)\n",
             asm_ ? "asm" : "C  ", mono ? "mono" : "stereo", moving ? ", knobs moving" : "", avg, static_cast<unsigned long>(st.max),
             avg * 689.0 / 1e6, st.max * 689.0 / 1e6);
    }
  }
}
#endif

// --- Analysis (host, float) -----------------------------------------------

#ifndef __bfin__
const double kFs = 44100.0;
const double kPi = 3.14159265358979323846;

double Goertzel(const int16_t* x, int32_t n, double f) {
  double re = 0, im = 0;
  for (int32_t i = 0; i < n; ++i) {
    const double w = 0.5 - 0.5 * cos(2 * kPi * (i + 0.5) / n);   // Hann
    re += x[i] * w * cos(2 * kPi * f * i / kFs);
    im -= x[i] * w * sin(2 * kPi * f * i / kFs);
  }
  return sqrt(re * re + im * im) * 4.0 / n;   // Hann's coherent gain 0.5
}

double Rms(const int16_t* x, int32_t n) {
  double s = 0;
  for (int32_t i = 0; i < n; ++i) s += static_cast<double>(x[i]) * x[i];
  return sqrt(s / n);
}

const int32_t kLen = 64 * 8268;   // 12 s, whole blocks
int16_t sig_l[kLen], sig_r[kLen];

// A sine through a fresh instance (stereo, both sides the same), knobs
// fixed; the output in sig_l.
void RunSine(double f, double amp, const TapeParams& p, int32_t len) {
  TapeSat fx;
  fx.Init();
  fx.set_use_asm(false);
  for (int32_t i = 0; i < len; ++i) {
    sig_l[i] = sig_r[i] = static_cast<int16_t>(lrint(amp * 32767 * sin(2 * kPi * f * i / kFs)));
  }
  for (int32_t i = 0; i + 64 <= len; i += 64) fx.Process(sig_l + i, sig_r + i, 64, p);
}

void Analysis() {
  printf("\n-- SATURATE: harmonics (dBc) and level (wet RMS / dry RMS, dB); 1 kHz sine, mix 1, other knobs 0\n");
  const double amps[3] = { 0.0625, 0.25, 1.0 };
  const char* amp_name[3] = { "-24 dBFS", "-12 dBFS", "  0 dBFS" };
  for (int32_t a = 0; a < 3; ++a) {
    printf("  %s   sat:   H3     H5     H7    THD   level\n", amp_name[a]);
    for (int32_t s = 0; s <= 8; ++s) {
      TapeParams p = { s * 4096 > 32767 ? 32767 : s * 4096, 0, 0, 32767 };
      RunSine(1000.0, amps[a], p, 44100);
      const int16_t* x = sig_l + 22050;
      const int32_t n = 16384;
      const double h1 = Goertzel(x, n, 1000);
      double thd = 0;
      for (int32_t k = 2; k <= 15; ++k) {
        const double hk = Goertzel(x, n, 1000.0 * k);
        thd += hk * hk;
      }
      const double h3 = Goertzel(x, n, 3000), h5 = Goertzel(x, n, 5000), h7 = Goertzel(x, n, 7000);
      const double level = 20 * log10(Rms(x, n) / (amps[a] * 32767 / sqrt(2.0)));
      printf("       %4.2f  %6.1f %6.1f %6.1f %6.1f %+6.2f\n", s / 8.0, 20 * log10(h3 / h1 + 1e-9),
             20 * log10(h5 / h1 + 1e-9), 20 * log10(h7 / h1 + 1e-9), 20 * log10(sqrt(thd) / h1 + 1e-9),
             level);
    }
  }

  printf("\n-- Aliasing: a 7 kHz sine at 0 dBFS, sat 1: the largest non-harmonic component (dBc)\n");
  for (int32_t s = 4; s <= 8; s += 4) {
    TapeParams p = { s * 4096 > 32767 ? 32767 : s * 4096, 0, 0, 32767 };
    RunSine(7000.0, 1.0, p, 44100);
    const int16_t* x = sig_l + 22050;
    const int32_t n = 16384;
    const double h1 = Goertzel(x, n, 7000);
    double worst = 0, worst_f = 0;
    for (int32_t k = 3; k <= 15; k += 2) {   // odd harmonics fold at 44.1k - k 7k ...
      double f = fmod(7000.0 * k, kFs);
      if (f > kFs / 2) f = kFs - f;
      if (fabs(f - 7000.0 * k) < 1) continue;   // not folded
      const double a = Goertzel(x, n, f);
      if (a > worst) {
        worst = a;
        worst_f = f;
      }
    }
    printf("  sat %.1f: %.1f dBc at %.0f Hz\n", s / 8.0, 20 * log10(worst / h1 + 1e-9), worst_f);
  }

  printf("\n-- WOBBLE: pitch deviation of a 1 kHz sine (cents; peak over 10 s, after 2 s)\n");
  printf("  wobble  dev+   dev-   rms\n");
  for (int32_t w = 0; w <= 8; ++w) {
    TapeParams p = { 0, w * 4096 > 32767 ? 32767 : w * 4096, 0, 32767 };
    RunSine(1000.0, 0.25, p, kLen);
    double last = -1, maxc = -1e9, minc = 1e9, sum2 = 0;
    int32_t count = 0;
    for (int32_t i = 44100 * 2; i < kLen - 1; ++i) {
      if (sig_l[i] < 0 && sig_l[i + 1] >= 0) {
        const double t = i + static_cast<double>(-sig_l[i]) / (sig_l[i + 1] - sig_l[i]);
        if (last > 0) {
          const double c = 1200 * log2(kFs / (t - last) / 1000.0);
          if (c > maxc) maxc = c;
          if (c < minc) minc = c;
          sum2 += c * c;
          ++count;
        }
        last = t;
      }
    }
    printf("  %4.2f  %+6.2f %+6.2f %6.2f\n", w / 8.0, maxc, minc, sqrt(sum2 / count));
  }

  printf("\n-- AGE: response (dB) of a -12 dBFS sine, sat 0, mix 1\n");
  const double freqs[] = { 30, 60, 90, 150, 300, 1000, 3000, 6000, 10000, 15000 };
  printf("  age  ");
  for (double f : freqs) printf("%7.0f", f);
  printf("\n");
  for (int32_t a = 0; a <= 4; ++a) {
    printf("  %4.2f ", a / 4.0);
    for (double f : freqs) {
      TapeParams p = { 0, 0, a * 8192 > 32767 ? 32767 : a * 8192, 32767 };
      RunSine(f, 0.25, p, 44100 * 2);
      const int16_t* x = sig_l + 44100;
      printf("%+7.2f", 20 * log10(Goertzel(x, 32768, f) / (0.25 * 32767)));
    }
    printf("\n");
  }

  // The effect is odd but for its rounding: the DC it makes is half the
  // mean of out(x) + out(-x), here over 4 s of a 100 Hz sine and of a chord.
  printf("\n-- DC made (LSB): (mean out(x) + mean out(-x)) / 2, mix 1, 4 s\n");
  static int16_t neg[64 * 2756];
  const int32_t len = 64 * 2756;
  for (int32_t sig = 0; sig < 2; ++sig) {
    for (int32_t s = 0; s <= 4; ++s) {
      const int32_t v = s * 8192 > 32767 ? 32767 : s * 8192;
      TapeParams p = { v, v, v, 32767 };
      double sum = 0;
      for (int32_t pass = 0; pass < 2; ++pass) {
        TapeSat fx;
        fx.Init();
        fx.set_use_asm(false);
        for (int32_t i = 0; i < len; ++i) {
          double x = sig ? 0.4 * sin(2 * kPi * 110 * i / kFs) + 0.3 * sin(2 * kPi * 277 * i / kFs) +
                               0.25 * sin(2 * kPi * 1650 * i / kFs)
                         : sin(2 * kPi * 100 * i / kFs);
          if (pass) x = -x;
          sig_l[i] = sig_r[i] = static_cast<int16_t>(lrint(x * 32767));
        }
        for (int32_t i = 0; i < len; i += 64) fx.Process(sig_l + i, sig_r + i, 64, p);
        for (int32_t i = 0; i < len; ++i) sum += sig_l[i];
      }
      printf("  %s, all knobs %.2f: %+.4f\n", sig ? "chord" : "100 Hz", s / 4.0, sum / (2 * len));
    }
  }
}
#endif

}  // namespace

int main() {
  harness::EnableCycleCounter();
#ifdef __bfin__
  TestDelayKernel();
  TestLaneKernel();
#endif
  TestEffect();
  TestIdentity();
  TestSilence();
#ifdef __bfin__
  TestCycles();
#else
  Analysis();
#endif
  printf("%s (%d failures)\n", failures ? "FAILED" : "all passed", failures);
  return failures ? 1 : 0;
}
