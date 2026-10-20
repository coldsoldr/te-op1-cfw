// FOLD's checks (make test-fold), built for the Blackfin simulator and for
// the host:
//   kernels   each assembly kernel against its C reference on random runs
//             (random states, ramps and full-scale input, n 1..64), with
//             guard words (Blackfin);
//   effect    a C instance and an assembly instance through scenarios
//             (knob jumps, LFO-rate sweeps, mono, n 2..64): outputs and
//             states equal (Blackfin); the C output's checksums, which the
//             host build prints too (the Makefile compares them);
//   identity  mix 0 returns the input bit-exactly (from Init, and after a
//             ramp down from full wet);
//   dc        the output's DC with the symmetry at its ends, with the
//             symmetry swept, and with silence;
//   level     wet RMS / dry RMS over the fold knob;
//   aliasing  (host) a high sine through the ADAA folder against a naive
//             one (the same folder without ADAA) and, for context, a
//             double-precision 2x-oversampled ADAA folder;
//   cycles    per 64-sample call, stereo and mono, assembly and C
//             (Blackfin; 689 calls/s).
//
// Integer only on the Blackfin (float is soft-float); the aliasing
// measurement is host-only.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "rings/harness/cycles.h"
#include "src/fold/fold.h"
#include "src/fold/fold_tables.h"

#ifndef __bfin__
#include <cmath>
#endif

using op1fx::Fold;
using op1fx::FoldChannel;
using op1fx::FoldParams;
using op1fx::FoldShapeBlock;
using op1fx::FoldToneBlock;

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

// Sine from a Q32 phase accumulator, integer only (the folder's own table).
struct Osc {
  uint32_t phase, inc;
  void Init(uint32_t i) { phase = 0; inc = i; }
  int16_t Next(int32_t amp) {   // amp Q15
    const int32_t s = op1fx::FoldSinQ15(phase);
    phase += inc;
    return static_cast<int16_t>((s * amp) >> 15);
  }
};
// Hz -> Q32 phase increment at 44.1 kHz (97391.5 per Hz).
uint32_t Inc(int32_t hz) { return static_cast<uint32_t>(static_cast<uint64_t>(hz) * 4294967296ull / 44100); }

// --- Kernels (Blackfin) ----------------------------------------------------

#ifdef __bfin__
const int32_t kGuardWords = 16;
const int16_t kGuard = 0x5a5a;

void RandomChannel(FoldChannel* ch) {
  ch->u_prev = Range(-(4 << 27), 4 << 27);
  ch->t1 = static_cast<int16_t>(Range(-16384, 16383));
  ch->t2 = static_cast<int16_t>(Range(-16384, 16383));
  ch->z = Range(-16384, 16383) * 65536 + Range(0, 65535);
}

void RandomTone(FoldToneBlock* tb) {
  tb->sc = Range(-11586, 11586) * 65536 + Range(0, 65535);
  tb->sc_step = Range(-30000, 30000);
  // The packed mix ramp as Control makes it: both halves in 0..32767 over
  // the run, each step towards its end.
  const int32_t lm0 = Range(0, 32767), lm1 = Range(0, 32767);
  const int32_t mm0 = Range(0, 32767), mm1 = Range(0, 32767);
  tb->mix = static_cast<int32_t>((static_cast<uint32_t>(lm0) << 16) | static_cast<uint32_t>(mm0));
  tb->mix_step = static_cast<int32_t>((static_cast<uint32_t>((lm1 - lm0) / 64) << 16) +
                                      static_cast<uint32_t>((mm1 - mm0) / 64));
  tb->coef = Range(1, 32767) | (Range(0, 32767) << 16);
}

