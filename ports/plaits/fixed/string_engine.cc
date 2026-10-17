#include "fixed/string_engine.h"

namespace op1plaits {

namespace {

const uint32_t kStringMask = kStringDelayLineSize - 1;
const uint32_t kStretchMask = kStringDelayLineSize / 4 - 1;

// 1 / f, f in Q32: (1 / x) 2^n with f << n = x 2^32. Returns Q16 (clamped
// to `max_q16`).
inline int32_t ReciprocalOfFrequencyQ16(uint32_t f, int32_t max_q16) {
  if (f < 70000u) return max_q16;   // 1 / f > 61000 samples
  const int n = CountLeadingZeros(f);
  const int32_t r = ReciprocalNormalized(static_cast<int32_t>((f << n) >> 1));   // Q30 of 1 / x
  // 1 / f = (1 / x) 2^n: Q16 is r 2^(n - 14).
  const int32_t q = n >= 14 ? ShlSat(r, n - 14) : r >> (14 - n);
  return q > max_q16 ? max_q16 : q;
}

inline int32_t Q15ToQ31(int32_t x) {
  return x >= kQ15One ? INT32_MAX : (x <= 0 ? 0 : x << 16);
}

}  // namespace

void StringFixed::Init(Allocator* allocator) {
  string_ = allocator->Allocate<int32_t>(kStringDelayLineSize);
  stretch_ = allocator->Allocate<int32_t>(kStringDelayLineSize / 4);
  delay_ = 100 << 16;
  Reset();
}

void StringFixed::Reset() {
  for (int i = 0; i < kStringDelayLineSize; ++i) string_[i] = 0;
  for (int i = 0; i < kStringDelayLineSize / 4; ++i) stretch_[i] = 0;
  string_write_ptr_ = 0;
  stretch_write_ptr_ = 0;
  iir_damping_filter_.Init();
  dc_x_ = dc_y_ = 0;
  dispersion_noise_ = 0;
  curved_bridge_ = 0;
  out_sample_[0] = out_sample_[1] = 0;
  src_phase_ = 0;
}

void StringFixed::Process(uint32_t f0, int32_t non_linearity, int32_t brightness, int32_t damping,
                          const int32_t* in, int32_t* out, size_t size) {
  if (non_linearity <= 0) {
    ProcessInternal<false>(f0, -non_linearity, brightness, damping, in, out, size);
  } else {
    ProcessInternal<true>(f0, non_linearity, brightness, damping, in, out, size);
  }
}

template<bool dispersion>
void StringFixed::ProcessInternal(uint32_t f0, int32_t nla, int32_t brightness, int32_t damping,
                                  const int32_t* in, int32_t* out, size_t size) {
  // delay = 1 / f0, clamped to [4, kDelayLineSize - 4] (Q16).
  int32_t delay = ReciprocalOfFrequencyQ16(f0, (kStringDelayLineSize - 4) << 16);
  if (delay < (4 << 16)) delay = 4 << 16;
  // src_ratio = delay * f0 (Q30; the phase reaches 2); below 0.9999, a
  // crude linear upsampler plays notes too low for the delay line.
  uint32_t src_ratio = static_cast<uint32_t>(
      (static_cast<uint64_t>(static_cast<uint32_t>(delay)) * f0) >> 18);
  uint32_t src_phase = static_cast<uint32_t>(src_phase_);
  if (src_ratio >= Q32(0.9999 / 4.0)) {
    src_phase = 1u << 30;
    src_ratio = 1u << 30;
  }

  // damping_cutoff = min(12 + 60 damping^2 + 24 brightness, 84), Q16.
  const int32_t d2 = MulQ31(damping, damping);
  int32_t damping_cutoff = (12 << 16) + Mul64(d2, 60 << 16, 31) + Mul64(brightness, 24 << 16, 31);
  if (damping_cutoff > (84 << 16)) damping_cutoff = 84 << 16;
  static constexpr FFloat kMaxF(0.499);
  FFloat damping_f_ff = FFloatFromQ32(f0) * SemitonesToRatio(damping_cutoff);
  if (kMaxF < damping_f_ff) damping_f_ff = kMaxF;
  uint32_t damping_f = static_cast<uint32_t>(damping_f_ff.ToQ(31)) << 1;

  // Crossfade to infinite decay.
  if (damping >= Q31(0.95)) {
    const int32_t to_infinite = static_cast<int32_t>(
        (static_cast<int64_t>(damping - Q31(0.95)) * 20) > INT32_MAX
            ? INT32_MAX : (static_cast<int64_t>(damping - Q31(0.95)) * 20));
    brightness += MulQ31(to_infinite, INT32_MAX - brightness);
    damping_f += static_cast<uint32_t>(MulQ31(to_infinite, static_cast<int32_t>((Q32(0.4999) - damping_f) >> 1)) << 1);
    damping_cutoff += Mul64(to_infinite, (128 << 16) - damping_cutoff, 31);
  }
  iir_damping_filter_.set_f_q<FREQUENCY_FAST>(damping_f, 1 << 15);

  // damping_compensation = Interpolate(lut_svf_shift, damping_cutoff, 1.0).
  const int32_t ci = damping_cutoff >> 16;
  const int32_t cf = (damping_cutoff & 0xffff) << 15;
  const int32_t damping_compensation =
      lut_svf_shift_q31[ci] + MulQ31(lut_svf_shift_q31[ci + 1] - lut_svf_shift_q31[ci], cf);

  // Linearly interpolate delay time.
  Interpolator delay_modulation(&delay_, MulQ31(delay, damping_compensation), size);

  // stretch_point = nla (2 - nla) 0.225; stretch_correction = clamp(160 / SR
  // delay, 1, 2.1) (Q29).
  const int32_t stretch_point = Mul(MulQ31(nla, INT32_MAX - nla) + (nla >> 0), Q31(0.225 / 2.0)) << 1;
  int32_t stretch_correction = MulQ31(delay, Q31(160.0 / kSampleRateD));   // Q16
  stretch_correction = Clip(stretch_correction, 1 << 16, Q(2.1, 16)) << 13;
  // noise_amount = (4 (nla - 0.75))^2 0.1 for nla > 0.75.
  int32_t noise_amount = 0;
  if (nla > Q31(0.75)) {
    const int32_t s = (nla - Q31(0.75)) << 2 < 0 ? INT32_MAX : (nla - Q31(0.75)) << 2;
    noise_amount = Mul(MulQ31(s, s), Q31(0.1));
  }
  const int32_t noise_filter = Q31(0.06) + Mul(MulQ31(brightness, brightness), Q31(0.94));
  const int32_t bridge_curving = Mul(MulQ31(nla, nla), Q31(0.01));
  // ap_gain = -0.618 nla / (0.15 + |nla|).
  const uint32_t nla_u = static_cast<uint32_t>(nla);
  const int32_t ap_gain = -Mul(Fraction(nla_u >> 1, (nla_u >> 1) + (Q31(0.15) >> 1)), Q31(0.618));
  // 0.408 - 0.308 stretch_point (Q31).
  const int32_t ap_k = Q31(0.408) - Mul(stretch_point, Q31(0.308));
  const int32_t dc_pole = Q31(1.0 - 20.0 / kSampleRateD);

  int32_t* line = string_;
  int32_t* stretch = stretch_;
  uint32_t wp = string_write_ptr_;
  uint32_t swp = stretch_write_ptr_;

  while (size--) {
    src_phase += src_ratio;
    if (src_phase > (1u << 30)) {
      src_phase -= 1u << 30;

      int32_t d = delay_modulation.Next();   // Q16
      int32_t s;
      if (dispersion) {
        const int32_t noise = static_cast<int32_t>(Random::GetWord() - 0x80000000u) >> 1;   // Q31
        dispersion_noise_ += MulRound(noise - dispersion_noise_, noise_filter);
        d += Mul(d, Mul(dispersion_noise_, noise_amount));
      } else {
        d -= Mul(d, Mul(curved_bridge_, bridge_curving) << 7);
      }

      bool hermite = true;
      if (dispersion) {
        const int32_t ap_delay = Mul(d, stretch_point);                   // Q16
        const int32_t main_delay = d - (Mul(Mul(ap_delay, ap_k), stretch_correction) << 2);
        if (ap_delay >= (4 << 16) && main_delay >= (4 << 16)) {
          // string_.Read(main_delay): linear interpolation.
          const uint32_t di = static_cast<uint32_t>(main_delay) >> 16;
          const int32_t df = (main_delay & 0xffff) << 15;
          const int32_t a = line[(wp + di) & kStringMask];
          const int32_t b = line[(wp + di + 1) & kStringMask];
          s = a + Mul(b - a, df);
          // stretch_.Allpass(s, size_t(ap_delay), ap_gain).
          const int32_t read = stretch[(swp + (static_cast<uint32_t>(ap_delay) >> 16)) & kStretchMask];
          const int32_t write = s + Mul(ap_gain, read);
          stretch[swp] = write;
          swp = (swp - 1) & kStretchMask;
          s = read - Mul(write, ap_gain);
          hermite = false;
        }
      }
      if (hermite) {
        // string_.ReadHermite(d), in Q23.
        const uint32_t di = static_cast<uint32_t>(d) >> 16;
        const int32_t f = (d & 0xffff) << 15;
        const uint32_t t = wp + di + kStringDelayLineSize;
        const int32_t xm1 = line[(t - 1) & kStringMask] >> 1;
        const int32_t x0 = line[t & kStringMask] >> 1;
        const int32_t x1 = line[(t + 1) & kStringMask] >> 1;
        const int32_t x2 = line[(t + 2) & kStringMask] >> 1;
        const int32_t c = (x1 - xm1) >> 1;
        const int32_t v = x0 - x1;
        const int32_t w = c + v;
        const int32_t a = w + v + ((x2 - x0) >> 1);
        const int32_t b_neg = w + a;
        s = (Mul(Mul(Mul(a, f) - b_neg, f) + c, f) + x0) << 1;
      }

      if (!dispersion) {
        // value = |s| - 0.025; curved_bridge = (|value| + value) (s > 0 ? 1 : -1.5).
        const int32_t value = Abs(s) - Q(0.025, 24);
        const int32_t p = Abs(value) + value;
        curved_bridge_ = s > 0 ? p : -(p + (p >> 1));
      }

      s += *in;
      s = Clip(s, -(20 << 24), 20 << 24);
      // dc_blocker_.Process(&s, 1).
      const int32_t old_x = dc_x_;
      dc_x_ = s;
      dc_y_ = MulRound(dc_y_, dc_pole) + s - old_x;
      s = iir_damping_filter_.Process<FILTER_MODE_LOW_PASS>(dc_y_);
      line[wp] = s;
      wp = (wp - 1) & kStringMask;
      out_sample_[1] = out_sample_[0];
      out_sample_[0] = s;
    }
    const int32_t o1 = out_sample_[1];
    *out++ += src_phase >= (1u << 30) ? out_sample_[0]
        : o1 + Mul(out_sample_[0] - o1, static_cast<int32_t>(src_phase << 1));
    ++in;
  }
  string_write_ptr_ = wp;
  stretch_write_ptr_ = swp;
  src_phase_ = static_cast<int32_t>(src_phase);
}

void StringVoiceFixed::Init(Allocator* allocator) {
  excitation_filter_.Init();
  string_.Init(allocator);
  remaining_noise_samples_ = 0;
}

void StringVoiceFixed::Reset() { string_.Reset(); }

void StringVoiceFixed::Render(bool sustain, bool trigger, int32_t accent, uint32_t f0,
                              int32_t structure, int32_t brightness, int32_t damping,
                              int32_t* temp, int32_t* out, int32_t* aux, size_t size) {
  // Parameters Q31.
  const int32_t density = MulQ31(brightness, brightness);
  brightness += MulQ31(Mul(accent, Q31(0.25)), INT32_MAX - brightness);
  damping += MulQ31(Mul(accent, Q31(0.25)), INT32_MAX - damping);

  // Synthesize excitation signal.
  if (trigger || sustain) {
    // cutoff = min(4 f0 SemitonesToRatio((b (2 - b) - 0.5) 72), 0.499).
    const int32_t b2 = MulQ31(brightness, INT32_MAX - brightness) + brightness;   // b (2 - b) = b (1 - b) + b
    const int32_t semis = Mul64(b2 - Q31(0.5), 72 << 16, 31);   // Q16
    static constexpr FFloat kMaxF(0.499);
    FFloat cutoff = FFloatFromQ32(f0).Scaled(2) * SemitonesToRatio(semis);
    if (kMaxF < cutoff) cutoff = kMaxF;
    const uint32_t cutoff_q32 = static_cast<uint32_t>(cutoff.ToQ(31)) << 1;
    // remaining_noise_samples_ = size_t(1 / f0).
    remaining_noise_samples_ = static_cast<uint32_t>(ReciprocalOfFrequencyQ16(f0, INT32_MAX) >> 16);
    excitation_filter_.set_f_q<FREQUENCY_DIRTY>(cutoff_q32, sustain ? (1 << 16) : (1 << 15));
  }
  if (sustain) {
    // dust_f = 0.00005 + 0.99995 density^2; Dust(f) (8 - 6 f) accent.
    const int32_t dust_f = Q31(0.00005) + Mul(MulQ31(density, density), Q31(0.99995));
    const int32_t k = (8 << 27) - 6 * (dust_f >> 4);   // 8 - 6 dust_f, Q27
    for (size_t i = 0; i < size; ++i) {
      const uint32_t u = Random::GetWord() >> 1;   // Q31
      const int32_t dust = u < static_cast<uint32_t>(dust_f) ? Fraction(u, static_cast<uint32_t>(dust_f)) : 0;
      temp[i] = Mul(Mul(dust, k), accent) >> 3;   // Q27 -> Q24
    }
  } else if (remaining_noise_samples_) {
    size_t noise_samples = remaining_noise_samples_ < size ? remaining_noise_samples_ : size;
    remaining_noise_samples_ -= noise_samples;
    size_t i = 0;
    for (; i < noise_samples; ++i) {
      temp[i] = static_cast<int32_t>(Random::GetWord() ^ 0x80000000u) >> 7;   // 2 u - 1, Q24
    }
    for (; i < size; ++i) temp[i] = 0;
  } else {
    for (size_t i = 0; i < size; ++i) temp[i] = 0;
  }

  excitation_filter_.Process<FILTER_MODE_LOW_PASS>(temp, temp, size);
  for (size_t i = 0; i < size; ++i) aux[i] += temp[i];

  // non_linearity = structure < 0.24 ? (structure - 0.24) 4.166 :
  // (structure > 0.26 ? (structure - 0.26) 1.35135 : 0), Q31.
  int32_t non_linearity = 0;
  if (structure < Q31(0.24)) {
    non_linearity = -static_cast<int32_t>(Mul64(Q31(0.24) - structure, Q(4.166, 28), 28));
  } else if (structure > Q31(0.26)) {
    non_linearity = Mul(structure - Q31(0.26), Q31(1.35135 / 2.0)) << 1;
  }
  if (non_linearity < -INT32_MAX) non_linearity = -INT32_MAX;
  string_.Process(f0, non_linearity, brightness, damping, temp, out, size);
}

void StringEngineFixed::Init(Allocator* allocator) {
  temp_buffer_ = allocator->Allocate<int32_t>(kMaxBlockSize);
  for (int i = 0; i < kNumStrings; ++i) {
    voice_[i].Init(allocator);
    f0_[i] = Q32(0.01);
  }
  active_string_ = kNumStrings - 1;
  for (int i = 0; i < 16; ++i) f0_delay_[i] = 0;
  f0_delay_write_ptr_ = 0;
}

void StringEngineFixed::Reset() {
  for (int i = 0; i < 16; ++i) f0_delay_[i] = 0;
  f0_delay_write_ptr_ = 0;
  for (int i = 0; i < kNumStrings; ++i) voice_[i].Reset();
}

void StringEngineFixed::Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
                               size_t size, bool* already_enveloped) {
  (void)already_enveloped;
  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    // f0_delay_.Read(14): the f0 of 14 blocks ago.
    f0_[active_string_] = f0_delay_[(f0_delay_write_ptr_ + 14) & 15];
    active_string_ = active_string_ == kNumStrings - 1 ? 0 : active_string_ + 1;
  }
  const uint32_t f0 = NoteToFrequency(parameters.note);
  f0_[active_string_] = f0;
  f0_delay_[f0_delay_write_ptr_] = f0;
  f0_delay_write_ptr_ = (f0_delay_write_ptr_ - 1) & 15;

  for (size_t i = 0; i < size; ++i) out[i] = aux[i] = 0;   // Q24
  const int32_t accent = Q15ToQ31(parameters.accent);
  const int32_t structure = Q15ToQ31(parameters.harmonics);
  const int32_t brightness = Q15ToQ31(MulQ15(parameters.timbre, parameters.timbre));
  const int32_t damping = Q15ToQ31(parameters.morph);
  for (int i = 0; i < kNumStrings; ++i) {
    voice_[i].Render(
        (parameters.trigger & TRIGGER_UNPATCHED) && i == active_string_,
        (parameters.trigger & TRIGGER_RISING_EDGE) && i == active_string_,
        accent, f0_[i], structure, brightness, damping, temp_buffer_, out, aux, size);
  }
  for (size_t i = 0; i < size; ++i) {
    out[i] = ShlSat(out[i], kAudioBits - 24);
    aux[i] = ShlSat(aux[i], kAudioBits - 24);
  }
}

}  // namespace op1plaits
