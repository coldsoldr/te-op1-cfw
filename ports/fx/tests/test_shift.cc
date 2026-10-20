// SHIFT's checks (make test-shift), built for the Blackfin simulator and
// for the host:
//   kernels    (Blackfin) each assembly kernel against its C reference on
//              random blocks and states, sample for sample, state for
//              state, with guard words round the outputs;
//   instance   (Blackfin) a C and an assembly FreqShift through scenarios
//              (mode changes, sweeps, mono/stereo, n 2..64): equal;
//   identity   mix 0 returns the input bit for bit (mono, stereo, any
//              other settings, after a mix ramp down);
//   rejection  the unwanted sideband for tones 50 Hz..15 kHz, up and down;
//   accuracy   the output frequency against 2000 Hz x (d / 32768)^3;
//   spread     L up, R down by the spread;
//   ring       both sidebands equal, the input tone suppressed;
//   feedback   60 s at the top feedback (noise, sines, bursts): bounded,
//              decays to silence when the input stops;
//   checksums  of the C output, printed by both builds (must be equal);
//   cycles     per 64-sample call, stereo and mono, asm and C (Blackfin).
// The measurements use double (soft-float on the Blackfin: slow but fine).

// int32_t is long on the Blackfin: the prints use %d throughout.
#pragma GCC diagnostic ignored "-Wformat"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "rings/harness/cycles.h"
#include "src/shift/shift.h"

using op1fx::FreqShift;
using op1fx::ShiftParams;

namespace {

const double kFs = 44100.0;
const double kPi = 3.14159265358979323846;

uint32_t rng = 12345;
uint32_t Next() {
  rng = rng * 1664525u + 1013904223u;
  return rng;
}
int32_t Range(int32_t lo, int32_t hi) {   // [lo, hi]
  return lo + static_cast<int32_t>(Next() % static_cast<uint32_t>(hi - lo + 1));
}
int32_t Clamp16(int32_t x) { return x < 0 ? 0 : (x > 32767 ? 32767 : x); }   // a knob
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
void Check(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL: %s\n", what);
    ++failures;
  }
}

// --- Signals ------------------------------------------------------------

// A sine by phasor rotation (no libm per sample).
struct Osc {
  double re, im, cr, ci, amp;
  void Init(double f, double a) {
    re = 1.0;
    im = 0.0;
    cr = cos(2 * kPi * f / kFs);
    ci = sin(2 * kPi * f / kFs);
    amp = a;
  }
  int16_t Next() {
    const double v = im * amp;
    const double r = re * cr - im * ci;
    im = re * ci + im * cr;
    re = r;
    return static_cast<int16_t>(lrint(v));
  }
};

// The Hann-windowed amplitude of frequency f in x[0..n): a sine of
// amplitude A reads A. Also its phase (radians) through *phase.
double Amplitude(const int16_t* x, int32_t n, double f, double* phase = nullptr) {
  const double w = 2 * kPi * f / kFs;
  const double cr = cos(w), ci = -sin(w);
  const double hr = cos(2 * kPi / n), hi = sin(2 * kPi / n);
  double zr = 1.0, zi = 0.0, hzr = 1.0, hzi = 0.0;   // e^-jwi, e^j2pi i/n
  double sr = 0.0, si = 0.0;
  for (int32_t i = 0; i < n; ++i) {
    const double win = 0.5 - 0.5 * hzr;
    sr += x[i] * win * zr;
    si += x[i] * win * zi;
    double t = zr * cr - zi * ci;
    zi = zr * ci + zi * cr;
    zr = t;
    t = hzr * hr - hzi * hi;
    hzi = hzr * hi + hzi * hr;
    hzr = t;
  }
  if (phase) *phase = atan2(si, sr);
  return 2.0 * sqrt(sr * sr + si * si) / (0.5 * n);
}

double Db(double a, double b) { return 20.0 * log10((a + 1e-9) / (b + 1e-9)); }

ShiftParams Params(int32_t shift, int32_t feedback, int32_t spread, int32_t mix, bool ring) {
  ShiftParams p;
  p.shift = shift;
  p.feedback = feedback;
  p.spread = spread;
  p.mix = mix;
  p.ring = ring;
  return p;
}

