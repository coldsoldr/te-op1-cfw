// DRIVE's checks (make test-drive), built for the Blackfin simulator and
// for the host:
//   kernels   each assembly pass against its C reference on random runs
//             (full-scale samples, random coefficients, every n), word for
//             word, with guard words round the buffers (Blackfin);
//   effect    a C instance and an assembly instance through scenarios
//             (stereo, mono split, short mono blocks, knobs jumping, odd
//             block sizes): outputs equal (Blackfin); the C output's
//             checksums, which the host build prints too;
//   identity  mix 0 returns the input bit for bit (stereo and mono, from
//             Init and after the mix has been up and come back);
//   cycles    per 64-sample call, stereo and mono (Blackfin);
//   analysis  (host only: double maths; the Blackfin's output is the same,
//             bit for bit) a sine's harmonics with and without bias, the
//             level across drive, aliasing with and without oversampling,
//             DC at the output.

#include <cstdint>
#include <cstdio>
#include <cstring>
#ifndef __bfin__
#include <cmath>
#endif

#include "rings/harness/cycles.h"
#include "src/drive/drive.h"

using op1fx::Drive;
using op1fx::DriveParams;

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
int32_t RandomWord() {
  const int32_t lo = RandomSample();
  const int32_t hi = RandomSample();
  return (lo & 0xffff) | static_cast<int32_t>(static_cast<uint32_t>(hi) << 16);
}
int32_t Pack(int32_t lo, int32_t hi) {
  return (lo & 0xffff) | static_cast<int32_t>(static_cast<uint32_t>(hi) << 16);
}
// Two random halves, sequenced (the order of a call's arguments is
// unspecified, and the compilers differ).
int32_t RangePair(int32_t lo0, int32_t hi0, int32_t lo1, int32_t hi1) {
  const int32_t a = Range(lo0, hi0);
  const int32_t b = Range(lo1, hi1);
  return Pack(a, b);
}

uint32_t checksum = 2166136261u;
void Hash(uint32_t x) {
  checksum ^= x;
  checksum *= 16777619u;
}

int failures = 0;

const int32_t kGuardWords = 16;
const int32_t kGuard = 0x5a5aa5a5;

struct Buf {   // int32 words with guards
  int32_t w[kGuardWords + 160 + kGuardWords];
  void Fill(int32_t words) {
    for (int32_t i = 0; i < kGuardWords + 160 + kGuardWords; ++i) w[i] = kGuard;
    for (int32_t i = 0; i < words; ++i) w[kGuardWords + i] = RandomWord();
  }
  int32_t* p() { return w + kGuardWords; }
  bool Guarded(int32_t words) const {
    for (int32_t i = 0; i < kGuardWords; ++i) {
      if (w[i] != kGuard) return false;
    }
    for (int32_t i = kGuardWords + words; i < kGuardWords + 160 + kGuardWords; ++i) {
      if (w[i] != kGuard) return false;
    }
    return true;
  }
};

struct Buf16 {
  int16_t w[2 * kGuardWords + 64 + 2 * kGuardWords];
  void Fill(int32_t n) {
    for (int32_t i = 0; i < 2 * kGuardWords + 64 + 2 * kGuardWords; ++i) w[i] = 0x5a5a;
    for (int32_t i = 0; i < n; ++i) w[2 * kGuardWords + i] = RandomSample();
  }
  int16_t* p() { return w + 2 * kGuardWords; }
  bool Guarded(int32_t n) const {
    for (int32_t i = 0; i < 2 * kGuardWords; ++i) {
      if (w[i] != 0x5a5a) return false;
    }
    for (int32_t i = 2 * kGuardWords + n; i < 2 * kGuardWords + 64 + 2 * kGuardWords; ++i) {
      if (w[i] != 0x5a5a) return false;
    }
    return true;
  }
};

void Report(const char* name, int trials, int bad, bool guards) {
#ifdef __bfin__
  printf("%-14s %s (%d runs, asm = C), guards %s\n", name, bad ? "FAIL" : "ok", trials,
         guards ? "intact" : "HIT");
  if (bad || !guards) ++failures;
#else
  (void)bad;
  (void)guards;
  printf("%-14s C only (%d runs)\n", name, trials);
#endif
}

// --- Kernels ------------------------------------------------------------

