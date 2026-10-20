// The hall's float reference and comparison (make compare-hall, host only).
//
// FloatHall is the same FDN written plainly in double precision: one ring
// per line (no shared buffer, no mirroring), the textbook allpass, exact
// Householder, the same lengths, signs and modulation. It runs from the
// same per-call control as the fixed-point hall (op1fx::HallControl: the
// same coefficients, ramps and crossfades, converted to double), so what's
// left between the two is the fixed point's arithmetic: 16-bit storage,
// rounding, saturation.
//
// For each scenario: the waveform SNR (whole render, and the tail after
// the input stops), the error's RMS in LSBs, the band-energy difference of
// the tail (octave bands), the wet/input RMS ratio while the input plays,
// the float render's peaks (wet, and inside the lines), and WAVs of both
// renders in out/hall/. Then: RT60 measured on impulse responses against
// the DECAY knob's nominal value, the onset (latency) of the early and the
// late parts, and the size changes: the broadband residue a size switch
// leaves, crossfaded and hard.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "src/hall/hall.h"

using op1fx::HallCoefs;
using op1fx::HallParams;

namespace {

const int32_t kRate = 22050;

struct Line {
  std::vector<double> b;
  int64_t t = 0;
  double peak = 0;
  void Init(int32_t max_delay) {
    b.assign(max_delay + 2, 0.0);
    t = 0;
    peak = 0;
  }
  // The value written d samples ago (d >= 1; d = 0: this sample's write).
  double Read(int32_t d) const {
    const int64_t n = static_cast<int64_t>(b.size());
    return b[static_cast<size_t>(((t - d) % n + n) % n)];
  }
  void Write(double x) {
    b[static_cast<size_t>(t % static_cast<int64_t>(b.size()))] = x;
    if (std::fabs(x) > peak) peak = std::fabs(x);
  }
  void Step() { ++t; }
};

const int32_t kLineMax[8] = { HL_L0_LEN, HL_L1_LEN, HL_L2_LEN, HL_L3_LEN,
                              HL_L4_LEN, HL_L5_LEN, HL_L6_LEN, HL_L7_LEN };

class FloatHall {
 public:
  double LinePeak() const {
    double p = 0;
    for (const Line& l : lines_) p = std::max(p, l.peak);
    return p;
  }
  void set_size_fade(bool fade) { control_.set_size_fade(fade); }

  void Init() {
    control_.Init();
    pre_.Init(HL_PRE_MAX + 2);
    ap_[0].Init(HL_AP1_LEN);
    ap_[1].Init(HL_AP2_LEN);
    ap_[2].Init(HL_AP3_LEN);
    er_.Init(HL_ER_REACH);
    for (int j = 0; j < 8; ++j) {
      lines_[j].Init(kLineMax[j] + HL_EXC_MAX + 1);
      z_[j] = 0;
    }
  }