// The knob position for a shift in Hz (the inverse of the cubic).
int32_t KnobForHz(double hz) {
  const double d = cbrt(hz / 2000.0) * 32768.0;
  return 16384 + static_cast<int32_t>(lrint(d / 2.0));
}
double HzForKnob(int32_t knob) {
  const double d = op1fx::ShiftKnobToD(knob) / 32768.0;
  return 2000.0 * d * d * d;
}

void Run(FreqShift* fx, int16_t* l, int16_t* r, int32_t n, const ShiftParams& p, int32_t block = 64) {
  for (int32_t i = 0; i < n; i += block) fx->Process(l + i, r ? r + i : nullptr, block, p);
}

const int32_t kSettle = 8192;
const int32_t kWindow = 16384;
int16_t sig_l[kSettle + 2 * kWindow];
int16_t sig_r[kSettle + 2 * kWindow];
FreqShift fx_a, fx_b;

// --- asm = C ------------------------------------------------------------

#ifdef __bfin__
const int32_t kGuard = 16;
const uint32_t kGuardWord = 0x5a5aa5a5u;

void KernelTests() {
  using namespace op1fx;
  static ShiftHilbertKernel hc, ha;
  static ShiftChannelKernel cc, ca;
  static int16_t l[SH_MAX_BLOCK], r[SH_MAX_BLOCK];
  static uint32_t iqc[kGuard + SH_MAX_BLOCK + kGuard], iqa[kGuard + SH_MAX_BLOCK + kGuard];
  static int32_t bufc[SH_SCRATCH], bufa[SH_SCRATCH];
  static int16_t ioc[kGuard + SH_MAX_BLOCK + kGuard], ioa[kGuard + SH_MAX_BLOCK + kGuard];
  static uint32_t scratch[kGuard + SH_MAX_BLOCK + kGuard];
  for (int32_t i = 0; i < kGuard + SH_MAX_BLOCK + kGuard; ++i) scratch[i] = kGuardWord;
  int bad = 0;
  for (int trial = 0; trial < 3000; ++trial) {
    const int32_t n = 2 * Range(1, SH_MAX_BLOCK / 2);
    // Hilbert: random states (within the reachable range and beyond, to
    // exercise the saturation), random input, mono or stereo.
    for (int32_t j = 0; j < 8; ++j) {
      for (int32_t s = 0; s < 2; ++s) {
        const int32_t v = static_cast<int32_t>(Next());
        hc.y[j][s] = (trial & 7) ? (v >> 3) : v;
      }
      hc.coef[j] = (trial & 1) ? kShiftHilbert[j] : static_cast<int16_t>(Next() & 0x7fff);
    }
    for (int32_t s = 0; s < 3; ++s) hc.xh[s] = static_cast<int32_t>(Next()) >> 6;
    switch (trial % 3) {
      case 0: hc.m = 32768; hc.dm = 0; break;           // shift
      case 1: hc.m = Range(0, 32768); hc.dm = 0; break;
      default: hc.m = Range(0, 32768); hc.dm = Range(-hc.m / n, (32768 - hc.m) / n); break;
    }
    ha = hc;
    for (int32_t i = 0; i < n; ++i) {
      l[i] = RandomSample();
      r[i] = RandomSample();
    }
    for (int32_t i = 0; i < kGuard + SH_MAX_BLOCK + kGuard; ++i) iqc[i] = iqa[i] = kGuardWord;
    const bool mono = trial & 2;
    ShiftHilbertC(&hc, l, mono ? nullptr : r, iqc + kGuard, bufc, n);
    ShiftHilbertAsm(&ha, l, mono ? nullptr : r, iqa + kGuard, bufa, n);
    if (memcmp(iqc, iqa, sizeof(iqc)) || memcmp(&hc, &ha, sizeof(hc))) {
      if (bad++ < 5) {
        printf("hilbert mismatch trial %d n %d mono %d\n", trial, n, mono);
        for (int32_t i = 0; i < n; ++i)
          if (iqc[kGuard + i] != iqa[kGuard + i]) {
            printf("  i %d C %08x asm %08x\n", i, iqc[kGuard + i], iqa[kGuard + i]);
            break;
          }
        for (int32_t j = 0; j < 8; ++j)
          for (int32_t s = 0; s < 2; ++s)
            if (hc.y[j][s] != ha.y[j][s]) printf("  y[%d][%d] C %d asm %d\n", j, s, hc.y[j][s], ha.y[j][s]);
        for (int32_t s = 0; s < 3; ++s)
          if (hc.xh[s] != ha.xh[s]) printf("  xh[%d] C %d asm %d\n", s, hc.xh[s], ha.xh[s]);
        if (hc.m != ha.m) printf("  m C %d asm %d\n", hc.m, ha.m);
      }
    }
    Check(iqc[0] == kGuardWord && iqc[kGuard + n] == kGuardWord, "hilbert C guard");
    Check(iqa[kGuard - 1] == kGuardWord && iqa[kGuard + n] == kGuardWord, "hilbert asm guard");

    // Channel: random state, delay, iq, params.
    cc.phase = Next();
    cc.inc = static_cast<int32_t>(Next()) >> 3;
    cc.dinc = static_cast<int32_t>(Next()) >> 12;
    cc.g = Range(0, 32767);
    cc.dg = 0;
    cc.mix = Range(0, 32767);
    cc.dmix = 0;
    if (trial & 4) {
      cc.dg = Range(-cc.g / n, (32767 - cc.g) / n);
      cc.dmix = Range(-cc.mix / n, (32767 - cc.mix) / n);
    }
    cc.lp = Next();
    cc.pos = 4 * Range(0, SH_DELAY - 1);
    cc.osc = kShiftOsc;
    for (int32_t i = 0; i < SH_DELAY; ++i) cc.delay[i] = Next();
    ca = cc;
    for (int32_t i = 0; i < kGuard + SH_MAX_BLOCK + kGuard; ++i) ioc[i] = ioa[i] = static_cast<int16_t>(kGuardWord);
    for (int32_t i = 0; i < n; ++i) ioc[kGuard + i] = ioa[kGuard + i] = RandomSample();
    const int32_t write = (trial % 5) != 0;
    ShiftChannelC(&cc, iqc + kGuard, ioc + kGuard, n, write, scratch);
    ShiftChannelAsm(&ca, iqc + kGuard, ioa + kGuard, n, write, scratch + kGuard);
    for (int32_t i = 0; i < kGuard; ++i)
      Check(scratch[i] == kGuardWord && scratch[kGuard + SH_MAX_BLOCK + i] == kGuardWord, "scratch guard");
    if (memcmp(ioc, ioa, sizeof(ioc)) || memcmp(&cc, &ca, sizeof(cc))) {
      if (bad++ < 5) {
        printf("channel mismatch trial %d n %d write %d\n", trial, n, write);
        for (int32_t i = 0; i < n; ++i)
          if (ioc[kGuard + i] != ioa[kGuard + i]) {
            printf("  i %d C %d asm %d\n", i, ioc[kGuard + i], ioa[kGuard + i]);
            break;
          }
        printf("  phase %u/%u inc %d/%d g %d/%d mix %d/%d lp %08x/%08x pos %d/%d\n", cc.phase, ca.phase,
               cc.inc, ca.inc, cc.g, ca.g, cc.mix, ca.mix, cc.lp, ca.lp, cc.pos, ca.pos);
        for (int32_t i = 0; i < SH_DELAY; ++i)
          if (cc.delay[i] != ca.delay[i]) {
            printf("  delay[%d] C %08x asm %08x\n", i, cc.delay[i], ca.delay[i]);
            break;
          }
      }
    }
    Check(ioa[kGuard - 1] == static_cast<int16_t>(kGuardWord) &&
          ioa[kGuard + n] == static_cast<int16_t>(kGuardWord), "channel asm guard");
  }
  Check(bad == 0, "kernels: asm = C");
  printf("kernels: 3000 random blocks, %s\n", bad ? "MISMATCH" : "asm = C");
}

