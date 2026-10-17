// The float reference for the Streams effect (host only): upstream's own
// processors (mutableinstruments/streams, namespace streams), unmodified, at
// their native 31089 Hz, driving an ideal model of the analog half at
// 44.1 kHz:
//   1. detector: the same SENSE gain and peak envelope as the port, in
//      double precision (src/core/core_config.h);
//   2. EXCITE resampled 44.1 kHz -> 31089 Hz (cubic Hermite), as Streams'
//      ADC would sample it;
//   3. upstream Processor at 31089 Hz, configured as the port configures it;
//   4. its gain and frequency codes interpolated (linear) back to 44.1 kHz,
//      standing in for the DAC/PWM smoothing;
//   5. VCA with exact exp2/linear response; zero-delay-feedback SVF with the
//      exact tan() per sample; output clipped to 16 bits; dry/wet mix.
// So the port is checked for its retiming (processors at 44.1 kHz), its
// fixed-point VCA and filter, and its coefficient tables, all at once.

#ifndef OP1STREAMS_HARNESS_REF_MODEL_H_
#define OP1STREAMS_HARNESS_REF_MODEL_H_

#include <cmath>
#include <cstring>
#include <vector>

#include "streams/processor.h"

#include "src/core/core_config.h"
#include "src/core/streams_core.h"

namespace harness {

struct RefOutput {
  std::vector<float> l, r;          // output, +-1
  std::vector<float> gain_db;       // VCA gain (with trim), dB
  std::vector<float> cutoff_oct;    // cutoff, octaves above 20 Hz (-1: bypassed)
};

inline uint16_t RefKnobToPot(int v) {
  if (v < 0) v = 0;
  return static_cast<uint16_t>((v << 1) | (v >> 14));
}

inline double RefVcaGain(double code, double response) {
  double lin = code / 32767.0;
  double log2_gain = (code - 32767.0) / op1streams::kVcaCodesPerOctave;
  double max_log2 = op1streams::kVcaMaxLog2Q16 / 65536.0;
  if (log2_gain > max_log2) log2_gain = max_log2;
  double ex = log2_gain < -16.0 ? 0.0 : std::pow(2.0, log2_gain);
  double g = ex + (lin - ex) * response;
  return g > 8.0 ? 8.0 : g;
}

class RefEffect {
 public:
  // x: input (int16 values), n samples per channel at 44.1 kHz.
  void Render(const op1streams::Controls& c, bool stereo,
              const std::vector<int16_t>& xl, const std::vector<int16_t>& xr,
              RefOutput* out) {
    using namespace op1streams;
    const size_t n = xl.size();
    const int f = c.function;
    // 1. Detector.
    std::vector<double> d(n);
    double sense = std::pow(2.0, kSenseOctaves * c.sense / 32768.0);
    bool envelope = c.detector == DETECTOR_ENVELOPE &&
        f != PROCESSOR_FUNCTION_FOLLOWER && f != PROCESSOR_FUNCTION_COMPRESSOR;
    double env = 0.0;
    for (size_t i = 0; i < n; ++i) {
      double in;
      if (!stereo) {
        in = xl[i];
      } else if (f == PROCESSOR_FUNCTION_FOLLOWER ||
                 f == PROCESSOR_FUNCTION_FILTER_CONTROLLER) {
        in = 0.5 * (xl[i] + xr[i]);
      } else {
        in = std::fabs(static_cast<double>(xl[i])) >= std::fabs(static_cast<double>(xr[i]))
            ? xl[i] : xr[i];
      }
      double e = in * sense;
      if (e > 32767.0) e = 32767.0;
      if (e < -32768.0) e = -32768.0;
      if (envelope) {
        double a = std::fabs(e);
        double k = a > env ? 1.0 / (1 << kDetectorAttackShift)
                           : 1.0 / (1 << kDetectorReleaseShift);
        env += (a - env) * k;
        e = env;
      }
      d[i] = e;
    }
    // 2. Resample to 31089 Hz.
    const double ratio = 44100.0 / 31089.0;
    const size_t m = static_cast<size_t>((n - 4) / ratio);
    std::vector<int16_t> excite(m);
    for (size_t j = 0; j < m; ++j) {
      double p = j * ratio;
      size_t i = static_cast<size_t>(p);
      double t = p - i;
      double y0 = d[i > 0 ? i - 1 : 0], y1 = d[i], y2 = d[i + 1], y3 = d[i + 2];
      double c0 = y1, c1 = 0.5 * (y2 - y0);
      double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
      double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
      double y = ((c3 * t + c2) * t + c1) * t + c0;
      y = std::floor(y + 0.5);
      if (y > 32767.0) y = 32767.0;
      if (y < -32768.0) y = -32768.0;
      excite[j] = static_cast<int16_t>(y);
    }
    // 3. Upstream processor at 31089 Hz.
    memset(static_cast<void*>(&processor_), 0, sizeof(processor_));
    processor_.Init(0);
    processor_.set_function(static_cast<streams::ProcessorFunction>(f));
    processor_.set_alternate(c.alternate != 0);
    processor_.set_linked(c.split != 0);
    processor_.set_global(0, RefKnobToPot(c.attack));
    processor_.set_global(1, RefKnobToPot(c.shape));
    processor_.set_global(2, RefKnobToPot(c.decay));
    processor_.set_global(3, RefKnobToPot(c.mod));
    processor_.set_parameter(0, RefKnobToPot(c.shape));
    processor_.set_parameter(1, RefKnobToPot(c.mod));
    processor_.Configure();
    std::vector<double> g31(m), f31(m);
    for (size_t j = 0; j < m; ++j) {
      uint16_t gain = 0, frequency = 65535;
      processor_.Process(excite[j], excite[j], &gain, &frequency);
      g31[j] = gain;
      f31[j] = frequency;
    }
    // 4. Back to 44.1 kHz (linear: the DAC/PWM smoothing).
    const size_t frames = static_cast<size_t>((m - 2) * ratio);
    std::vector<double> gcode(frames), fcode(frames);
    for (size_t i = 0; i < frames; ++i) {
      double p = i / ratio;
      size_t j = static_cast<size_t>(p);
      double t = p - j;
      gcode[i] = g31[j] + (g31[j + 1] - g31[j]) * t;
      fcode[i] = f31[j] + (f31[j + 1] - f31[j]) * t;
    }
    Analog(c, xl, xr, gcode, fcode, out);
  }

