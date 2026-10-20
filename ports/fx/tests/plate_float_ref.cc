// The plate's float reference and comparison (make compare-plate, host
// only).
//
// FloatPlate is Dattorro's topology written plainly in double precision:
// one ring per line (no shared buffer, no mirroring), the textbook
// allpass, the same signs and lengths. It runs from the same per-call
// control as the fixed-point plate (op1fx::PlateControl: the same
// coefficients, ramps, size fades and crossfades, converted to double), so
// what's left between the two is the fixed point's arithmetic: 16-bit
// storage, rounding, saturation.
//
// For each scenario: the waveform SNR (whole render, and the tail after
// the input stops), the error's RMS in LSBs, the band-energy difference of
// the tail (octave bands, 62.5 Hz .. 11 kHz), the wet/input RMS ratio
// while the input plays, the float render's peaks (wet, and inside the
// tank), and WAVs of both renders in out/plate/. Then the size changes:
// the broadband residue a size switch leaves, crossfaded and hard.

#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "src/plate/plate.h"

using op1fx::PlateCoefs;
using op1fx::PlateParams;

namespace {

const int32_t kRate = 22050;

// --- The float plate --------------------------------------------------------

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

class FloatPlate {
 public:
  double TankPeak() const {
    double p = 0;
    for (const Line* l : { &a1_, &d1_, &a2_, &d2_, &a3_, &d3_, &a4_, &d4_ }) p = std::max(p, l->peak);
    return p;
  }
  void set_size_fade(bool fade) { control_.set_size_fade(fade); }

  void Init() {
    control_.Init();
    pre_.Init(PL_PRE_MAX + 2);
    i1_.Init(PL_I1_LEN);
    i2_.Init(PL_I2_LEN);
    i3_.Init(PL_I3_LEN);
    i4_.Init(PL_I4_LEN);
    a1_.Init(PL_A1_REACH);
    d1_.Init(PL_D1_LEN);
    a2_.Init(PL_A2_LEN);
    d2_.Init(PL_D2_LEN);
    a3_.Init(PL_A3_REACH);
    d3_.Init(PL_D3_LEN);
    a4_.Init(PL_A4_LEN);
    d4_.Init(PL_D4_LEN);
    hp_ = lp1_ = lp2_ = 0;
  }