void InstanceTest() {
  static int16_t al[64], ar[64], cl[64], cr[64];
  fx_a.Init();
  fx_b.Init();
  fx_a.set_use_asm(true);
  fx_b.set_use_asm(false);
  ShiftParams p = Params(16384, 0, 0, 32767, false);
  int bad = 0;
  for (int32_t call = 0; call < 6000; ++call) {
    if (call % 50 == 0) {
      p.shift = Range(0, 32767);
      p.feedback = (call % 300 < 150) ? 32767 : Range(0, 32767);
      p.spread = Range(0, 32767);
      p.mix = (call % 700 < 50) ? 0 : Range(0, 32767);
      p.ring = (Next() & 3) == 0;
    }
    if (call % 7 == 0) p.shift = Clamp16(p.shift + Range(-300, 300));
    const bool mono = (call / 1000) & 1;
    const int32_t n = (call % 13 == 0) ? 2 * Range(1, 32) : 64;
    for (int32_t i = 0; i < n; ++i) {
      al[i] = cl[i] = RandomSample();
      ar[i] = cr[i] = RandomSample();
    }
    fx_a.Process(al, mono ? nullptr : ar, n, p);
    fx_b.Process(cl, mono ? nullptr : cr, n, p);
    if (memcmp(al, cl, 2 * n) || (!mono && memcmp(ar, cr, 2 * n))) ++bad;
  }
  Check(bad == 0, "instance: asm = C");
  printf("instance: 6000 calls (sweeps, modes, mono/stereo, n 2..64): %s\n", bad ? "MISMATCH" : "asm = C");
}