  // 5. VCA, SVF, clip, mix, from gain and cutoff codes at 44.1 kHz. Also
  // used on the port's own codes, to measure its fixed-point audio path.
  static void Analog(const op1streams::Controls& c,
                     const std::vector<int16_t>& xl, const std::vector<int16_t>& xr,
                     const std::vector<double>& gcode, const std::vector<double>& fcode,
                     RefOutput* out) {
    using namespace op1streams;
    const int f = c.function;
    const size_t frames = gcode.size();
    const bool bypass = f == PROCESSOR_FUNCTION_COMPRESSOR ||
        (f == PROCESSOR_FUNCTION_FOLLOWER && c.alternate);
    const bool unity = f == PROCESSOR_FUNCTION_FILTER_CONTROLLER;
    const double response = f == PROCESSOR_FUNCTION_COMPRESSOR ? 0.0 : c.response / 32767.0;
    const double trim = std::pow(2.0, (c.output - 16384) / 8192.0);
    const double mix = ((c.mix < 0 ? 0 : c.mix) + 1) / 2 / 16384.0;
    const int mode = c.filter_mode;
    const bool resonant = mode == FILTER_LP_RESONANT || mode == FILTER_BP_RESONANT;
    const bool band_pass = mode == FILTER_BP || mode == FILTER_BP_RESONANT;
    const double k = resonant ? 0.5 : std::sqrt(2.0);
    out->l.assign(frames, 0.0f);
    out->r.assign(frames, 0.0f);
    out->gain_db.assign(frames, 0.0f);
    out->cutoff_oct.assign(frames, -1.0f);
    double ic1[2] = { 0, 0 }, ic2[2] = { 0, 0 };
    for (size_t i = 0; i < frames; ++i) {
      double code = unity ? 32767.0 : gcode[i];
      double g = RefVcaGain(code, response) * trim;
      if (g > 8.0) g = 8.0;
      out->gain_db[i] = static_cast<float>(g > 1e-6 ? 20.0 * std::log10(g) : -120.0);
      double a1 = 0, a2 = 0, a3 = 0;
      if (!bypass) {
        double fc = kCutoffMinHz * std::pow(2.0, kCutoffOctaves * fcode[i] / 65536.0);
        if (fc > kCutoffMaxRatio * kSampleRate) fc = kCutoffMaxRatio * kSampleRate;
        out->cutoff_oct[i] = static_cast<float>(std::log2(fc / kCutoffMinHz));
        double gg = std::tan(M_PI * fc / kSampleRate);
        a1 = 1.0 / (1.0 + gg * (gg + k));
        a2 = gg * a1;
        a3 = gg * a2;
      }
      for (int ch = 0; ch < 2; ++ch) {
        double dry = ch == 0 ? xl[i] : xr[i];
        double x = dry * g;
        double y;
        if (bypass) {
          y = x;
        } else {
          double v3 = x - ic2[ch];
          double v1 = a1 * ic1[ch] + a2 * v3;
          double v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
          ic1[ch] = 2 * v1 - ic1[ch];
          ic2[ch] = 2 * v2 - ic2[ch];
          y = band_pass ? k * v1 : v2;
        }
        if (y > 32767.0) y = 32767.0;
        if (y < -32768.0) y = -32768.0;
        double o = dry + (y - dry) * mix;
        (ch == 0 ? out->l : out->r)[i] = static_cast<float>(o / 32768.0);
      }
    }
  }

 private:
  streams::Processor processor_;
};

}  // namespace harness

#endif  // OP1STREAMS_HARNESS_REF_MODEL_H_