  void Process(const int16_t* in_l, const int16_t* in_r, double* wet_l, double* wet_r, int32_t n,
               const HallParams& p) {
    HallCoefs c;
    control_.Update(p, n, &c);
    const op1fx::HallSizeTable& t = op1fx::kHallSizes[c.size];
    const op1fx::HallSizeTable& tn = op1fx::kHallSizes[c.size_new];
    const double g = c.dg / 32768.0;
    const double gl = c.gl / 32768.0, wa = c.wa / 32768.0, wb = c.wb / 32768.0;
    int32_t gain = c.gain0, pre = c.pre0, xf = c.xf0;
    int32_t pos[2] = { c.mod0[0], c.mod0[1] };
    for (int32_t i = 0; i < n; ++i) {
      const double s = in_r ? 0.5 * (in_l[i] + in_r[i]) : in_l[i];
      pre_.Write(s);
      double x;
      {
        const int32_t ip = pre >> 16, f = (pre & 0xffff) >> 1;
        pre += c.pre_step;
        x = pre_.Read(ip) * (32767 - f) / 32768.0 + pre_.Read(ip + 1) * f / 32768.0;
      }
      const double xg = x * ((gain >> 16) / 32768.0);
      gain += c.gain_step;
      er_.Write(xg);
      double acc = xg;
      for (int j = 0; j < 3; ++j) {
        const int32_t len = j == 0 ? HL_AP1_LEN : (j == 1 ? HL_AP2_LEN : HL_AP3_LEN);
        const double prev = ap_[j].Read(len);
        const double w = acc - g * prev;
        ap_[j].Write(w);
        acc = prev + g * w;
      }
      const double d = 0.5 * acc;

      const double xb = c.xfade ? (xf >> 16) / 32768.0 : 0.0;
      const double xa = c.xfade ? (32767 - (xf >> 16)) / 32768.0 : 1.0;
      xf += c.xf_step;

      // The lines: line j's slot gives its modulation group (A or B) and
      // sign: delay = len +- the excursion, interpolated as the fixed point.
      double y[8];
      for (int s = 0; s < 8; ++s) {
        const int line = op1fx::kHallSlotLine[s];
        const int m = s >> 2;
        const bool neg = (s & 2) != 0;
        const int32_t ip = pos[m] >> 16, f = (pos[m] & 0xffff) >> 1;
        auto read = [&](int32_t len) {
          if (neg) {
            const int32_t q = len - ip - 1;
            return lines_[line].Read(q) * f / 32768.0 + lines_[line].Read(q + 1) * (32767 - f) / 32768.0;
          }
          const int32_t q = len + ip;
          return lines_[line].Read(q) * (32767 - f) / 32768.0 + lines_[line].Read(q + 1) * f / 32768.0;
        };
        y[line] = c.xfade ? xa * read(t.len[line]) + xb * read(tn.len[line]) : read(t.len[line]);
      }
      pos[0] += c.mod_step[0];
      pos[1] += c.mod_step[1];
      double sum = 0;
      for (int j = 0; j < 8; ++j) {
        z_[j] = z_[j] * (c.a[j] / 32768.0) + y[j] * (c.b[j] / 32768.0);
        sum += z_[j];
      }
      for (int s = 0; s < 8; ++s) {
        const int line = op1fx::kHallSlotLine[s];
        lines_[line].Write(z_[line] - 0.25 * sum + op1fx::kHallSlotSign[s] * d);
      }

      double el = 0, er = 0, nl = 0, nr = 0;
      for (int k = 0; k < 5; ++k) {
        const double ck = c.c[k] / 32768.0;
        el += ck * er_.Read(t.er_l[k]);
        er += ck * er_.Read(t.er_r[k]);
        nl += ck * er_.Read(tn.er_l[k]);
        nr += ck * er_.Read(tn.er_r[k]);
      }
      if (c.xfade) {
        el = xa * el + xb * nl;
        er = xa * er + xb * nr;
      }
      const double ml = gl * 0.5 * (y[0] + y[2] + y[4] + y[6]) + el;
      const double mr = gl * 0.5 * (y[1] + y[3] + y[5] + y[7]) + er;
      if (wet_r) {
        wet_l[i] = 2 * (ml * wa + mr * wb);
        wet_r[i] = 2 * (ml * wb + mr * wa);
      } else {
        wet_l[i] = 2 * ml;
      }
      pre_.Step();
      er_.Step();
      for (Line& l : ap_) l.Step();
      for (Line& l : lines_) l.Step();
    }
  }

 private:
  op1fx::HallControl control_;
  Line pre_, er_, ap_[3], lines_[8];
  double z_[8];
};

// --- Signals, WAVs, measures --------------------------------------------------

uint32_t noise_state = 1;
int16_t Noise(int32_t amp) {
  noise_state = noise_state * 1664525u + 1013904223u;
  return static_cast<int16_t>((static_cast<int32_t>(noise_state) >> 16) * amp >> 15);
}

void WriteWav(const std::string& path, const std::vector<double>& l, const std::vector<double>& r) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  const uint32_t frames = static_cast<uint32_t>(l.size());
  const uint32_t data = frames * 4;
  auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f);
  u32(36 + data);
  fwrite("WAVEfmt ", 1, 8, f);
  u32(16);
  u16(1);
  u16(2);
  u32(kRate);
  u32(kRate * 4);
  u16(4);
  u16(16);
  fwrite("data", 1, 4, f);
  u32(data);
  for (uint32_t i = 0; i < frames; ++i) {
    for (double v : { l[i], r[i] }) {
      const double c = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
      const int16_t s = static_cast<int16_t>(std::lrint(c));
      fwrite(&s, 2, 1, f);
    }
  }
  fclose(f);
}