void TestInterp() {
  int bad = 0;
  bool guards = true;
  const int trials = 1500;
  for (int t = 0; t < trials; ++t) {
    const int32_t n = Range(1, 64);
    Buf xc, xa, yc, ya;
    Buf16 in0, in1;
    xc.Fill(op1fx::kDriveXHistory + n);
    memcpy(xa.w, xc.w, sizeof(xc.w));
    yc.Fill(0);
    ya.Fill(0);
    in0.Fill(n);
    in1.Fill(n);
    op1fx::DriveInterpKernel k;
    k.beta = RandomWord();
    k.dbeta = (t & 1) ? RangePair(-300, 300, -300, 300) : RandomWord();
    op1fx::DriveInterpKernel kc = k, ka = k;
    (void)ka;
    op1fx::DriveInterpC(&kc, in0.p(), in1.p(), xc.p(), yc.p(), n);
#ifdef __bfin__
    op1fx::DriveInterpAsm(&ka, in0.p(), in1.p(), xa.p(), ya.p(), n);
    const bool ok = kc.beta == ka.beta && memcmp(xc.w, xa.w, sizeof(xc.w)) == 0 &&
                    memcmp(yc.w, ya.w, sizeof(yc.w)) == 0;
    if (!ok && bad++ < 4) {
      printf("interp: MISMATCH trial %d n %ld beta %08lx %08lx\n", t, static_cast<long>(n),
             static_cast<unsigned long>(kc.beta), static_cast<unsigned long>(ka.beta));
      for (int32_t i = 0; i < 2 * n && i < 4; ++i) {
        printf("  ys[%ld] c %08lx asm %08lx\n", static_cast<long>(i),
               static_cast<unsigned long>(yc.p()[i]), static_cast<unsigned long>(ya.p()[i]));
      }
    }
    guards = guards && xa.Guarded(op1fx::kDriveXHistory + n) && ya.Guarded(2 * n) &&
             in0.Guarded(n) && in1.Guarded(n);
#endif
    guards = guards && xc.Guarded(op1fx::kDriveXHistory + n) && yc.Guarded(2 * n);
    for (int32_t i = 0; i < 2 * n; ++i) Hash(static_cast<uint32_t>(yc.p()[i]));
  }
  Report("interp", trials, bad, guards);
}

void TestShape() {
  int bad = 0;
  bool guards = true;
  const int trials = 1500;
  for (int t = 0; t < trials; ++t) {
    const int32_t words = Range(1, 128);
    Buf yc, ya;
    yc.Fill(words);
    memcpy(ya.w, yc.w, sizeof(yc.w));
    op1fx::DriveShapeKernel k;
    k.gain = (t & 1) ? RangePair(0, 32767, 0, 32767) : RandomWord();
    k.dgain = (t & 2) ? RangePair(-200, 200, -200, 200) : RandomWord();
    k.shift = Range(0, 7);
    op1fx::DriveShapeKernel kc = k, ka = k;
    (void)ka;
    op1fx::DriveShapeC(&kc, yc.p(), words);
#ifdef __bfin__
    op1fx::DriveShapeAsm(&ka, ya.p(), words);
    const bool ok = kc.gain == ka.gain && memcmp(yc.w, ya.w, sizeof(yc.w)) == 0;
    if (!ok && bad++ < 4) {
      printf("shape: MISMATCH trial %d words %ld shift %ld gain %08lx %08lx\n", t,
             static_cast<long>(words), static_cast<long>(k.shift),
             static_cast<unsigned long>(kc.gain), static_cast<unsigned long>(ka.gain));
      for (int32_t i = 0; i < words && i < 4; ++i) {
        printf("  y[%ld] c %08lx asm %08lx\n", static_cast<long>(i),
               static_cast<unsigned long>(yc.p()[i]), static_cast<unsigned long>(ya.p()[i]));
      }
    }
    guards = guards && ya.Guarded(words);
#endif
    guards = guards && yc.Guarded(words);
    for (int32_t i = 0; i < words; ++i) Hash(static_cast<uint32_t>(yc.p()[i]));
  }
  Report("shape", trials, bad, guards);
}

void TestDecim() {
  int bad = 0;
  bool guards = true;
  const int trials = 1500;
  for (int t = 0; t < trials; ++t) {
    const int32_t n = Range(1, 64);
    Buf ys;
    ys.Fill(op1fx::kDriveYHistory + 2 * n);
    Buf oc, oa;
    oc.Fill(0);
    oa.Fill(0);
    op1fx::DriveDecimC(ys.p(), oc.p(), n);
#ifdef __bfin__
    op1fx::DriveDecimAsm(ys.p(), oa.p(), n);
    const bool ok = memcmp(oc.w, oa.w, sizeof(oc.w)) == 0;
    if (!ok && bad++ < 4) {
      printf("decim: MISMATCH trial %d n %ld\n", t, static_cast<long>(n));
      for (int32_t i = 0; i < n && i < 4; ++i) {
        printf("  [%ld] c %08lx asm %08lx\n", static_cast<long>(i),
               static_cast<unsigned long>(oc.p()[i]), static_cast<unsigned long>(oa.p()[i]));
      }
    }
    guards = guards && oa.Guarded(n) && ys.Guarded(op1fx::kDriveYHistory + 2 * n);
#endif
    guards = guards && oc.Guarded(n);
    for (int32_t i = 0; i < n; ++i) Hash(static_cast<uint32_t>(oc.p()[i]));
  }
  Report("decim", trials, bad, guards);
}