void TestKernels() {
  const int32_t kBuf = kGuardWords + 128 + kGuardWords;
  alignas(4) int16_t x[64], wc[kBuf], wa[kBuf], lc[kBuf], la[kBuf], rc[kBuf], ra[kBuf];
  int32_t shape_bad = 0, tone_bad = 0, stereo_bad = 0, guards_bad = 0;
  const int32_t runs = 3000;
  for (int32_t run = 0; run < runs; ++run) {
    const int32_t n = Range(1, 32) * 2;
    const int32_t stride = Range(1, 2);
    FoldShapeBlock sb;
    sb.g = Range(1024, 16384) * 65536 + Range(0, 65535);
    sb.g_step = Range(-200000, 200000);
    sb.c = Range(-(1 << 24), 1 << 24);
    sb.c_step = Range(-20000, 20000);
    sb.sin_tab = op1fx::kFoldSin;
    sb.sinc_tab = op1fx::kFoldSinc;
    FoldChannel chc[2], cha[2];
    RandomChannel(&chc[0]);
    RandomChannel(&chc[1]);
    cha[0] = chc[0];
    cha[1] = chc[1];
    for (int32_t i = 0; i < n; ++i) x[i] = RandomSample();
    for (int32_t i = 0; i < kBuf; ++i) wc[i] = wa[i] = kGuard;
    op1fx::FoldShapeRunC(&sb, &chc[0], x, wc + kGuardWords, stride, n);
    op1fx::FoldShapeRunAsm(&sb, &cha[0], x, wa + kGuardWords, stride, n);
    if (memcmp(wc, wa, sizeof(wc)) || chc[0].u_prev != cha[0].u_prev) {
      if (!shape_bad) printf("  shape mismatch at run %ld (n %ld)\n", static_cast<long>(run), static_cast<long>(n));
      ++shape_bad;
    }
    for (int32_t i = 0; i < kGuardWords; ++i) {
      if (wa[i] != kGuard || wa[kGuardWords + 128 + i] != kGuard) ++guards_bad;
    }

    // Mono tone on the folded block (or random input, half the time).
    FoldToneBlock tb;
    RandomTone(&tb);
    if (run & 1) {
      for (int32_t i = 0; i < 2 * n; ++i) wc[kGuardWords + i] = RandomSample();
    } else if (stride == 2) {
      for (int32_t i = 0; i < n; ++i) wc[kGuardWords + i] = wc[kGuardWords + 2 * i];
    }
    for (int32_t i = 0; i < kBuf; ++i) lc[i] = la[i] = rc[i] = ra[i] = kGuard;
    for (int32_t i = 0; i < n; ++i) {
      lc[kGuardWords + i] = la[kGuardWords + i] = RandomSample();
      rc[kGuardWords + i] = ra[kGuardWords + i] = RandomSample();
    }
    op1fx::FoldToneRunC(&tb, &chc[0], wc + kGuardWords, lc + kGuardWords, n);
    op1fx::FoldToneRunAsm(&tb, &cha[0], wc + kGuardWords, la + kGuardWords, n);
    if (memcmp(lc, la, sizeof(lc)) || memcmp(&chc[0], &cha[0], sizeof(FoldChannel))) {
      if (!tone_bad) printf("  tone mismatch at run %ld (n %ld)\n", static_cast<long>(run), static_cast<long>(n));
      ++tone_bad;
    }

    // Stereo tone: interleaved input.
    RandomTone(&tb);
    op1fx::FoldToneStereoRunC(&tb, chc, wc + kGuardWords, lc + kGuardWords, rc + kGuardWords, n);
    op1fx::FoldToneStereoRunAsm(&tb, cha, wc + kGuardWords, la + kGuardWords, ra + kGuardWords, n);
    if (memcmp(lc, la, sizeof(lc)) || memcmp(rc, ra, sizeof(rc)) || memcmp(chc, cha, sizeof(chc))) {
      if (!stereo_bad) printf("  stereo tone mismatch at run %ld (n %ld)\n", static_cast<long>(run), static_cast<long>(n));
      ++stereo_bad;
    }
    for (int32_t i = 0; i < kGuardWords; ++i) {
      if (la[i] != kGuard || la[kGuardWords + n + i] != kGuard) ++guards_bad;
      if (ra[i] != kGuard || ra[kGuardWords + n + i] != kGuard) ++guards_bad;
    }
  }
  printf("kernels: %ld runs (n 2..64): shape differs in %ld, tone mono %ld, tone stereo %ld; guards %s\n",
         static_cast<long>(runs), static_cast<long>(shape_bad), static_cast<long>(tone_bad),
         static_cast<long>(stereo_bad), guards_bad ? "HIT" : "intact");
  if (shape_bad || tone_bad || stereo_bad || guards_bad) ++failures;
}
// Cycles of each assembly kernel alone, one 64-sample run (warm).
void KernelCycles() {
  alignas(4) static int16_t x[64], w[128], l[64], r[64];
  for (int32_t i = 0; i < 64; ++i) {
    x[i] = RandomSample();
    l[i] = RandomSample();
    r[i] = RandomSample();
  }
  FoldShapeBlock sb = { 4096 << 16, 100, 1 << 20, 10, op1fx::kFoldSin, op1fx::kFoldSinc };
  FoldToneBlock tb = { 0, 10, 0x40004000, 0, 20000 | (47 << 16) };
  FoldChannel ch[2] = {};
  uint64_t best[3] = { ~0ull, ~0ull, ~0ull };
  for (int32_t rep = 0; rep < 4; ++rep) {
    uint64_t t0 = harness::ReadCycles();
    op1fx::FoldShapeRunAsm(&sb, &ch[0], x, w, 1, 64);
    uint64_t t1 = harness::ReadCycles();
    op1fx::FoldToneRunAsm(&tb, &ch[0], w, l, 64);
    uint64_t t2 = harness::ReadCycles();
    op1fx::FoldToneStereoRunAsm(&tb, ch, w, l, r, 64);
    uint64_t t3 = harness::ReadCycles();
    if (t1 - t0 < best[0]) best[0] = t1 - t0;
    if (t2 - t1 < best[1]) best[1] = t2 - t1;
    if (t3 - t2 < best[2]) best[2] = t3 - t2;
  }
  printf("kernel cycles per 64 samples: shape %lu (one channel), tone mono %lu, tone stereo %lu (both)\n",
         static_cast<unsigned long>(best[0]), static_cast<unsigned long>(best[1]),
         static_cast<unsigned long>(best[2]));
}