std::string WavPath(const std::string& name) {
  std::string path = "out/hall/" + name;
  for (char& ch : path) {
    if (ch == ' ' || ch == ',' || ch == '.') ch = '_';
  }
  return path + ".wav";
}

double Snr(const std::vector<double>& ref, const std::vector<double>& fix, size_t a, size_t b) {
  double s = 0, e = 0;
  for (size_t i = a; i < b && i < ref.size(); ++i) {
    s += ref[i] * ref[i];
    e += (fix[i] - ref[i]) * (fix[i] - ref[i]);
  }
  if (e == 0) return 999;
  return 10 * std::log10(s / e);
}

double Rms(const std::vector<double>& x, size_t a, size_t b) {
  double s = 0;
  size_t n = 0;
  for (size_t i = a; i < b && i < x.size(); ++i, ++n) s += x[i] * x[i];
  return n ? std::sqrt(s / n) : 0;
}

void Fft(std::vector<std::complex<double>>& a) {
  const size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    const double ang = -2 * M_PI / len;
    const std::complex<double> wl(std::cos(ang), std::sin(ang));
    for (size_t i = 0; i < n; i += len) {
      std::complex<double> w(1);
      for (size_t j = 0; j < len / 2; ++j) {
        const std::complex<double> u = a[i + j], v = a[i + j + len / 2] * w;
        a[i + j] = u + v;
        a[i + j + len / 2] = u - v;
        w *= wl;
      }
    }
  }
}

std::vector<double> Bands(const std::vector<double>& x, size_t a, size_t b) {
  const size_t N = 4096;
  std::vector<double> e(8, 0.0);
  for (size_t s = a; s + N <= b && s + N <= x.size(); s += N / 2) {
    std::vector<std::complex<double>> f(N);
    for (size_t i = 0; i < N; ++i) f[i] = x[s + i] * (0.5 - 0.5 * std::cos(2 * M_PI * i / N));
    Fft(f);
    for (size_t k = 1; k < N / 2; ++k) {
      const double hz = static_cast<double>(k) * kRate / N;
      const int band = static_cast<int>(std::floor(std::log2(hz / 62.5)));
      if (band >= 0 && band < 8) e[band] += std::norm(f[k]);
    }
  }
  return e;
}

// --- Scenarios -----------------------------------------------------------------

struct Scenario {
  const char* name;
  double seconds;
  double input_seconds;   // the input stops here (the tail starts)
  bool mono_in, mono_out;
  int32_t kind;           // 0 impulse, 1 noise, 2 saw, 3 sine (220 Hz), 4 4 kHz, 5 250 Hz
  int32_t size, decay, damp, predelay, early, motion, width;
};

struct Render {
  std::vector<double> fl, fr;     // fixed
  std::vector<double> rl, rr;     // float
  std::vector<double> in;         // the mono input
  double line_peak = 0;
};