void Cycles() {
  static int16_t l[64], r[64];
  for (int pass = 0; pass < 4; ++pass) {
    const bool use_asm = pass < 2, mono = pass & 1;
    fx_a.Init();
    fx_a.set_use_asm(use_asm);
    harness::CycleStats st;
    const ShiftParams p = Params(20000, 30000, 12000, 20000, false);
    for (int32_t call = 0; call < 200; ++call) {
      for (int32_t i = 0; i < 64; ++i) {
        l[i] = RandomSample();
        r[i] = RandomSample();
      }
      const uint64_t t0 = harness::ReadCycles();
      fx_a.Process(l, mono ? nullptr : r, 64, p);
      const uint64_t t1 = harness::ReadCycles();
      if (call >= 10) st.Add(t1 - t0);
    }
    printf("cycles %-4s %-6s avg %5.0f max %5lu per 64-sample call => %.2f MHz (x 689)\n",
           use_asm ? "asm" : "C", mono ? "mono" : "stereo", static_cast<double>(st.total) / st.n,
           static_cast<unsigned long>(st.max), static_cast<double>(st.total) / st.n * 689.0 / 1e6);
  }
}
#endif  // __bfin__

// --- Identity -------------------------------------------------------------

void IdentityTest(bool use_asm) {
  static int16_t l[64], r[64], l0[64], r0[64];
  FreqShift* fx = &fx_a;
  fx->Init();
#ifdef __bfin__
  fx->set_use_asm(use_asm);
#endif
  int bad = 0;
  ShiftParams p = Params(16384, 0, 0, 0, false);
  for (int32_t call = 0; call < 4000; ++call) {
    const bool mono = (call / 500) & 1;
    // Every 100 calls: some calls wet, then back to 0.
    const bool wet = call % 100 >= 90;
    if (call % 10 == 0) {
      p.shift = Range(0, 32767);
      p.feedback = Range(0, 32767);
      p.spread = Range(0, 32767);
      p.ring = Next() & 1;
    }
    p.mix = wet ? Range(1, 32767) : 0;
    for (int32_t i = 0; i < 64; ++i) {
      l[i] = l0[i] = RandomSample();
      r[i] = r0[i] = RandomSample();
    }
    fx->Process(l, mono ? nullptr : r, 64, p);
    // The first call at mix 0 after wet calls ramps the mix down: skip it.
    const bool after_wet = call % 100 == 0 && call > 0;
    if (!wet && !after_wet && (memcmp(l, l0, sizeof(l)) || (!mono && memcmp(r, r0, sizeof(r))))) ++bad;
    if (mono && memcmp(r, r0, sizeof(r))) ++bad;   // mono never touches r
  }
  Check(bad == 0, "mix 0 identity");
  printf("identity (%s): 4000 calls at mix 0 (any shift/feedback/spread/mode, mono and stereo): %s\n",
         use_asm ? "asm" : "C", bad ? "DIFFERS" : "bit-exact");
}

// --- Rejection, accuracy, spread, ring ------------------------------------

// Mono shifter, full wet, on a tone f; returns wanted / unwanted (dB).
double Rejection(double f, int32_t knob, double* wanted_db) {
  const double hz = HzForKnob(knob);
  Osc o;
  o.Init(f, 16000.0);
  const int32_t n = kSettle + kWindow;
  for (int32_t i = 0; i < n; ++i) sig_l[i] = o.Next();
  fx_a.Init();
  Run(&fx_a, sig_l, nullptr, n, Params(knob, 0, 0, 32767, false));
  const double want = Amplitude(sig_l + kSettle, kWindow, fabs(f + hz));
  const double unwanted = Amplitude(sig_l + kSettle, kWindow, fabs(f - hz));
  if (wanted_db) *wanted_db = Db(want, 16000.0);
  return Db(want, unwanted);
}

