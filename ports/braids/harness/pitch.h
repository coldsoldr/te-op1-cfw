// Fundamental-frequency estimate for the pitch checks (host only, double
// precision): YIN's cumulative-mean-normalised difference finds the period
// T0, then the difference function's minimum near the largest multiple of
// T0 that fits refines it (parabolic interpolation there, divided by the
// multiple), which gives a few hundredths of a cent on steady tones.

#ifndef PORTS_BRAIDS_HARNESS_PITCH_H_
#define PORTS_BRAIDS_HARNESS_PITCH_H_

#include <cmath>
#include <cstdint>
#include <vector>

namespace harness {

struct PitchEstimate {
  double frequency;   // Hz, 0 if no periodicity was found
  double aperiodicity;  // YIN's normalised difference at T0 (0 = periodic)
};

inline double Difference(const int16_t* x, int window, int tau) {
  double d = 0.0;
  for (int j = 0; j < window; ++j) {
    double e = static_cast<double>(x[j]) - static_cast<double>(x[j + tau]);
    d += e * e;
  }
  return d;
}

// x: n samples. Looks for fundamentals between fmin and fmax.
inline PitchEstimate EstimatePitch(const int16_t* x, int n, double fs,
                                   double fmin = 40.0, double fmax = 4000.0) {
  PitchEstimate result = { 0.0, 1.0 };
  const int window = 8192;
  const int tau_max = static_cast<int>(fs / fmin);
  const int tau_min = static_cast<int>(fs / fmax);
  if (n < window + tau_max + 2) return result;
  std::vector<double> d(tau_max + 2, 0.0), cmnd(tau_max + 2, 1.0);
  double sum = 0.0;
  for (int tau = 1; tau <= tau_max + 1; ++tau) {
    d[tau] = Difference(x, window, tau);
    sum += d[tau];
    cmnd[tau] = sum > 0.0 ? d[tau] * tau / sum : 1.0;
  }
  int t0 = -1;
  for (int tau = tau_min; tau <= tau_max; ++tau) {
    if (cmnd[tau] < 0.15) {
      while (tau + 1 <= tau_max && cmnd[tau + 1] < cmnd[tau]) ++tau;
      t0 = tau;
      break;
    }
  }
  if (t0 < 0) {
    // No dip below the threshold: the global minimum, flagged by its value.
    t0 = tau_min;
    for (int tau = tau_min; tau <= tau_max; ++tau) {
      if (cmnd[tau] < cmnd[t0]) t0 = tau;
    }
  }
  result.aperiodicity = cmnd[t0];
  // Parabolic interpolation of d around t0.
  double a = d[t0 - 1], b = d[t0], c = d[t0 + 1];
  double den = a - 2.0 * b + c;
  double period = t0 + (den > 0.0 ? 0.5 * (a - c) / den : 0.0);
  // Refine on the largest multiple of the period that fits.
  const int max_lag = n - window - 4;
  int m = static_cast<int>(max_lag / period);
  if (m > 64) m = 64;
  if (m >= 2) {
    int centre = static_cast<int>(std::floor(m * period + 0.5));
    int best = centre;
    double best_d = Difference(x, window, centre);
    for (int tau = centre - 3; tau <= centre + 3; ++tau) {
      if (tau < 1 || tau + 1 > max_lag) continue;
      double v = Difference(x, window, tau);
      if (v < best_d) { best_d = v; best = tau; }
    }
    if (best - 1 >= 1 && best + 1 <= max_lag) {
      double a2 = Difference(x, window, best - 1);
      double b2 = best_d;
      double c2 = Difference(x, window, best + 1);
      double den2 = a2 - 2.0 * b2 + c2;
      double lag = best + (den2 > 0.0 ? 0.5 * (a2 - c2) / den2 : 0.0);
      period = lag / m;
    }
  }
  result.frequency = fs / period;
  return result;
}

inline double Cents(double f, double reference) {
  return 1200.0 * std::log2(f / reference);
}

inline double MidiFrequency(int note) {
  return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

}  // namespace harness

#endif  // PORTS_BRAIDS_HARNESS_PITCH_H_