Render Run(const Scenario& s, int32_t switch_call = -1, int32_t switch_size = 0, bool fade = true) {
  op1fx::HallReverb* fixed = new op1fx::HallReverb;
  FloatHall* ref = new FloatHall;
  fixed->Init();
  ref->Init();
  fixed->set_size_fade(fade);
  ref->set_size_fade(fade);
  noise_state = 1;
  Render out;
  const int32_t n = 32;
  const int32_t calls = static_cast<int32_t>(s.seconds * kRate / n);
  const int32_t input_end = static_cast<int32_t>(s.input_seconds * kRate);
  uint32_t phase = 0;
  for (int32_t call = 0; call < calls; ++call) {
    HallParams p;
    p.size = (switch_call >= 0 && call >= switch_call) ? switch_size : s.size;
    p.decay = s.decay;
    p.damp = s.damp;
    p.predelay = s.predelay;
    p.early = s.early;
    p.motion = s.motion;
    p.width = s.width;
    p.freeze = false;
    int16_t l[n], r[n], wl[n], wr[n];
    for (int32_t i = 0; i < n; ++i) {
      const int32_t t = call * n + i;
      int16_t x = 0, y = 0;
      if (t < input_end) {
        if (s.kind == 0) x = t == 0 ? 16384 : 0;
        if (s.kind == 1) {
          x = Noise(12000);
          y = Noise(12000);
        }
        if (s.kind == 2) {
          phase += 0x00a3d70a;
          x = static_cast<int16_t>((static_cast<int32_t>(phase) >> 16) * 10000 >> 15);
        }
        if (s.kind == 3) {
          phase += 0x00a3d70a;
          x = static_cast<int16_t>(std::lrint(10000 * std::sin(2 * M_PI * phase / 4294967296.0)));
        }
        if (s.kind == 4 || s.kind == 5) {   // 4 kHz, 250 Hz
          const double hz = s.kind == 4 ? 4000.0 : 250.0;
          x = static_cast<int16_t>(std::lrint(10000 * std::sin(2 * M_PI * hz * t / kRate)));
        }
        if (s.kind != 1) y = x;
      }
      l[i] = x;
      r[i] = s.mono_in ? x : y;
      out.in.push_back(s.mono_in ? x : 0.5 * (l[i] + r[i]));
    }
    double dl[n], dr[n];
    fixed->Process(l, s.mono_in ? nullptr : r, wl, s.mono_out ? nullptr : wr, n, p);
    ref->Process(l, s.mono_in ? nullptr : r, dl, s.mono_out ? nullptr : dr, n, p);
    for (int32_t i = 0; i < n; ++i) {
      out.fl.push_back(wl[i]);
      out.fr.push_back(s.mono_out ? wl[i] : wr[i]);
      out.rl.push_back(dl[i]);
      out.rr.push_back(s.mono_out ? dl[i] : dr[i]);
    }
  }
  out.line_peak = ref->LinePeak();
  delete fixed;
  delete ref;
  return out;
}

void Report(const Scenario& s, const Render& r) {
  const size_t total = r.fl.size();
  const size_t tail = static_cast<size_t>((s.input_seconds + 0.1) * kRate);
  const double snr_l = Snr(r.rl, r.fl, 0, total), snr_r = Snr(r.rr, r.fr, 0, total);
  const double tsnr_l = Snr(r.rl, r.fl, tail, total), tsnr_r = Snr(r.rr, r.fr, tail, total);
  double e2 = 0;
  for (size_t i = 0; i < total; ++i) {
    e2 += (r.fl[i] - r.rl[i]) * (r.fl[i] - r.rl[i]) + (r.fr[i] - r.rr[i]) * (r.fr[i] - r.rr[i]);
  }
  const double err_rms = std::sqrt(e2 / (2.0 * total));
  double worst = 0;
  for (int side = 0; side < 2; ++side) {
    const std::vector<double> bf = Bands(side ? r.fr : r.fl, tail, total);
    const std::vector<double> br = Bands(side ? r.rr : r.rl, tail, total);
    double top = 0;
    for (double e : br) top = std::max(top, e);
    for (int b = 0; b < 8; ++b) {
      if (br[b] < top * 1e-6 || br[b] == 0) continue;
      worst = std::max(worst, std::fabs(10 * std::log10(bf[b] / br[b])));
    }
  }
  const size_t in_end = static_cast<size_t>(s.input_seconds * kRate);
  const double in_rms = Rms(r.in, 0, in_end);
  const double wet_rms = 0.5 * (Rms(r.rl, in_end / 2, in_end) + Rms(r.rr, in_end / 2, in_end));
  double peak = 0;
  for (size_t i = 0; i < total; ++i) peak = std::max(peak, std::max(std::fabs(r.rl[i]), std::fabs(r.rr[i])));
  // L/R correlation of the tail (float).
  double sll = 0, srr = 0, slr = 0;
  for (size_t i = tail; i < total; ++i) {
    sll += r.rl[i] * r.rl[i];
    srr += r.rr[i] * r.rr[i];
    slr += r.rl[i] * r.rr[i];
  }
  const double corr = (sll > 0 && srr > 0) ? slr / std::sqrt(sll * srr) : 0;
  printf("%-22s SNR L %4.1f R %4.1f | tail L %4.1f R %4.1f dB | err %5.2f LSB | bands %4.2f dB |"
         " wet/in %4.2f | peaks wet %5.0f lines %5.0f | L/R corr %5.2f\n",
         s.name, snr_l, snr_r, tsnr_l, tsnr_r, err_rms, worst, in_rms > 0 ? wet_rms / in_rms : 0.0, peak,
         r.line_peak, corr);
  WriteWav(WavPath(std::string(s.name) + "_fixed"), r.fl, r.fr);
  WriteWav(WavPath(std::string(s.name) + "_float"), r.rl, r.rr);
}