  void Process(const int16_t* in_l, const int16_t* in_r, double* wet_l, double* wet_r, int32_t n,
               const PlateParams& p) {
    PlateCoefs c;
    control_.Update(p, n, &c);
    const op1fx::PlateSizeTable& t = op1fx::kPlateSizes[c.size];
    const op1fx::PlateSizeTable& tn = op1fx::kPlateSizes[c.size_new];
    const double decay = c.decay / 32768.0, dd1 = c.dd1 / 32768.0, dd2 = c.dd2 / 32768.0;
    const double klp = c.klp / 32768.0, khp = c.khp / 32768.0;
    const double g1 = c.g1 / 32768.0, g2 = c.g2 / 32768.0;
    const double gout = 2.0 * 19661.0 / 32768.0;
    int32_t gain = c.gain0, pre = c.pre0, pos1 = c.mod0[0], pos2 = c.mod0[1], fade = c.fade0;
    int32_t xpos1 = c.xmod0[0], xpos2 = c.xmod0[1], xf = c.xf0;
    for (int32_t i = 0; i < n; ++i) {
      // Pre-delay.
      const double s = in_r ? 0.5 * (in_l[i] + in_r[i]) : in_l[i];
      pre_.Write(s);
      double x;
      {
        const int32_t ip = pre >> 16, f = (pre & 0xffff) >> 1;
        pre += c.pre_step;
        x = pre_.Read(ip) * (32767 - f) / 32768.0 + pre_.Read(ip + 1) * f / 32768.0;
      }
      // Low cut, gain, input diffusers.
      const double y = x - hp_;
      hp_ += khp * y;
      double acc = y * ((gain >> 16) / 32768.0);
      gain += c.gain_step;
      acc = Allpass(&i1_, PL_I1_LEN, g1, acc);
      acc = Allpass(&i2_, PL_I2_LEN, g1, acc);
      acc = Allpass(&i3_, PL_I3_LEN, g2, acc);
      acc = Allpass(&i4_, PL_I4_LEN, g2, acc);
      const double in = acc;

      // A size crossfade: every size-dependent read mixes the old size's
      // (weight wa) and the new one's (wb).
      const double wb = c.xfade ? (xf >> 16) / 32768.0 : 0.0;
      const double wa = c.xfade ? (32767 - (xf >> 16)) / 32768.0 : 1.0;
      xf += c.xf_step;
      auto tail = [&](const Line& l, int32_t j) {
        return c.xfade ? wa * l.Read(t.tail[j]) + wb * l.Read(tn.tail[j]) : l.Read(t.tail[j]);
      };
      // Left half.
      acc = in + decay * tail(d4_, 0);
      acc = ModAllpass(&a1_, pos1, xpos1, wa, wb, c.xfade, dd1, acc);
      pos1 += c.mod_step[0];
      xpos1 += c.xmod_step[0];
      d1_.Write(acc);
      lp1_ += klp * (tail(d1_, 1) - lp1_);
      acc = AllpassAt(&a2_, tail(a2_, 2), dd2, decay * lp1_);
      d2_.Write(acc);
      // Right half.
      acc = in + decay * tail(d2_, 3);
      acc = ModAllpass(&a3_, pos2, xpos2, wa, wb, c.xfade, dd1, acc);
      pos2 += c.mod_step[1];
      xpos2 += c.xmod_step[1];
      d3_.Write(acc);
      lp2_ += klp * (tail(d3_, 4) - lp2_);
      acc = AllpassAt(&a4_, tail(a4_, 5), dd2, decay * lp2_);
      d4_.Write(acc);

      // Taps: left D3 D3 A4 D4 D1 A2 D2, right D1 D1 A2 D2 D3 A4 D4.
      const Line* left[7] = { &d3_, &d3_, &a4_, &d4_, &d1_, &a2_, &d2_ };
      const Line* right[7] = { &d1_, &d1_, &a2_, &d2_, &d3_, &a4_, &d4_ };
      double ol = 0, orr = 0;
      for (int32_t j = 0; j < 7; ++j) {
        ol += op1fx::kPlateTapSign[j] * left[j]->Read(t.tap_l[j]);
        orr += op1fx::kPlateTapSign[j] * right[j]->Read(t.tap_r[j]);
      }
      const double g = (fade >> 16) / 32768.0;
      fade += c.fade_step;
      const bool faded = c.fade0 != op1fx::PlateControl::kFadeOne || c.fade_step != 0;
      const double gain_out = c.xfade ? 0.0 : (faded ? g : 1.0);
      wet_l[i] = gout * ol * gain_out;
      if (wet_r) wet_r[i] = gout * orr * gain_out;

      for (Line* l : { &pre_, &i1_, &i2_, &i3_, &i4_, &a1_, &d1_, &a2_, &d2_, &a3_, &d3_, &a4_, &d4_ }) {
        l->Step();
      }
    }
  }

 private:
  // w = x - g prev; y = prev + g w.
  static double Allpass(Line* line, int32_t delay, double g, double x) {
    return AllpassAt(line, line->Read(delay), g, x);
  }
  static double AllpassAt(Line* line, double prev, double g, double x) {
    const double w = x - g * prev;
    line->Write(w);
    return prev + g * w;
  }
  static double TapAt(const Line& line, int32_t pos) {
    const int32_t ip = pos >> 16, f = (pos & 0xffff) >> 1;
    return line.Read(ip) * (32767 - f) / 32768.0 + line.Read(ip + 1) * f / 32768.0;
  }
  // Dattorro's decay diffusion 1, the other sign: w = x + g m; y = m - g w.
  // In a crossfade, m mixes the old and the new size's delays.
  static double ModAllpass(Line* line, int32_t pos, int32_t xpos, double wa, double wb, bool xfade,
                           double g, double x) {
    const double m = xfade ? wa * TapAt(*line, pos) + wb * TapAt(*line, xpos) : TapAt(*line, pos);
    const double w = x + g * m;
    line->Write(w);
    return m - g * w;
  }