void TestTone() {
  int bad = 0;
  bool guards = true;
  const int trials = 1500;
  for (int t = 0; t < trials; ++t) {
    const int32_t n = Range(1, 64);
    Buf wc, wa;
    wc.Fill(n);
    memcpy(wa.w, wc.w, sizeof(wc.w));
    op1fx::DriveToneKernel k;
    k.fb = RandomWord();
    k.dfb = (t & 1) ? RangePair(-300, 300, -300, 300) : RandomWord();
    k.lp = RandomWord();
    k.tone = RandomWord();
    k.dtone = (t & 2) ? RangePair(-300, 300, -300, 300) : RandomWord();
    op1fx::DriveToneKernel kc = k, ka = k;
    (void)ka;
    op1fx::DriveToneC(&kc, wc.p(), n);
#ifdef __bfin__
    op1fx::DriveToneAsm(&ka, wa.p(), n);
    const bool ok = kc.fb == ka.fb && kc.lp == ka.lp && kc.tone == ka.tone &&
                    memcmp(wc.w, wa.w, sizeof(wc.w)) == 0;
    if (!ok && bad++ < 4) {
      printf("tone: MISMATCH trial %d n %ld lp %08lx %08lx\n", t, static_cast<long>(n),
             static_cast<unsigned long>(kc.lp), static_cast<unsigned long>(ka.lp));
      for (int32_t i = 0; i < n && i < 4; ++i) {
        printf("  w[%ld] c %08lx asm %08lx\n", static_cast<long>(i),
               static_cast<unsigned long>(wc.p()[i]), static_cast<unsigned long>(wa.p()[i]));
      }
    }
    guards = guards && wa.Guarded(n);
#endif
    guards = guards && wc.Guarded(n);
    for (int32_t i = 0; i < n; ++i) Hash(static_cast<uint32_t>(wc.p()[i]));
    Hash(static_cast<uint32_t>(kc.lp));
  }
  Report("tone", trials, bad, guards);
}

void TestDc() {
  int bad = 0;
  bool guards = true;
  const int trials = 1500;
  for (int t = 0; t < trials; ++t) {
    const int32_t n = Range(1, 64);
    Buf wc, wa;
    wc.Fill(n);
    memcpy(wa.w, wc.w, sizeof(wc.w));
    op1fx::DriveDcKernel k;
    // The low-pass stays inside [-32768.5, 32767.5] x 2^16 in use.
    k.acc0 = static_cast<int32_t>(Next()) >> Range(0, 12);
    k.acc1 = static_cast<int32_t>(Next()) >> Range(0, 12);
    if (k.acc0 < -2147450880) k.acc0 = -2147450880;
    if (k.acc1 < -2147450880) k.acc1 = -2147450880;
    if (k.acc0 > 2147450880) k.acc0 = 2147450880;
    if (k.acc1 > 2147450880) k.acc1 = 2147450880;
    op1fx::DriveDcKernel kc = k, ka = k;
    (void)ka;
    op1fx::DriveDcC(&kc, wc.p(), n);
#ifdef __bfin__
    op1fx::DriveDcAsm(&ka, wa.p(), n);
    const bool ok = kc.acc0 == ka.acc0 && kc.acc1 == ka.acc1 &&
                    memcmp(wc.w, wa.w, sizeof(wc.w)) == 0;
    if (!ok && bad++ < 4) {
      printf("dc: MISMATCH trial %d n %ld acc %08lx %08lx / %08lx %08lx\n", t,
             static_cast<long>(n), static_cast<unsigned long>(kc.acc0),
             static_cast<unsigned long>(kc.acc1), static_cast<unsigned long>(ka.acc0),
             static_cast<unsigned long>(ka.acc1));
      for (int32_t i = 0; i < n && i < 4; ++i) {
        printf("  w[%ld] c %08lx asm %08lx\n", static_cast<long>(i),
               static_cast<unsigned long>(wc.p()[i]), static_cast<unsigned long>(wa.p()[i]));
      }
    }
    guards = guards && wa.Guarded(n);
#endif
    guards = guards && wc.Guarded(n);
    for (int32_t i = 0; i < n; ++i) Hash(static_cast<uint32_t>(wc.p()[i]));
    Hash(static_cast<uint32_t>(kc.acc0));
  }
  Report("dc", trials, bad, guards);
}