#endif

// --- Effect: C against assembly, checksums ---------------------------------

Fold fx_c, fx_a;

void Scenario(const char* name, int32_t scenario, int32_t calls, bool mono) {
  fx_c.Init();
  fx_a.Init();
  fx_c.set_use_asm(false);
  fx_a.set_use_asm(true);
  rng = 777 + scenario;
  Osc a, b;
  a.Init(Inc(110 + 37 * scenario));
  b.Init(Inc(1730));
  FoldParams p = { 16384, 16384, 32767, 32767 };
  int32_t mismatch = -1;
  checksum = 2166136261u;
  for (int32_t call = 0; call < calls; ++call) {
    int32_t n = 64;
    if (scenario == 0 && call % 50 == 0) {   // knob jumps
      p.fold = Range(0, 32767);
      p.symmetry = Range(0, 32767);
      p.tone = Range(0, 32767);
      p.mix = Range(0, 32767);
    }
    if (scenario == 1) {   // LFO-rate sweeps: fold a triangle, symmetry the other way
      const int32_t t = (call * 600) & 65535;
      p.fold = t < 32768 ? t : 65535 - t;
      p.symmetry = 32767 - p.fold;
      p.tone = 24000;
      p.mix = 32767;
    }
    if (scenario == 2) {   // full-scale noise, every knob jumping, n 2..64
      n = Range(1, 32) * 2;
      if (call % 7 == 0) {
        p.fold = Range(-100, 33000);
        p.symmetry = Range(-100, 33000);
        p.tone = Range(-100, 33000);
        p.mix = Range(-100, 33000);
      }
    }
    int16_t lc[64], rc[64], la[64], ra[64];
    for (int32_t i = 0; i < n; ++i) {
      if (scenario == 2) {
        lc[i] = RandomSample();
        rc[i] = RandomSample();
      } else {
        lc[i] = a.Next(16000);
        rc[i] = static_cast<int16_t>((a.Next(12000) + b.Next(8000)) >> 1);
      }
      la[i] = lc[i];
      ra[i] = rc[i];
    }
    fx_c.Process(lc, mono ? nullptr : rc, n, p);
    fx_a.Process(la, mono ? nullptr : ra, n, p);
    for (int32_t i = 0; i < n; ++i) {
      Hash(static_cast<uint16_t>(lc[i]));
      if (!mono) Hash(static_cast<uint16_t>(rc[i]));
    }
#ifdef __bfin__
    if (mismatch < 0 && (memcmp(lc, la, n * 2) || (!mono && memcmp(rc, ra, n * 2)) ||
                         memcmp(&fx_c.channel(0), &fx_a.channel(0), sizeof(FoldChannel)) ||
                         memcmp(&fx_c.channel(1), &fx_a.channel(1), sizeof(FoldChannel)))) {
      mismatch = call;
    }
#endif
  }
#ifdef __bfin__
  printf("effect %-34s %5ld calls: asm %s", name, static_cast<long>(calls),
         mismatch < 0 ? "= C" : "DIFFERS");
  if (mismatch >= 0) {
    printf(" (first at call %ld)", static_cast<long>(mismatch));
    ++failures;
  }
#else
  printf("effect %-34s %5ld calls:", name, static_cast<long>(calls));
#endif
  printf("  checksum %08lx\n", static_cast<unsigned long>(checksum));
}