// RT60 by Schroeder's backward integration (L + R) from `start`, fitted
// between -5 and -35 dB (T30), or -5 and -25 (T20) if the response doesn't
// reach -35 well before the end.
double MeasureRt60(const std::vector<double>& l, const std::vector<double>& r, size_t start = 0) {
  const size_t n = l.size();
  std::vector<double> edc(n, 0.0);
  double acc = 0;
  for (size_t i = n; i-- > start;) {
    acc += l[i] * l[i] + r[i] * r[i];
    edc[i] = acc;
  }
  const double top = edc[start];
  auto db = [&](size_t i) { return 10 * std::log10(edc[i] / top + 1e-30); };
  for (double lo : { -35.0, -25.0 }) {
    size_t a = start, b = 0;
    while (a < n && db(a) > -5) ++a;
    b = a;
    while (b < n && db(b) > lo) ++b;
    if (b >= start + (n - start) * 8 / 10) continue;
    const double slope = (lo + 5) / ((b - a) / static_cast<double>(kRate));   // dB per s
    return -60 / slope;
  }
  return -1;
}

double NominalRt60(int32_t decay) {
  if (decay >= op1fx::kHallInfinite) return INFINITY;
  return 0.3 * std::pow(20 / 0.3, decay / static_cast<double>(op1fx::kHallInfinite));
}

double Db(double x) { return 10 * std::log10(x + 1e-30); }