void RejectionTest() {
  const double freqs[] = {50, 100, 200, 500, 1000, 2000, 5000, 8000, 10000, 15000};
  const double shifts[] = {250.0, -250.0, 31.25, -1000.0};
  double worst = 1e9, worst_wide = 1e9;
  for (double hz : shifts) {
    const int32_t knob = KnobForHz(hz);
    printf("rejection, shift %+8.2f Hz:", HzForKnob(knob));
    for (double f : freqs) {
      if (f + HzForKnob(knob) < 20 || fabs(f - HzForKnob(knob)) < 20) {
        printf("     -");
        continue;
      }
      double gain;
      const double rej = Rejection(f, knob, &gain);
      printf(" %5.1f", rej);
      if (f >= 100 && f <= 10000 && rej < worst) worst = rej;
      if (rej < worst_wide) worst_wide = rej;
      if (fabs(gain) > 0.5) printf("(gain %.2f dB)", gain);
    }
    printf("  dB\n");
  }
  printf("  (tones 50 100 200 500 1k 2k 5k 8k 10k 15k Hz; - = sideband too close to 0 Hz)\n");
  printf("rejection: worst %.1f dB over 100 Hz-10 kHz, %.1f dB over every tone\n", worst, worst_wide);
  Check(worst >= 40.0, "sideband rejection >= 40 dB, 100 Hz-10 kHz");
}

// Frequency of the strongest tone near fe in x: from the phase advance
// between two windows.
double MeasureFreq(const int16_t* x, double fe) {
  double p1, p2;
  Amplitude(x, kWindow, fe, &p1);
  Amplitude(x + kWindow, kWindow, fe, &p2);
  double d = p2 - p1;
  // d is the measured advance mod 2 pi; the expected advance is fe's.
  const double expect = 2 * kPi * fe * kWindow / kFs;
  d -= 2 * kPi * floor((d - expect) / (2 * kPi) + 0.5);
  return d * kFs / (2 * kPi * kWindow);
}

void AccuracyTest() {
  const double targets[] = {-2000, -500, -100, -10, -1, -0.1, 0.1, 1, 10, 100, 500, 1999.6};
  double worst = 0;
  printf("accuracy (1 kHz in, knob for the shift, output measured):\n");
  for (double t : targets) {
    const int32_t knob = t >= 1999 ? 32767 : (t <= -2000 ? 0 : KnobForHz(t));
    const double hz = HzForKnob(knob);
    Osc o;
    o.Init(1000.0, 16000.0);
    const int32_t n = kSettle + 2 * kWindow;
    for (int32_t i = 0; i < n; ++i) sig_l[i] = o.Next();
    fx_a.Init();
    Run(&fx_a, sig_l, nullptr, n, Params(knob, 0, 0, 32767, false));
    const double f = MeasureFreq(sig_l + kSettle, 1000.0 + hz);
    const double inc_hz = op1fx::ShiftIncrement(op1fx::ShiftKnobToD(knob)) * kFs / 4294967296.0;
    const double err = f - 1000.0 - hz;
    if (fabs(err) > worst) worst = fabs(err);
    printf("  knob %5d: curve %+10.4f Hz, step %+10.4f Hz, measured %+10.4f Hz (err %+.4f)\n", knob, hz,
           inc_hz, f - 1000.0, err);
  }
  printf("accuracy: worst error %.4f Hz\n", worst);
  Check(worst < 0.01, "shift accuracy < 0.01 Hz");
  // The curve's ends and centre.
  Check(op1fx::ShiftIncrement(op1fx::ShiftKnobToD(16384)) == 0, "16384 = 0 Hz");
  Check(op1fx::ShiftIncrement(op1fx::ShiftKnobToD(0)) == -194783097, "0 = -2000 Hz");
}