void TestMix() {
  int bad = 0;
  bool guards = true;
  const int trials = 1500;
  for (int t = 0; t < trials; ++t) {
    const int32_t n = Range(1, 64);
    Buf w;
    w.Fill(n);
    Buf16 dry0, dry1, c0, c1, a0, a1;
    dry0.Fill(n);
    dry1.Fill(n);
    c0.Fill(0);
    c1.Fill(0);
    a0.Fill(0);
    a1.Fill(0);
    op1fx::DriveMixKernel k;
    k.mix = (t & 1) ? RangePair(0, 16384, 0, 32767) : RandomWord();
    k.dmix = (t & 2) ? RangePair(-200, 200, -200, 200) : RandomWord();
    op1fx::DriveMixKernel kc = k, ka = k;
    (void)ka;
    op1fx::DriveMixC(&kc, w.p(), dry0.p(), dry1.p(), c0.p(), c1.p(), n);
#ifdef __bfin__
    op1fx::DriveMixAsm(&ka, w.p(), dry0.p(), dry1.p(), a0.p(), a1.p(), n);
    const bool ok = kc.mix == ka.mix && memcmp(c0.w, a0.w, sizeof(c0.w)) == 0 &&
                    memcmp(c1.w, a1.w, sizeof(c1.w)) == 0;
    if (!ok && bad++ < 4) {
      printf("mix: MISMATCH trial %d n %ld\n", t, static_cast<long>(n));
      for (int32_t i = 0; i < n && i < 4; ++i) {
        printf("  [%ld] c %d %d asm %d %d\n", static_cast<long>(i), c0.p()[i], c1.p()[i],
               a0.p()[i], a1.p()[i]);
      }
    }
    guards = guards && a0.Guarded(n) && a1.Guarded(n) && w.Guarded(n);
    // In place (out = dry), as Process calls it, and mono (dry1 = dry0).
    Buf16 ip0, ip1;
    memcpy(ip0.w, dry0.w, sizeof(ip0.w));
    memcpy(ip1.w, dry1.w, sizeof(ip1.w));
    op1fx::DriveMixKernel ki = k;
    op1fx::DriveMixAsm(&ki, w.p(), ip0.p(), ip1.p(), ip0.p(), ip1.p(), n);
    if (memcmp(ip0.p(), c0.p(), 2 * n) != 0 || memcmp(ip1.p(), c1.p(), 2 * n) != 0) {
      if (bad++ < 4) printf("mix: in-place MISMATCH trial %d\n", t);
    }
#endif
    guards = guards && c0.Guarded(n) && c1.Guarded(n);
    for (int32_t i = 0; i < n; ++i) Hash(Pack(c0.p()[i], c1.p()[i]));
  }
  Report("mix", trials, bad, guards);
}

void TestMonoWet() {
  int bad = 0;
  bool guards = true;
  const int trials = 1500;
  for (int t = 0; t < trials; ++t) {
    const int32_t runs = (t & 16) ? 2 : 1;
    const int32_t n = runs == 2 ? 2 * Range(1, 32) : Range(1, 64);
    Buf y;
    y.Fill(n / runs);
    Buf16 ioc, ioa;
    ioc.Fill(n);
    memcpy(ioa.w, ioc.w, sizeof(ioc.w));
    op1fx::DriveToneKernel tk;
    const int32_t fb = RandomSample();
    tk.fb = Pack(fb, fb);
    const int32_t dfb = (t & 1) ? Range(-300, 300) : RandomSample();
    tk.dfb = Pack(dfb, dfb);
    tk.lp = RandomWord();
    tk.tone = RandomWord();
    tk.dtone = (t & 2) ? RangePair(-300, 300, -300, 300) : RandomWord();
    op1fx::DriveDcKernel dk;
    dk.acc0 = static_cast<int32_t>(Next()) >> Range(0, 12);
    if (dk.acc0 < -2147450880) dk.acc0 = -2147450880;
    if (dk.acc0 > 2147450880) dk.acc0 = 2147450880;
    dk.acc1 = 0;
    op1fx::DriveMixKernel mk;
    mk.mix = (t & 4) ? RangePair(0, 16384, 0, 32767) : RandomWord();
    mk.dmix = (t & 8) ? RangePair(-200, 200, -200, 200) : RandomWord();
    op1fx::DriveToneKernel tc = tk, ta = tk;
    op1fx::DriveDcKernel dc = dk, da = dk;
    op1fx::DriveMixKernel mc = mk, ma = mk;
    const op1fx::DriveMonoWetKernel kc = { &tc, &dc, &mc };
    const op1fx::DriveMonoWetKernel ka = { &ta, &da, &ma };
    (void)ka;
    op1fx::DriveMonoWetC(&kc, y.p(), ioc.p(), n, runs);
#ifdef __bfin__
    op1fx::DriveMonoWetAsm(&ka, y.p(), ioa.p(), n, runs);
    const bool ok = memcmp(&tc, &ta, sizeof(tc)) == 0 && memcmp(&dc, &da, sizeof(dc)) == 0 &&
                    memcmp(&mc, &ma, sizeof(mc)) == 0 && memcmp(ioc.w, ioa.w, sizeof(ioc.w)) == 0;
    if (!ok && bad++ < 4) {
      printf("mono wet: MISMATCH trial %d n %ld acc %08lx %08lx lp %08lx %08lx\n", t,
             static_cast<long>(n), static_cast<unsigned long>(dc.acc0),
             static_cast<unsigned long>(da.acc0), static_cast<unsigned long>(tc.lp),
             static_cast<unsigned long>(ta.lp));
      for (int32_t i = 0; i < n && i < 4; ++i) {
        printf("  [%ld] c %d asm %d\n", static_cast<long>(i), ioc.p()[i], ioa.p()[i]);
      }
    }
    guards = guards && ioa.Guarded(n) && y.Guarded(n / runs);
#endif
    guards = guards && ioc.Guarded(n);
    for (int32_t i = 0; i < n; ++i) Hash(static_cast<uint16_t>(ioc.p()[i]));
    Hash(static_cast<uint32_t>(dc.acc0));
  }
  Report("mono wet", trials, bad, guards);
}