// --- Mix 0 identity ----------------------------------------------------------

void Identity() {
  Fold fx;
  int32_t bad_init = 0, bad_after = 0;
  for (int32_t pass = 0; pass < 2; ++pass) {
#ifdef __bfin__
    const bool use_asm = pass == 1;
#else
    const bool use_asm = false;
    if (pass) break;
#endif
    rng = 99;
    fx.Init();
    fx.set_use_asm(use_asm);
    FoldParams p = { 32767, 0, 0, 0 };
    for (int32_t call = 0; call < 400; ++call) {
      int16_t x[64], y[64], x2[64], y2[64];
      for (int32_t i = 0; i < 64; ++i) {
        x[i] = y[i] = RandomSample();
        x2[i] = y2[i] = RandomSample();
      }
      if (call == 100) p.mix = 32767;   // up to full wet ...
      if (call == 200) p.mix = 0;       // ... and back
      p.fold = Range(0, 32767);
      p.symmetry = Range(0, 32767);
      p.tone = Range(0, 32767);
      fx.Process(y, (call & 1) ? nullptr : y2, 64, p);
      const bool same = !memcmp(x, y, sizeof(x)) && !memcmp(x2, y2, sizeof(x2));
      if (call < 100 && !same) ++bad_init;
      if (call >= 260 && !same) ++bad_after;   // the smoother has settled long before
    }
  }
  printf("identity: mix 0 from Init: %ld calls differ; after full wet and back: %ld differ\n",
         static_cast<long>(bad_init), static_cast<long>(bad_after));
  if (bad_init || bad_after) ++failures;
}

// --- DC ------------------------------------------------------------------------