void SpreadTest() {
  const int32_t knob = KnobForHz(20.0);
  const double hz = HzForKnob(knob);
  const int32_t spread = 32767;
  const double dl = (op1fx::ShiftKnobToD(knob) + op1fx::ShiftSpreadToD(spread)) / 32768.0;
  const double dr = (op1fx::ShiftKnobToD(knob) - op1fx::ShiftSpreadToD(spread)) / 32768.0;
  const double hl = 2000 * dl * dl * dl, hr = 2000 * dr * dr * dr;
  Osc o;
  o.Init(1000.0, 16000.0);
  const int32_t n = kSettle + 2 * kWindow;
  for (int32_t i = 0; i < n; ++i) sig_l[i] = sig_r[i] = o.Next();
  fx_a.Init();
  Run(&fx_a, sig_l, sig_r, n, Params(knob, 0, spread, 32767, false));
  const double fl = MeasureFreq(sig_l + kSettle, 1000.0 + hl) - 1000.0;
  const double fr = MeasureFreq(sig_r + kSettle, 1000.0 + hr) - 1000.0;
  printf("spread (shift %+.2f Hz, spread top): L %+.3f Hz (expect %+.3f), R %+.3f Hz (expect %+.3f)\n", hz,
         fl, hl, fr, hr);
  Check(fabs(fl - hl) < 0.01 && fabs(fr - hr) < 0.01, "spread: L = shift + spread, R = shift - spread");
  // Spread alone at the centre: the extremes.
  const double ds = op1fx::ShiftSpreadToD(32767) / 32768.0;
  printf("spread: at shift 0, the top spread gives L %+.2f Hz, R %+.2f Hz\n", 2000 * ds * ds * ds,
         -2000 * ds * ds * ds);
}

void RingTest() {
  const int32_t knob = KnobForHz(-250.0);   // ring uses |shift|
  const double hz = fabs(HzForKnob(knob));
  Osc o;
  o.Init(1000.0, 16000.0);
  const int32_t n = kSettle + kWindow;
  for (int32_t i = 0; i < n; ++i) sig_l[i] = o.Next();
  fx_a.Init();
  Run(&fx_a, sig_l, nullptr, n, Params(knob, 0, 0, 32767, true));
  const double up = Amplitude(sig_l + kSettle, kWindow, 1000.0 + hz);
  const double dn = Amplitude(sig_l + kSettle, kWindow, 1000.0 - hz);
  const double car = Amplitude(sig_l + kSettle, kWindow, 1000.0);
  const double cf = Amplitude(sig_l + kSettle, kWindow, hz);
  printf("ring (1 kHz in, carrier %.1f Hz): sum %.1f dB, difference %.1f dB (re input), input tone %.1f dB, carrier %.1f dB\n",
         hz, Db(up, 16000), Db(dn, 16000), Db(car, 16000), Db(cf, 16000));
  Check(fabs(Db(up, dn)) < 0.5, "ring: sidebands equal");
  Check(Db(car, up) < -40 && Db(cf, up) < -40, "ring: input and carrier suppressed");
}

// --- Feedback stability ---------------------------------------------------

void FeedbackTest() {
  static int16_t l[64], r[64];
  const int32_t knobs[] = {16384, KnobForHz(0.3), KnobForHz(-3), KnobForHz(60), KnobForHz(-700), 32767, 0};
  int32_t worst_tail = 0;
  for (int32_t pass = 0; pass < 2; ++pass) {
    const bool ring = pass == 1;
    for (int32_t k : knobs) {
      fx_a.Init();
      const ShiftParams p = Params(k, 32767, 8000, 32767, ring);
      int32_t peak = 0;
      int64_t sum2 = 0, cnt = 0;
      Osc o1, o2;
      o1.Init(110.0, 20000.0);
      o2.Init(3150.0, 12000.0);
      // 8 s: 2 s noise at full scale, 2 s loud chord, 2 s bursts; then 2 s
      // silence: the loop must die away.
      const int32_t calls = 8 * 689;
      int32_t tail_peak = 0;
      for (int32_t c = 0; c < calls; ++c) {
        const int32_t sec = c / 689;
        for (int32_t i = 0; i < 64; ++i) {
          int32_t v = 0;
          if (sec < 2) v = RandomSample();
          else if (sec < 4) v = o1.Next() + o2.Next();
          else if (sec < 6) v = (c % 40 < 3) ? RandomSample() : 0;
          v = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
          l[i] = r[i] = static_cast<int16_t>(v);
        }
        fx_a.Process(l, r, 64, p);
        for (int32_t i = 0; i < 64; ++i) {
          const int32_t a = l[i] < 0 ? -l[i] : l[i];
          if (a > peak) peak = a;
          if (sec == 7) {
            if (a > tail_peak) tail_peak = a;
            const int32_t b = r[i] < 0 ? -r[i] : r[i];
            if (b > tail_peak) tail_peak = b;
          }
          sum2 += static_cast<int64_t>(l[i]) * l[i];
          ++cnt;
          Hash(static_cast<uint16_t>(l[i]) | (static_cast<uint32_t>(static_cast<uint16_t>(r[i])) << 16));
        }
      }
      if (tail_peak > worst_tail) worst_tail = tail_peak;
      printf("feedback top, %s %+9.2f Hz: peak %5d, RMS %6.0f, last second (silent input) peak %d\n",
             ring ? "ring " : "shift", HzForKnob(k), peak, sqrt(static_cast<double>(sum2) / cnt), tail_peak);
    }
  }
  Check(worst_tail == 0, "feedback: decays to silence");
}