// --- The effect ------------------------------------------------------------

struct Source {
  uint32_t phase, inc, seed;
  void Init(uint32_t f) {
    phase = 0;
    inc = f;
    seed = 777;
  }
  // A parabolic sine (integer), amplitude a.
  int16_t Tone(int32_t a) {
    phase += inc;
    const int32_t x = static_cast<int32_t>(phase) >> 16;   // Q15
    const int32_t m = x < 0 ? -x : x;
    const int32_t y = (x * (32768 - m)) >> 13;           // 4 x (1 - |x|), Q15
    return static_cast<int16_t>((y * a) >> 15);
  }
  int16_t Noise(int32_t a) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<int16_t>((static_cast<int32_t>(seed) >> 16) * a >> 15);
  }
};

Drive fx_c, fx_a;

DriveParams RandomParams() {
  DriveParams p;
  p.drive = Range(0, 32767);
  p.tone = Range(0, 32767);
  p.bias = Range(0, 32767);
  p.mix = (Next() & 3) == 0 ? 32767 : Range(0, 32767);
  return p;
}

// mode 0 stereo n = 64, 1 mono n = 64, 2 stereo odd n, 3 mono odd n (some < 18).
void Scenario(const char* name, int mode, int calls) {
  fx_c.Init();
  fx_c.set_use_asm(false);
#ifdef __bfin__
  fx_a.Init();
  fx_a.set_use_asm(true);
#endif
  Source src;
  src.Init(0x01234567);
  DriveParams p = RandomParams();
  int bad = 0;
  uint32_t sum = 2166136261u;
  for (int call = 0; call < calls; ++call) {
    if (call % 37 == 0) p = RandomParams();
    if (call % 5 == 0) p.drive = Range(0, 32767);
    const bool mono = mode & 1;
    const int32_t n = mode >= 2 ? 2 * Range(1, 32) : 64;
    int16_t lc[64], rc[64], la[64], ra[64];
    for (int32_t i = 0; i < n; ++i) {
      const int32_t a = (call / 200) % 3 == 0 ? 32767 : 9000;
      lc[i] = static_cast<int16_t>((src.Tone(a) + src.Noise(a >> 2)) >> 1);
      rc[i] = (call & 64) ? RandomSample() : src.Noise(a);
    }
    memcpy(la, lc, sizeof(lc));
    memcpy(ra, rc, sizeof(rc));
    fx_c.Process(lc, mono ? nullptr : rc, n, p);
#ifdef __bfin__
    fx_a.Process(la, mono ? nullptr : ra, n, p);
    if (memcmp(lc, la, 2 * n) != 0 || (!mono && memcmp(rc, ra, 2 * n) != 0)) {
      if (bad++ < 4) {
        printf("effect %s: MISMATCH call %d n %ld\n", name, call, static_cast<long>(n));
        for (int32_t i = 0; i < n; ++i) {
          if (lc[i] != la[i] || (!mono && rc[i] != ra[i])) {
            printf("  first at [%ld]: c %d %d asm %d %d\n", static_cast<long>(i), lc[i],
                   rc[i], la[i], ra[i]);
            break;
          }
        }
      }
    }
#endif
    for (int32_t i = 0; i < n; ++i) {
      const uint32_t v = static_cast<uint16_t>(lc[i]) |
                         (mono ? 0u : static_cast<uint32_t>(static_cast<uint16_t>(rc[i])) << 16);
      sum ^= v;
      sum *= 16777619u;
    }
  }
#ifdef __bfin__
  printf("effect %-26s %s (%d calls, asm = C)  checksum %08lx\n", name, bad ? "FAIL" : "ok", calls,
         static_cast<unsigned long>(sum));
  if (bad) ++failures;
#else
  printf("effect %-26s C only (%d calls)  checksum %08lx\n", name, calls,
         static_cast<unsigned long>(sum));
#endif
}

// Mix 0: the output is the input, bit for bit.
void Identity(bool mono) {
  Drive& fx = fx_c;
  fx.Init();
#ifdef __bfin__
  fx.set_use_asm(true);
#endif
  Source src;
  src.Init(0x00f00000);
  int bad = 0;
  int checked = 0;
  for (int call = 0; call < 1500; ++call) {
    DriveParams p = RandomParams();
    // 0..399 mix 0; 400..599 mix up; 600..1499 mix 0 again (settled after ~40 calls).
    p.mix = (call >= 400 && call < 600) ? 25000 : 0;
    const int32_t n = 2 * Range(1, 32);
    int16_t l[64], r[64], l0[64], r0[64];
    for (int32_t i = 0; i < n; ++i) {
      l[i] = (call & 1) ? RandomSample() : src.Tone(32767);
      r[i] = RandomSample();
    }
    memcpy(l0, l, sizeof(l));
    memcpy(r0, r, sizeof(r));
    fx.Process(l, mono ? nullptr : r, n, p);
    if (call < 400 || call >= 700) {
      ++checked;
      if (memcmp(l, l0, 2 * n) != 0 || (!mono && memcmp(r, r0, 2 * n) != 0)) ++bad;
    }
  }
  printf("identity %-6s mix 0 -> input: %s (%d calls checked, %d differ)\n", mono ? "mono" : "stereo",
         bad ? "FAIL" : "ok", checked, bad);
  if (bad) ++failures;
}