  op1fx::PlateControl control_;
  Line pre_, i1_, i2_, i3_, i4_, a1_, d1_, a2_, d2_, a3_, d3_, a4_, d4_;
  double hp_, lp1_, lp2_;
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
  std::string path = "out/plate/" + name;
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

// Octave band energies (Hann-windowed 4096-point frames, averaged) over
// [a, b): bands 62.5-125 Hz .. 5.5-11 kHz (8 bands).
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
  int32_t kind;           // 0 impulse, 1 noise, 2 saw, 3 sine (220 Hz)
  int32_t decay_permille; // tank gain
  int32_t motion, predelay, lowcut, damp, size;
};

int32_t DecayFor(int32_t g_permille) {
  return ((g_permille - 200) * 32767 + 390) / 780;
}

struct Render {
  std::vector<double> fl, fr;     // fixed
  std::vector<double> rl, rr;     // float
  std::vector<double> in;         // the mono input
  double tank_peak = 0;           // float: the largest value written in the tank
};

Render Run(const Scenario& s, int32_t switch_call = -1, int32_t switch_size = 0, bool fade = true) {
  op1fx::PlateReverb* fixed = new op1fx::PlateReverb;
  FloatPlate* ref = new FloatPlate;
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
    PlateParams p;
    p.decay = DecayFor(s.decay_permille);
    p.damp = s.damp;
    p.predelay = s.predelay;
    p.size = (switch_call >= 0 && call >= switch_call) ? switch_size : s.size;
    p.diffusion = 32767;
    p.motion = s.motion;
    p.lowcut = s.lowcut;
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
          phase += 0x00a3d70a;   // ~220 Hz
          x = static_cast<int16_t>((static_cast<int32_t>(phase) >> 16) * 10000 >> 15);
        }
        if (s.kind == 3) {
          phase += 0x00a3d70a;
          x = static_cast<int16_t>(std::lrint(10000 * std::sin(2 * M_PI * phase / 4294967296.0)));
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
  out.tank_peak = ref->TankPeak();
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
  // Band difference of the tail, both sides, bands within 60 dB of the loudest.
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
  printf("%-20s SNR L %4.1f R %4.1f | tail L %4.1f R %4.1f dB | err %4.2f LSB | bands %4.2f dB |"
         " wet/in %4.2f | peaks wet %5.0f tank %5.0f\n",
         s.name, snr_l, snr_r, tsnr_l, tsnr_r, err_rms, worst, in_rms > 0 ? wet_rms / in_rms : 0.0, peak,
         r.tank_peak);
  WriteWav(WavPath(std::string(s.name) + "_fixed"), r.fl, r.fr);
  WriteWav(WavPath(std::string(s.name) + "_float"), r.rl, r.rr);
}

// The click a size switch leaves. A sine's reverb is (nearly) a sine; a
// third difference (x w^3: -74 dB at 220 Hz) shows what's broadband. Per
// 10 ms window: its peak against the signal's peak over the second before
// the switch (dBc). Reported: the worst window in that second, in the
// 400 ms after the switch, and the same without the fade and crossfade (a
// hard switch), to show the measure sees clicks; and where the worst is.
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
  const Scenario s = { name, 4.0, 4.0, false, false, kind, 900, 16384, 0, 0, 8192, from };
  const int32_t switch_call = static_cast<int32_t>(2.0 * kRate / 32);
  const size_t sw = static_cast<size_t>(switch_call) * 32;
  printf("%-18s %ld -> %ld:", name, static_cast<long>(from), static_cast<long>(to));
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
    // name                  s     in     mono   mono   kind decay motion  pre    lowcut damp   size
    { "impulse decay .5",     4.0, 0.001, false, false, 0, 500,  16384, 0,     0,     8192,  7 },
    { "impulse still",        4.0, 0.001, false, false, 0, 700,  0,     0,     0,     8192,  7 },
    { "noise burst .85",      5.0, 0.2,   false, false, 1, 850,  16384, 0,     0,     8192,  7 },
    { "saw decay .5",         4.0, 1.5,   false, false, 2, 500,  16384, 0,     0,     8192,  7 },
    { "saw decay .95",        8.0, 1.5,   false, false, 2, 950,  16384, 0,     0,     8192,  7 },
    { "saw .95 motion max",   8.0, 1.5,   false, false, 2, 950,  32767, 0,     0,     8192,  7 },
    { "saw .95 dark small",   6.0, 1.5,   false, false, 2, 950,  16384, 0,     0,     26000, 0 },
    { "noise mono",           5.0, 0.2,   true,  true,  1, 850,  16384, 0,     0,     8192,  7 },
    { "noise pre lowcut",     5.0, 0.2,   false, false, 1, 850,  16384, 16384, 16384, 8192,  4 },
    { "noise bright .98",    12.0, 2.0,   false, false, 1, 980,  16384, 0,     0,     0,     7 },
  };
  printf("fixed point vs float reference (same control), 22050 Hz:\n");
  for (const Scenario& s : scenarios) Report(s, Run(s));
  printf("\nsize changes (fade out 4 calls, crossfade 256 samples muted, fade in 8 calls), decay .9,\n"
         "input held; broadband peak (3rd difference) against the signal, worst 10 ms window:\n");
  SizeClick("size sine 7 to 3", 3, 7, 3);
  SizeClick("size sine 3 to 7", 3, 3, 7);
  SizeClick("size sine 7 to 6", 3, 7, 6);
  SizeClick("size sine 0 to 7", 3, 0, 7);
  SizeClick("size noise 7 to 0", 1, 7, 0);
  return 0;
}