void Rt60s() {
  printf("\nRT60 (a 50 ms noise burst at -8 dBFS, motion 0, early 0, damp 0; Schroeder from the\n"
         "burst's end; nominal: float | fixed):\n");
  const int32_t decays[] = { 0, 8192, 16384, 24576, 30000, 32000 };
  const int32_t sizes[] = { 0, 16384, 32767 };
  for (int32_t size : sizes) {
    printf("  size %5ld:", static_cast<long>(size));
    for (int32_t d : decays) {
      const double nominal = NominalRt60(d);
      const double secs = std::min(60.0, std::max(3.0, 2.0 * nominal));
      const Scenario s = { "rt60", secs, 0.05, false, false, 1, size, d, 0, 0, 0, 0, 32767 };
      const Render r = Run(s);
      const size_t st = static_cast<size_t>(0.05 * kRate);
      printf("  %5.2f: %5.2f | %5.2f", nominal, MeasureRt60(r.rl, r.rr, st), MeasureRt60(r.fl, r.fr, st));
    }
    printf("\n");
  }
  // The fixed point's floor: what's left 3 x RT60 after a burst (180 dB
  // down: nothing, in exact arithmetic): rounding's noise and limit cycles,
  // in LSB RMS of the wet.
  printf("  idle floor 3 x RT60 after a burst, LSB RMS of the wet (fixed; float < 1e-6):\n");
  for (int32_t d : { 0, 16384, 24576, 30000 }) {
    printf("    RT60 %5.2f s:", NominalRt60(d));
    for (int32_t size : { 0, 32767 }) {
      for (int32_t motion : { 0, 16384 }) {
        const double rt = NominalRt60(d);
        const Scenario s = { "floor", 3.0 * rt + 1.05, 0.05, false, false, 1, size, d, 0, 0, 0, motion, 32767 };
        const Render r = Run(s);
        const size_t b = r.fl.size(), a = b - kRate;
        printf("  size %5ld motion %5ld: %6.2f", static_cast<long>(size), static_cast<long>(motion),
               0.5 * (Rms(r.fl, a, b) + Rms(r.fr, a, b)));
      }
    }
    printf("\n");
  }
  printf("  damp: RT60 of a 250 Hz and a 4 kHz sine burst (1 s), decay 16384 (2.49 s), size 32767,\n"
         "  motion 0 (float); the design's ratio is DC : Nyquist, so 4 kHz sees less of it:\n");
  for (int32_t damp : { 0, 8192, 16384, 24576, 32767 }) {
    double rt[2];
    for (int k = 0; k < 2; ++k) {
      const Scenario s = { "damp", 7.0, 1.0, false, false, k ? 4 : 5, 32767, 16384, damp, 0, 0, 0, 32767 };
      const Render r = Run(s);
      rt[k] = MeasureRt60(r.rl, r.rr, static_cast<size_t>(kRate));
    }
    printf("    damp %5ld: 250 Hz %5.2f s, 4 kHz %5.2f s, ratio %4.1f (DC : Nyquist %4.1f)\n",
           static_cast<long>(damp), rt[0], rt[1], rt[0] / rt[1], std::pow(10.0, damp / 32767.0));
  }
  // Early against late: an impulse's energy, early only (EARLY 32767) and
  // late only (EARLY 0), both at full gain (EARLY 16384 has both).
  printf("  energy of an impulse's response (float), early only : late only, by decay:\n");
  for (int32_t d : { 8192, 16384, 24576 }) {
    double e[2];
    for (int k = 0; k < 2; ++k) {
      const Scenario s = { "bal", 8.0, 0.001, false, false, 0, 32767, d, 12000, 0, k ? 0 : 32767, 0, 32767 };
      const Render r = Run(s);
      double sum = 0;
      for (size_t i = 0; i < r.rl.size(); ++i) sum += r.rl[i] * r.rl[i] + r.rr[i] * r.rr[i];
      e[k] = sum;
    }
    printf("    decay %5ld (RT60 %5.2f s): early %5.1f dB, late %5.1f dB (re a full-scale sample)\n",
           static_cast<long>(d), NominalRt60(d), Db(e[0] / (2 * 16384.0 * 16384.0)),
           Db(e[1] / (2 * 16384.0 * 16384.0)));
  }
}

// The first sample above -60 dB of the peak (impulse), in ms.
void Onsets() {
  printf("\nonset after the input (impulse at t = 0, pre-delay 0), ms, L | R:\n");
  for (int32_t size : { 0, 16384, 32767 }) {
    for (int32_t early : { 16384, 0 }) {
      const Scenario s = { "onset", 0.5, 0.001, false, false, 0, size, 16384, 8192, 0, early, 0, 32767 };
      const Render r = Run(s);
      double on[2] = { -1, -1 };
      for (int ch = 0; ch < 2; ++ch) {
        const std::vector<double>& x = ch ? r.fr : r.fl;
        double pk = 0;
        for (double v : x) pk = std::max(pk, std::fabs(v));
        for (size_t i = 0; i < x.size(); ++i) {
          if (std::fabs(x[i]) > pk * 1e-3) {
            on[ch] = 1000.0 * i / kRate;
            break;
          }
        }
      }
      printf("  size %5ld %-10s %5.1f | %5.1f\n", static_cast<long>(size),
             early ? "early+late" : "late only", on[0], on[1]);
    }
  }
}

double D3Peak(const std::vector<double>& x, size_t a, size_t b, size_t* at) {
  const size_t w = kRate / 100;
  double worst = 0;
  for (size_t s = a; s + w <= b; s += w / 2) {
    double d3 = 0;
    for (size_t i = s + 3; i < s + w; ++i) {
      d3 = std::max(d3, std::fabs(x[i] - 3 * x[i - 1] + 3 * x[i - 2] - x[i - 3]));
    }
    if (d3 > worst) {
      worst = d3;
      if (at) *at = s;
    }
  }
  return worst;
}