#ifdef __bfin__
void Cycles(const char* name, bool use_asm, bool mono, int moving) {   // moving: 0 still, 1 drive (an LFO), 2 drive, bias, tone
  Drive& fx = fx_a;
  fx.Init();
  fx.set_use_asm(use_asm);
  Source src;
  src.Init(0x00a00000);
  DriveParams p;
  p.drive = 20000;
  p.tone = 22000;
  p.bias = 12000;
  p.mix = 32767;
  harness::CycleStats stats;
  for (int call = 0; call < 600; ++call) {
    int16_t l[64], r[64];
    for (int32_t i = 0; i < 64; ++i) {
      l[i] = src.Tone(16000);
      r[i] = src.Noise(16000);
    }
    if (moving == 1) {
      const int32_t t = (call * 600) & 65535;   // a triangle, ~6 Hz
      p.drive = t < 32768 ? t : 65535 - t;
    } else if (moving == 2) {
      p.drive = (call * 300) & 32767;
      p.bias = (call * 170) & 32767;
      p.tone = (call * 90) & 32767;
    }
    const uint64_t t0 = harness::ReadCycles();
    fx.Process(l, mono ? nullptr : r, 64, p);
    const uint64_t t1 = harness::ReadCycles();
    if (call >= 50) stats.Add(t1 - t0);
  }
  const unsigned long avg = static_cast<unsigned long>(stats.total / stats.n);
  // 689.0625 calls per second (44100 / 64).
  const unsigned long khz_avg = static_cast<unsigned long>(stats.total * 689 / stats.n / 1000);
  const unsigned long khz_max = static_cast<unsigned long>(stats.max * 689 / 1000);
  printf("cycles %-24s per call avg %5lu min %5lu max %5lu  per sample %3lu  => %lu.%02lu MHz avg, "
         "%lu.%02lu MHz peak\n",
         name, avg, static_cast<unsigned long>(stats.min), static_cast<unsigned long>(stats.max),
         avg / 64, khz_avg / 1000, (khz_avg % 1000) / 10, khz_max / 1000, (khz_max % 1000) / 10);
}
#endif

// --- Analysis (host) ----------------------------------------------------------

#ifndef __bfin__

const int32_t kN = 32768;          // analysis length (a power of two)
const double kFs = 44100.0;
double sig[kN];
double re[kN], im[kN];

// Renders a sine on FFT bin `bin` (peak amplitude a, full scale 1) through
// the effect for `settle` samples, then kN more into sig[] (normalised).
void Render(Drive& fx, const DriveParams& p, int32_t bin, double a, bool mono, double settle_s) {
  const int32_t settle = static_cast<int32_t>(settle_s * kFs) & ~63;
  const double w = 2.0 * M_PI * bin / kN;
  int64_t t = 0;
  for (int32_t pos = 0; pos < settle + kN; pos += 64) {
    int16_t l[64], r[64];
    for (int32_t i = 0; i < 64; ++i, ++t) {
      const double v = a * 32767.0 * sin(w * static_cast<double>(t));
      l[i] = r[i] = static_cast<int16_t>(lrint(v));
    }
    fx.Process(l, mono ? nullptr : r, 64, p);
    if (pos >= settle) {
      for (int32_t i = 0; i < 64; ++i) sig[pos - settle + i] = l[i] / 32768.0;
    }
  }
}

void Fft() {
  for (int32_t i = 0; i < kN; ++i) {
    re[i] = sig[i];
    im[i] = 0;
  }
  for (int32_t i = 1, j = 0; i < kN; ++i) {
    int32_t bit = kN >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      double tr = re[i]; re[i] = re[j]; re[j] = tr;
      double ti = im[i]; im[i] = im[j]; im[j] = ti;
    }
  }
  for (int32_t len = 2; len <= kN; len <<= 1) {
    const double ang = -2.0 * M_PI / len;
    for (int32_t i = 0; i < kN; i += len) {
      for (int32_t k = 0; k < len / 2; ++k) {
        const double c = cos(ang * k), s = sin(ang * k);
        const double ur = re[i + k], ui = im[i + k];
        const double vr = re[i + k + len / 2] * c - im[i + k + len / 2] * s;
        const double vi = re[i + k + len / 2] * s + im[i + k + len / 2] * c;
        re[i + k] = ur + vr;
        im[i + k] = ui + vi;
        re[i + k + len / 2] = ur - vr;
        im[i + k + len / 2] = ui - vi;
      }
    }
  }
}