// 60 s of full-scale noise at the top feedback, shift and ring, then 2 s
// of silence: bounded, and silent at the end. Integer only, so it runs on
// the Blackfin too (the assembly there).
void FeedbackNoiseTest() {
  static int16_t l[64], r[64];
  for (int32_t pass = 0; pass < 2; ++pass) {
    fx_a.Init();
    const ShiftParams p = Params(17000, 32767, 32767, 32767, pass == 1);   // +0.12 Hz
    int32_t peak = 0, tail = 0;
    for (int32_t c = 0; c < 62 * 689; ++c) {
      const bool silent = c >= 60 * 689;
      for (int32_t i = 0; i < 64; ++i) l[i] = r[i] = silent ? 0 : RandomSample();
      fx_a.Process(l, r, 64, p);
      for (int32_t i = 0; i < 64; ++i) {
        const int32_t a = l[i] < 0 ? -l[i] : l[i];
        const int32_t b = r[i] < 0 ? -r[i] : r[i];
        if (a > peak) peak = a;
        if (c >= 61 * 689 && (a > tail || b > tail)) tail = a > b ? a : b;
      }
    }
    printf("feedback top, 60 s full-scale noise, %s: peak %d (bounded), last second of silence peak %d\n",
           pass ? "ring" : "shift", peak, tail);
    Check(tail == 0, "feedback: silent after the noise");
  }
}

void ChecksumRun() {
  // The C output through a scenario, for the Blackfin/host comparison.
  static int16_t l[64], r[64];
  fx_b.Init();
#ifdef __bfin__
  fx_b.set_use_asm(false);
#endif
  ShiftParams p = Params(16384, 0, 0, 32767, false);
  rng = 777;
  uint32_t sum = 2166136261u;
  for (int32_t call = 0; call < 3000; ++call) {
    if (call % 40 == 0) {
      p.shift = Range(0, 32767);
      p.feedback = Range(0, 32767);
      p.spread = Range(0, 32767);
      p.mix = Range(0, 32767);
      p.ring = (Next() & 3) == 0;
    }
    const bool mono = (call / 500) & 1;
    const int32_t n = (call % 11 == 0) ? 2 * Range(1, 32) : 64;
    for (int32_t i = 0; i < n; ++i) {
      l[i] = RandomSample();
      r[i] = RandomSample();
    }
    fx_b.Process(l, mono ? nullptr : r, n, p);
    for (int32_t i = 0; i < n; ++i) {
      sum ^= static_cast<uint16_t>(l[i]) | (static_cast<uint32_t>(static_cast<uint16_t>(r[i])) << 16);
      sum *= 16777619u;
    }
  }
  printf("C output checksum %08x\n", sum);
}

}  // namespace

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef __bfin__
  harness::EnableCycleCounter();
  KernelTests();
  InstanceTest();
  IdentityTest(true);
#endif
  IdentityTest(false);
#ifndef __bfin__
  // The measurements: on the host (double), on the C reference, which the
  // Blackfin build shows equal to the assembly bit for bit.
  RejectionTest();
  AccuracyTest();
  SpreadTest();
  RingTest();
  FeedbackTest();
#endif
  FeedbackNoiseTest();
  ChecksumRun();
#ifdef __bfin__
  Cycles();
#endif
  printf("test-shift: %s (%d failures)\n", failures ? "FAILED" : "passed", failures);
  return failures ? 1 : 0;
}