void SizeClick(const char* name, int32_t kind, int32_t from, int32_t to) {
  const Scenario s = { name, 4.0, 4.0, false, false, kind, from, 24000, 8192, 0, 16384, 16384, 32767 };
  const int32_t switch_call = static_cast<int32_t>(2.0 * kRate / 32);
  const size_t sw = static_cast<size_t>(switch_call) * 32;
  printf("%-20s %5ld -> %5ld:", name, static_cast<long>(from), static_cast<long>(to));
  for (int fade = 1; fade >= 0; --fade) {
    const Render r = Run(s, switch_call, to, fade != 0);
    double before = -999, after = -999;
    size_t at = 0;
    for (int side = 0; side < 2; ++side) {
      const std::vector<double>& x = side ? r.fr : r.fl;
      double pk = 1e-9;
      for (size_t i = sw - kRate; i < sw; ++i) pk = std::max(pk, std::fabs(x[i]));
      size_t a = 0;
      before = std::max(before, 20 * std::log10(D3Peak(x, sw - kRate, sw, nullptr) / pk));
      const double db = 20 * std::log10(D3Peak(x, sw, sw + 2 * kRate / 5, &a) / pk);
      if (db > after) {
        after = db;
        at = a;
      }
    }
    if (fade) {
      printf("  before %5.1f dBc, after: crossfaded %5.1f dBc (at +%3.0f ms)", before, after,
             1000.0 * at / kRate - 1000.0 * sw / kRate);
    } else {
      printf(", hard switch %5.1f dBc", after);
    }
    WriteWav(WavPath(std::string(name) + (fade ? "" : "_hard") + "_fixed"), r.fl, r.fr);
  }
  printf("\n");
}

}  // namespace

int main() {
  const Scenario scenarios[] = {
    // name                    s     in     mono   mono   kind size   decay  damp   pre    early  motion width
    { "impulse hall",          5.0, 0.001, false, false, 0, 32767, 20000, 12000, 0,     16384, 16384, 32767 },
    { "impulse room still",    3.0, 0.001, false, false, 0, 4096,  16384, 12000, 0,     16384, 0,     32767 },
    { "noise burst",           5.0, 0.2,   false, false, 1, 24576, 22000, 12000, 0,     16384, 16384, 32767 },
    { "saw short",             4.0, 1.5,   false, false, 2, 24576, 12000, 12000, 0,     16384, 16384, 32767 },
    { "saw long",              8.0, 1.5,   false, false, 2, 32767, 28000, 12000, 0,     16384, 16384, 32767 },
    { "saw long motion max",   8.0, 1.5,   false, false, 2, 32767, 28000, 12000, 0,     16384, 32767, 32767 },
    { "saw dark small",        6.0, 1.5,   false, false, 2, 0,     28000, 30000, 0,     16384, 16384, 32767 },
    { "noise mono",            5.0, 0.2,   true,  true,  1, 24576, 22000, 12000, 0,     16384, 16384, 32767 },
    { "noise pre early only",  5.0, 0.2,   false, false, 1, 16384, 22000, 12000, 16384, 32767, 16384, 32767 },
    { "noise late narrow",     5.0, 0.2,   false, false, 1, 16384, 22000, 12000, 0,     0,     16384, 8192 },
    { "noise bright 20 s",    12.0, 2.0,   false, false, 1, 32767, 32000, 0,     0,     16384, 16384, 32767 },
  };
  printf("fixed point vs float reference (same control), 22050 Hz:\n");
  for (const Scenario& s : scenarios) Report(s, Run(s));
  Rt60s();
  Onsets();
  printf("\nsize changes (crossfade 256 samples, output included), decay 24000, input held;\n"
         "broadband peak (3rd difference) against the signal, worst 10 ms window:\n");
  SizeClick("size sine 15 to 7", 3, 32767, 15000);
  SizeClick("size sine 7 to 15", 3, 15000, 32767);
  SizeClick("size sine 15 to 14", 3, 32767, 30000);
  SizeClick("size sine 0 to 15", 3, 0, 32767);
  SizeClick("size noise 15 to 0", 1, 32767, 0);
  return 0;
}