// Power of a bin (one-sided), from the last Fft().
double BinPower(int32_t b) {
  b %= kN;
  if (b < 0) b += kN;
  if (b > kN / 2) b = kN - b;
  const double p = (re[b] * re[b] + im[b] * im[b]) / (static_cast<double>(kN) * kN);
  return (b == 0 || b == kN / 2) ? p : 2 * p;
}
double Db(double p) { return 10.0 * log10(p > 1e-30 ? p : 1e-30); }
int32_t Fold(int64_t b) {
  b %= kN;
  if (b > kN / 2) b = kN - b;
  return static_cast<int32_t>(b);
}

double Rms() {
  double s = 0, s2 = 0;
  for (int32_t i = 0; i < kN; ++i) {
    s += sig[i];
    s2 += sig[i] * sig[i];
  }
  const double m = s / kN;
  return sqrt(s2 / kN - m * m);
}
double Mean() {
  double s = 0;
  for (int32_t i = 0; i < kN; ++i) s += sig[i];
  return s / kN;
}

DriveParams Knobs(double drive_db, int32_t tone, int32_t bias, int32_t mix) {
  DriveParams p;
  p.drive = static_cast<int32_t>(lrint(drive_db / 36.0 * 32767.0));
  p.tone = tone;
  p.bias = bias;
  p.mix = mix;
  return p;
}

