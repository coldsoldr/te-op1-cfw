// The fixed-point state-variable filter and one-pole (fixed/svf.h) against
// stmlib's float Svf / OnePole: white noise and an impulse train through
// each mode, over frequencies, resonances and approximations. Prints the
// SNR per case (the float filters' output is the reference).

#include <cmath>
#include <cstdio>

#include "stmlib/dsp/filter.h"
#include "stmlib/utils/random.h"
#include "fixed/svf.h"

namespace {

struct Stats {
  double sig = 0.0, err = 0.0;
  void Add(double ref, double test) {
    sig += ref * ref;
    err += (test - ref) * (test - ref);
  }
  double snr() const { return 10.0 * std::log10(sig / (err + 1e-30)); }
};

// Signals in Q(bits).
template<stmlib::FrequencyApproximation fa, op1plaits::FrequencyApproximation fb>
void TestSvf(const char* name, double f, double q, int bits) {
  stmlib::Svf ref;
  op1plaits::SvfFixed fixed;
  ref.Init();
  fixed.Init();
  const uint32_t f_q32 = static_cast<uint32_t>(f * 4294967296.0);
  const int32_t q_q16 = static_cast<int32_t>(q * 65536.0);
  ref.set_f_q<fa>(static_cast<float>(f_q32 / 4294967296.0), static_cast<float>(q_q16 / 65536.0));
  fixed.set_f_q<fb>(f_q32, q_q16);
  Stats lp, bp, hp, flp, fbp;
  // The same filter in double, with stmlib's (float) coefficients.
  const double g = ref.g(), r = ref.r(), hh = ref.h();
  double s1 = 0.0, s2 = 0.0;
  uint32_t rng = 1;
  const double scale = std::ldexp(1.0, bits);
  for (int i = 0; i < 48000; ++i) {
    rng = rng * 1664525u + 1013904223u;
    // Half noise, half sparse impulses.
    double x = i < 24000 ? (static_cast<int32_t>(rng) / 2147483648.0) * 0.5
                         : ((i % 2400) == 0 ? 1.0 : 0.0);
    const int32_t xq = static_cast<int32_t>(x * scale);
    x = xq / scale;
    float l, b, h;
    float in = static_cast<float>(x);
    ref.Process<stmlib::FILTER_MODE_LOW_PASS, stmlib::FILTER_MODE_BAND_PASS>(in, &l, &b);
    // stmlib::Svf has no 3-output Process: recompute hp from the inputs.
    int32_t ql, qb, qh;
    fixed.Tick(xq, &ql, &qb, &qh);
    const double dh = (x - r * s1 - g * s1 - s2) * hh;
    const double db = g * dh + s1;
    s1 = g * dh + db;
    const double dl = g * db + s2;
    s2 = g * db + dl;
    lp.Add(dl, ql / scale);
    bp.Add(db, qb / scale);
    flp.Add(dl, l);
    fbp.Add(db, b);
  }
  printf("svf %-22s f %.5f q %7.1f  vs double: fixed LP %5.1f BP %5.1f | float LP %5.1f BP %5.1f dB\n",
         name, f, q, lp.snr(), bp.snr(), flp.snr(), fbp.snr());
}

template<stmlib::FrequencyApproximation fa, op1plaits::FrequencyApproximation fb>
void TestOnePole(const char* name, double f) {
  stmlib::OnePole ref;
  op1plaits::OnePoleFixed fixed;
  ref.Init();
  fixed.Init();
  const uint32_t f_q32 = static_cast<uint32_t>(f * 4294967296.0);
  ref.set_f<fa>(static_cast<float>(f_q32 / 4294967296.0));
  fixed.set_f<fb>(f_q32);
  Stats lp, hp;
  uint32_t rng = 7;
  for (int i = 0; i < 48000; ++i) {
    rng = rng * 1664525u + 1013904223u;
    const int32_t xq = static_cast<int32_t>(rng) >> 4;   // Q27
    const float x = static_cast<float>(xq / 134217728.0);
    float l = ref.Process<stmlib::FILTER_MODE_LOW_PASS>(x);
    int32_t ql = fixed.Process<op1plaits::FILTER_MODE_LOW_PASS>(xq);
    lp.Add(l, ql / 134217728.0);
  }
  printf("onepole %-26s f %.5f  LP %6.1f dB\n", name, f, lp.snr());
}

}  // namespace

int main() {
  const double fs[] = { 0.0005, 0.003, 0.02, 0.1, 0.3 };
  const double qs[] = { 0.5, 1.5, 10.0, 100.0, 1024.0 };
  for (double f : fs) {
    for (double q : qs) {
      TestSvf<stmlib::FREQUENCY_DIRTY, op1plaits::FREQUENCY_DIRTY>("dirty, Q25", f, q, 25);
      TestSvf<stmlib::FREQUENCY_ACCURATE, op1plaits::FREQUENCY_ACCURATE>("accurate, Q25", f, q, 25);
    }
    TestSvf<stmlib::FREQUENCY_FAST, op1plaits::FREQUENCY_FAST>("fast, Q20", f, 1.5, 20);
  }
  for (double f : fs) {
    TestOnePole<stmlib::FREQUENCY_DIRTY, op1plaits::FREQUENCY_DIRTY>("dirty", f);
    TestOnePole<stmlib::FREQUENCY_FAST, op1plaits::FREQUENCY_FAST>("fast", f);
  }
  return 0;
}