// Mean and RMS of the output over 2 s after 1 s of settling.
void DcCase(const char* name, int32_t fold, int32_t sym, int32_t amp, int32_t hz, bool sweep) {
  Fold fx;
  fx.Init();
#ifdef __bfin__
  fx.set_use_asm(true);
#endif
  Osc o;
  o.Init(Inc(hz));
  FoldParams p = { fold, sym, 32767, 32767 };
  int64_t sum = 0, sum2 = 0, count = 0;
  int64_t win_sum = 0;
  int32_t worst_win = 0, win_count = 0;
  for (int32_t call = 0; call < 3 * 689; ++call) {
    if (sweep) {   // symmetry: a 2 Hz triangle end to end
      const int32_t t = (call * 190) & 65535;
      p.symmetry = t < 32768 ? t : 65535 - t;
    }
    int16_t x[64];
    for (int32_t i = 0; i < 64; ++i) x[i] = o.Next(amp);
    fx.Process(x, nullptr, 64, p);
    if (call >= 689) {
      for (int32_t i = 0; i < 64; ++i) {
        sum += x[i];
        sum2 += static_cast<int64_t>(x[i]) * x[i];
        win_sum += x[i];
        // 250 ms windows: whole periods of 220 Hz and 1 kHz.
        if (++win_count == 11025) {
          const int32_t m = static_cast<int32_t>(win_sum / 11025);
          const int32_t am = m < 0 ? -m : m;
          if (am > worst_win) worst_win = am;
          win_sum = 0;
          win_count = 0;
        }
      }
      count += 64;
    }
  }
  const int32_t dc = static_cast<int32_t>(sum / count);
  int32_t rms = 0;
  const int64_t ms = sum2 / count;
  while (static_cast<int64_t>(rms + 1) * (rms + 1) <= ms) ++rms;
  const int32_t adc = dc < 0 ? -dc : dc;
  // DC well under the signal: 1/1000 of the RMS or 8 LSB, whichever is larger.
  const int32_t limit = rms / 1000 > 8 ? rms / 1000 : 8;
  // Swept, the DC moves with the symmetry faster than the 10 Hz blocker
  // follows: allow a quarter of the RMS (it's measured, see fold-port.md).
  const bool ok = adc <= limit && worst_win <= (sweep ? rms / 4 : limit);
  printf("dc %-38s dc %5ld  worst 250 ms mean %5ld  rms %5ld  %s\n", name, static_cast<long>(dc),
         static_cast<long>(worst_win), static_cast<long>(rms), ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

void Dc() {
  DcCase("symmetry 0, fold max, 220 Hz -6 dB", 32767, 0, 16384, 220, false);
  DcCase("symmetry max, fold max, 220 Hz -6 dB", 32767, 32767, 16384, 220, false);
  DcCase("symmetry 0, fold 0, 220 Hz -6 dB", 0, 0, 16384, 220, false);
  DcCase("symmetry max, fold mid, 1 kHz -12 dB", 16384, 32767, 8192, 1000, false);
  DcCase("symmetry swept 2 Hz, fold mid, 220 Hz", 16384, 0, 16384, 220, true);
  DcCase("symmetry 0, silence", 20000, 0, 0, 220, false);
}

// --- Level --------------------------------------------------------------------

void Level() {
  printf("level (wet/dry RMS, 220 Hz, tone open, symmetric):\n");
  const int32_t amps[3] = { 5827, 16384, 29204 };   // -15, -6, -1 dBFS peaks
  const int32_t folds[5] = { 0, 8192, 16384, 24576, 32767 };
  for (int32_t fi = 0; fi < 5; ++fi) {
    const int32_t fold = folds[fi];
    printf("  fold %5ld:", static_cast<long>(fold));
    for (int32_t ai = 0; ai < 3; ++ai) {
      Fold fx;
      fx.Init();
#ifdef __bfin__
      fx.set_use_asm(true);
#endif
      Osc o;
      o.Init(Inc(220));
      FoldParams p = { fold, 16384, 32767, 32767 };
      int64_t s_in = 0, s_out = 0;
      for (int32_t call = 0; call < 400; ++call) {
        int16_t x[64], y[64];
        for (int32_t i = 0; i < 64; ++i) x[i] = y[i] = o.Next(amps[ai]);
        fx.Process(y, nullptr, 64, p);
        if (call >= 100) {
          for (int32_t i = 0; i < 64; ++i) {
            s_in += static_cast<int64_t>(x[i]) * x[i];
            s_out += static_cast<int64_t>(y[i]) * y[i];
          }
        }
      }
      // 100 x the RMS ratio, integer: sqrt(s_out / s_in) x 100.
      int64_t q = s_out * 10000 / (s_in ? s_in : 1);
      int32_t r = 0;
      while (static_cast<int64_t>(r + 1) * (r + 1) <= q) ++r;
      printf("  %s %ld.%02ld", ai == 0 ? "-15 dB" : (ai == 1 ? "-6 dB" : "-1 dB"),
             static_cast<long>(r / 100), static_cast<long>(r % 100));
    }
    printf("\n");
  }
}

// --- Aliasing (host) ------------------------------------------------------------

#ifndef __bfin__
const int32_t kFftN = 8192;

void Fft(double* re, double* im, int32_t n) {
  for (int32_t i = 1, j = 0; i < n; ++i) {
    int32_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      double t = re[i]; re[i] = re[j]; re[j] = t;
      t = im[i]; im[i] = im[j]; im[j] = t;
    }
  }
  for (int32_t len = 2; len <= n; len <<= 1) {
    const double ang = -2 * M_PI / len;
    for (int32_t i = 0; i < n; i += len) {
      for (int32_t k = 0; k < len / 2; ++k) {
        const double wr = cos(ang * k), wi = sin(ang * k);
        const double ur = re[i + k], ui = im[i + k];
        const double vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
        const double vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
        re[i + k] = ur + vr; im[i + k] = ui + vi;
        re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
      }
    }
  }
}

// Harmonic power and alias power (every other bin but DC) of a signal whose
// fundamental sits on bin k: dB of alias / harmonics, and the strongest
// single alias in dBc (against the fundamental).
// Also the aliases below 5 kHz only (inharmonic tones under the note,
// the ones that are heard), against all the harmonics.
struct AliasResult { double total_db, low_db, worst_db, harm_db; };
AliasResult Analyse(const double* y, int32_t k) {
  static double re[kFftN], im[kFftN];
  for (int32_t i = 0; i < kFftN; ++i) { re[i] = y[i]; im[i] = 0; }
  Fft(re, im, kFftN);
  double harm = 0, alias = 0, low = 0, worst = 0;
  const int32_t low_bins = 5000 * kFftN / 44100;
  const double fund = re[k] * re[k] + im[k] * im[k];
  for (int32_t b = 1; b < kFftN / 2; ++b) {
    const double p = re[b] * re[b] + im[b] * im[b];
    if (b % k == 0) {
      harm += p;
    } else {
      alias += p;
      if (b < low_bins) low += p;
      if (p > worst) worst = p;
    }
  }
  AliasResult r;
  r.total_db = 10 * log10(alias / harm + 1e-30);
  r.low_db = 10 * log10(low / harm + 1e-30);
  r.worst_db = 10 * log10(worst / fund + 1e-30);
  r.harm_db = 10 * log10(harm / fund);
  return r;
}

// The 2x reference: double precision, Clouds' 19-tap halfband (the taps
// of src/core/halfband.h), ADAA at 88.2 kHz.
const double kHb[5] = { 10019 / 32768.0, -2397 / 32768.0, 710 / 32768.0, -151 / 32768.0, 11 / 32768.0 };

// Up-samples x (n) 2:1 into out (2n), zero-stuffed, x 2.
void Up2(const double* x, double* out, int32_t n) {
  for (int32_t m = 0; m < n; ++m) {
    // Even output: x[m] (the centre tap); odd: halfway to x[m + 1].
    double acc = 0;
    for (int32_t t = 0; t < 5; ++t) {
      const int32_t a = m - t, b = m + 1 + t;   // around m + 0.5
      const double xa = a >= 0 ? x[a] : 0, xb = b < n ? x[b] : 0;
      acc += kHb[t] * (xa + xb);
    }
    out[2 * m] = x[m];
    out[2 * m + 1] = 2 * acc;
  }
}
void Down2(const double* x, double* out, int32_t n) {   // 2n in, n out
  for (int32_t k = 0; k < n; ++k) {
    const int32_t c = 2 * k;
    double acc = 0.5 * x[c];
    for (int32_t t = 0; t < 5; ++t) {
      const int32_t a = c - (2 * t + 1), b = c + (2 * t + 1);
      acc += kHb[t] * ((a >= 0 ? x[a] : 0) + (b < 2 * n ? x[b] : 0));
    }
    out[k] = acc;
  }
}

void AliasCase(int32_t fold, int32_t bin, int32_t amp) {
  const int32_t warm = 688 * 64, n = kFftN;
  const int32_t gi = fold >> 10, gf = fold & 1023;
  const double turns =
      (op1fx::kFoldGain[gi] + (((op1fx::kFoldGain[gi + 1] - op1fx::kFoldGain[gi]) * gf) >> 10)) / 4096.0;
  // The effect (ADAA, fixed point), tone open, symmetric, full wet.
  static double y_adaa[kFftN], y_naive[kFftN], y_2x[kFftN], y_float[kFftN];
  {
    Fold fx;
    fx.Init();
    FoldParams p = { fold, 16384, 32767, 32767 };
    const uint32_t inc = static_cast<uint32_t>((static_cast<uint64_t>(bin) << 32) / n);
    uint32_t ph = 0;
    for (int32_t call = 0; call < (warm + n) / 64; ++call) {
      int16_t x[64];
      for (int32_t i = 0; i < 64; ++i) {
        x[i] = static_cast<int16_t>(lround(amp * sin(2 * M_PI * ph / 4294967296.0)));
        ph += inc;
      }
      fx.Process(x, nullptr, 64, p);
      const int32_t base = call * 64 - warm;
      for (int32_t i = 0; i < 64; ++i) {
        if (base + i >= 0) y_adaa[base + i] = x[i];
      }
    }
  }
  // Naive (no ADAA) and float ADAA, double precision, same u.
  const int32_t total = warm + n;
  static double xs[44100 + kFftN], up[2 * (44100 + kFftN)], fo[2 * (44100 + kFftN)], dn[44100 + kFftN];
  for (int32_t i = 0; i < total; ++i) {
    const double ph = static_cast<double>(static_cast<uint64_t>(bin) * i % n) / n;
    xs[i] = lround(amp * sin(2 * M_PI * ph)) / 32768.0;
  }
  double up_prev = 0;
  for (int32_t i = warm; i < total; ++i) {
    const double u = xs[i] * turns, u1 = xs[i - 1] * turns;
    y_naive[i - warm] = sin(2 * M_PI * u);
    const double d = u - u1;
    y_float[i - warm] = fabs(d) < 1e-9 ? sin(M_PI * (u + u1))
                                         : (cos(2 * M_PI * u1) - cos(2 * M_PI * u)) / (2 * M_PI * d);
    (void)up_prev;
  }
  // 2x: up, ADAA at 2x, down.
  Up2(xs, up, total);
  double p2 = 0;
  for (int32_t i = 0; i < 2 * total; ++i) {
    const double u = up[i] * turns, d = u - p2;
    fo[i] = fabs(d) < 1e-9 ? sin(M_PI * (u + p2)) : (cos(2 * M_PI * p2) - cos(2 * M_PI * u)) / (2 * M_PI * d);
    p2 = u;
  }
  Down2(fo, dn, total);
  for (int32_t i = 0; i < n; ++i) y_2x[i] = dn[warm + i];

  const AliasResult a = Analyse(y_adaa, bin), b = Analyse(y_naive, bin);
  const AliasResult f = Analyse(y_float, bin), c = Analyse(y_2x, bin);
  printf("  %5.0f Hz, %4.2f turns at the peak: all aliases naive %6.1f dB, ADAA %6.1f (float %6.1f), 2x %6.1f"
         " | below 5 kHz naive %6.1f, ADAA %6.1f, 2x %6.1f | strongest naive %6.1f dBc, ADAA %6.1f, 2x %6.1f\n",
         bin * 44100.0 / n, turns * amp / 32768.0, b.total_db, a.total_db, f.total_db, c.total_db,
         b.low_db, a.low_db, c.low_db, b.worst_db, a.worst_db, c.worst_db);
  // ADAA must clearly beat the naive folder on the audible aliases, unless
  // both are down at the fixed-point noise floor.
  const bool ok = a.low_db < b.low_db - 6 || a.low_db < -80;
  if (!ok) {
    printf("    FAIL: ADAA not clearly better below 5 kHz\n");
    ++failures;
  }
}

void Aliasing() {
  printf("aliasing (a sine on an FFT bin, %ld points; tone open, symmetric, full wet):\n",
         static_cast<long>(kFftN));
  const int32_t bins[3] = { 371, 929, 1487 };   // 1997, 5001, 8005 Hz; aliases miss the harmonics
  const int32_t folds[3] = { 8192, 16384, 32767 };
  for (int32_t fi = 0; fi < 3; ++fi) {
    for (int32_t bi = 0; bi < 3; ++bi) AliasCase(folds[fi] == 32767 ? 32767 : folds[fi], bins[bi], 16384);
  }
}
#endif

// --- Cycles ----------------------------------------------------------------------

#ifdef __bfin__
void Cycles(const char* name, bool use_asm, bool mono, bool lfo) {
  Fold fx;
  fx.Init();
  fx.set_use_asm(use_asm);
  Osc a;
  a.Init(Inc(220));
  rng = 4242;
  FoldParams p = { 20000, 12000, 20000, 30000 };
  harness::CycleStats stats;
  for (int32_t call = 0; call < 600; ++call) {
    if (lfo) {   // every knob moving every call: the full control path
      const int32_t t = (call * 300) & 65535;
      const int32_t tri = t < 32768 ? t : 65535 - t;
      p.fold = tri;
      p.symmetry = 32767 - tri;
      p.tone = tri;
      p.mix = 16384 + tri / 2;
    } else if (call % 40 == 0) {   // knobs move now and then: the ramps run
      p.fold = Range(0, 32767);
      p.symmetry = Range(0, 32767);
    }
    int16_t l[64], r[64];
    for (int32_t i = 0; i < 64; ++i) {
      l[i] = a.Next(20000);
      r[i] = static_cast<int16_t>(RandomSample() >> 1);
    }
    const uint64_t t0 = harness::ReadCycles();
    fx.Process(l, mono ? nullptr : r, 64, p);
    const uint64_t t1 = harness::ReadCycles();
    if (call >= 10) stats.Add(t1 - t0);
  }
  const unsigned long avg = static_cast<unsigned long>(stats.total / stats.n);
  const unsigned long khz_avg = static_cast<unsigned long>(stats.total * 689 / stats.n / 1000);
  const unsigned long khz_max = static_cast<unsigned long>(stats.max * 689 / 1000);
  printf("cycles %-12s per call avg %5lu min %5lu max %5lu  per sample %3lu  => %lu.%02lu MHz avg, %lu.%02lu MHz peak\n",
         name, avg, static_cast<unsigned long>(stats.min), static_cast<unsigned long>(stats.max),
         avg / 64, khz_avg / 1000, (khz_avg % 1000) / 10, khz_max / 1000, (khz_max % 1000) / 10);
}
#endif

}  // namespace