void Harmonics() {
  const int32_t bin = 744;   // 1001.3 Hz
  printf("harmonics: 1 kHz sine at -12 dBFS, wet only, tone flat (dBc)\n");
  printf("  %-26s %7s %7s %7s %7s %7s\n", "", "H2", "H3", "H4", "H5", "H7");
  struct Case {
    const char* name;
    double db;
    int32_t bias;
  } cases[] = {
    { "drive 0 dB, bias 0", 0, 0 },
    { "drive 12 dB, bias 0", 12, 0 },
    { "drive 12 dB, bias half", 12, 16384 },
    { "drive 12 dB, bias max", 12, 32767 },
    { "drive 24 dB, bias 0", 24, 0 },
    { "drive 24 dB, bias max", 24, 32767 },
    { "drive 36 dB, bias 0", 36, 0 },
    { "drive 36 dB, bias max", 36, 32767 },
  };
  double h2_sym = -1000, h3_sym = -1000, h2_bias = 0;
  for (const Case& c : cases) {
    Drive fx;
    fx.Init();
    Render(fx, Knobs(c.db, 16384, c.bias, 32767), bin, 0.25, false, 0.5);
    Fft();
    const double p1 = BinPower(bin);
    double h[8];
    for (int k = 2; k < 8; ++k) h[k] = Db(BinPower(Fold(static_cast<int64_t>(bin) * k)) / p1);
    printf("  %-26s %7.1f %7.1f %7.1f %7.1f %7.1f\n", c.name, h[2], h[3], h[4], h[5], h[7]);
    if (c.bias == 0 && c.db >= 12) {
      if (h[2] > h2_sym) h2_sym = h[2];
      if (h[4] > h2_sym) h2_sym = h[4];
      if (c.db == 12) h3_sym = h[3];
    }
    if (c.bias == 32767 && c.db == 12) h2_bias = h[2];
  }
  const bool ok = h2_sym < -70 && h3_sym > -40 && h2_bias > -35;
  printf("harmonics: bias 0 even <= %.1f dBc (want < -70), H3 at 12 dB %.1f (want > -40), "
         "bias max H2 at 12 dB %.1f (want > -35): %s\n",
         h2_sym, h3_sym, h2_bias, ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

void Level() {
  const int32_t bin = 744;
  printf("level: wet RMS / input RMS (dB), 1 kHz sine, tone flat, bias 0 / max\n");
  printf("  %-8s", "drive");
  const double levels[3] = { -24, -12, -6 };
  for (double l : levels) printf("  %4.0f dBFS b0  %4.0f b max", l, l);
  printf("\n");
  double worst = 0;
  for (int d = 0; d <= 36; d += 6) {
    printf("  %3d dB  ", d);
    for (double lv : levels) {
      for (int b = 0; b < 2; ++b) {
        Drive fx;
        fx.Init();
        const double a = pow(10.0, lv / 20.0);
        Render(fx, Knobs(d, 16384, b ? 32767 : 0, 32767), bin, a, false, 0.3);
        const double g = 20.0 * log10(Rms() / (a / sqrt(2.0)));
        printf("  %12.2f", g);
        if (lv == -12 && fabs(g) > worst) worst = fabs(g);
      }
    }
    printf("\n");
  }
  const bool ok = worst < 1.5;
  printf("level: at the reference (-12 dBFS) within +-%.2f dB across drive and bias (want < 1.5): %s\n",
         worst, ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

// Aliasing: the power on non-harmonic bins (folded harmonics above
// 22.05 kHz, and anything else) against the fundamental.
void Aliasing() {
  printf("aliasing: alias power / fundamental (dB), wet only, bias 0, tone flat\n");
  struct Case {
    int32_t bin;
    double a, db;
  } cases[] = {
    { 3715, 0.5, 12 }, { 3715, 0.5, 24 }, { 3715, 0.5, 36 },
    { 6501, 0.5, 12 }, { 6501, 0.5, 24 }, { 6501, 0.5, 36 },
    { 9001, 0.25, 24 },
  };
  bool ok = true;
  for (const Case& c : cases) {
    double res[2];
    for (int os = 0; os < 2; ++os) {
      Drive fx;
      fx.Init();
      fx.set_oversampling(os == 1);
      Render(fx, Knobs(c.db, 16384, 0, 32767), c.bin, c.a, false, 0.3);
      Fft();
      const double p1 = BinPower(c.bin);
      // Total power except DC, minus the true harmonics (k bin < N / 2).
      double total = 0;
      for (int32_t b = 1; b <= kN / 2; ++b) total += BinPower(b);
      double harm = 0;
      for (int64_t k = 1; k * c.bin < kN / 2; ++k) harm += BinPower(static_cast<int32_t>(k * c.bin));
      res[os] = Db((total - harm) / p1);
    }
    printf("  %6.0f Hz %5.1f dBFS drive %2.0f dB: no oversampling %6.1f, 2x %6.1f  (%5.1f dB less)\n",
           c.bin * kFs / kN, 20 * log10(c.a), c.db, res[0], res[1], res[0] - res[1]);
    if (res[0] - res[1] < 3) ok = false;
  }
  printf("aliasing: 2x below no oversampling in every case: %s\n", ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

void DcTest() {
  printf("dc: output mean (LSB), wet only\n");
  struct Case {
    const char* name;
    double db;
    int32_t bias;
    int32_t bin;
    double a;
  } cases[] = {
    { "200 Hz -6 dBFS, 12 dB, bias max", 12, 32767, 149, 0.5 },
    { "200 Hz -6 dBFS, 36 dB, bias max", 36, 32767, 149, 0.5 },
    { "1 kHz -20 dBFS, 0 dB, bias max", 0, 32767, 744, 0.1 },
    { "1 kHz -6 dBFS, 24 dB, bias half", 24, 16384, 744, 0.5 },
  };
  bool ok = true;
  for (const Case& c : cases) {
    Drive fx;
    fx.Init();
    Render(fx, Knobs(c.db, 16384, c.bias, 32767), c.bin, c.a, false, 1.0);
    const double m = Mean() * 32768.0;
    printf("  %-34s %7.2f\n", c.name, m);
    if (fabs(m) > 2) ok = false;
  }
  // Silence in, bias max, every drive: nothing out.
  int32_t worst = 0;
  for (int d = 0; d <= 36; d += 12) {
    Drive fx;
    fx.Init();
    for (int call = 0; call < 2000; ++call) {
      int16_t l[64] = {}, r[64] = {};
      fx.Process(l, r, 64, Knobs(d, 0, 32767, 32767));
      if (call > 1000) {
        for (int i = 0; i < 64; ++i) {
          if (abs(l[i]) > worst) worst = abs(l[i]);
        }
      }
    }
  }
  printf("  silence in, bias max: |out| <= %d LSB after 1.5 s\n", worst);
  if (worst > 1) ok = false;
  printf("dc: %s\n", ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

void ToneCheck() {
  printf("tone: gain at 100 Hz / 1 kHz / 8 kHz (dB), drive 0, -24 dBFS, bias 0\n");
  const int32_t bins[3] = { 74, 743, 5944 };
  const int32_t tones[5] = { 0, 8192, 16384, 24576, 32767 };
  for (int32_t tone : tones) {
    printf("  tone %5d:", static_cast<int>(tone));
    for (int32_t b : bins) {
      Drive fx;
      fx.Init();
      Render(fx, Knobs(0, tone, 0, 32767), b, 0.063, false, 0.3);
      printf(" %7.2f", 20.0 * log10(Rms() / (0.063 / sqrt(2.0))));
    }
    printf("\n");
  }
}

#endif  // !__bfin__

}  // namespace

int main() {
  harness::EnableCycleCounter();
  TestInterp();
  TestShape();
  TestDecim();
  TestTone();
  TestDc();
  TestMix();
  TestMonoWet();
  printf("kernels checksum %08lx\n", static_cast<unsigned long>(checksum));
  printf("sizeof(Drive) %lu bytes\n", static_cast<unsigned long>(sizeof(Drive)));

  Scenario("stereo, n 64", 0, 1500);
  Scenario("mono (split), n 64", 1, 1500);
  Scenario("stereo, n 2..64", 2, 1500);
  Scenario("mono, n 2..64", 3, 1500);
  Identity(false);
  Identity(true);

#ifdef __bfin__
  Cycles("stereo, knobs still", true, false, 0);
  Cycles("stereo, drive LFO", true, false, 1);
  Cycles("stereo, 3 knobs moving", true, false, 2);
  Cycles("mono, knobs still", true, true, 0);
  Cycles("mono, drive LFO", true, true, 1);
  Cycles("mono, 3 knobs moving", true, true, 2);
  Cycles("C stereo", false, false, 0);
  Cycles("C mono", false, true, 0);
#else
  Harmonics();
  Level();
  Aliasing();
  DcTest();
  ToneCheck();
#endif

  printf("%s: %d failure(s)\n", failures ? "FAIL" : "ok", failures);
  return failures ? 1 : 0;
}