int main() {
  harness::EnableCycleCounter();
#ifdef __bfin__
  TestKernels();
  KernelCycles();
#endif
  Scenario("knob jumps, stereo", 0, 2000, false);
  Scenario("fold/symmetry sweeps, stereo", 1, 2000, false);
  Scenario("full-scale noise, n 2..64, jumps", 2, 2000, false);
  Scenario("knob jumps, mono", 0, 1000, true);
  Identity();
  Dc();
  Level();
#ifndef __bfin__
  Aliasing();
#endif
#ifdef __bfin__
  Cycles("stereo", true, false, false);
  Cycles("mono", true, true, false);
  Cycles("stereo, LFO", true, false, true);
  Cycles("mono, LFO", true, true, true);
  Cycles("C stereo", false, false, false);
  Cycles("C mono", false, true, false);
  printf("memory: sizeof(Fold) %lu bytes; tables (shared, const) %lu bytes\n",
         static_cast<unsigned long>(sizeof(Fold)),
         static_cast<unsigned long>(sizeof(op1fx::kFoldSin) + sizeof(op1fx::kFoldSinc) + sizeof(op1fx::kFoldGain) +
                                    sizeof(op1fx::kFoldLevel) + sizeof(op1fx::kFoldTone) +
                                    sizeof(op1fx::kFoldRecip)));
#endif
  printf("%s: %d failure(s)\n", failures ? "FAIL" : "ok", failures);
  return failures ? 1 : 0;
}
