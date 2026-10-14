// One Rings voice for the OP-1, fixed point. See op1_voice.h.

#include "src/op1_voice.h"

#include "src/ffloat.h"
#include "src/fixed.h"
#include "src/fixed_tables.h"

namespace op1rings {

namespace {

const int32_t kOne = INT32_MAX;

// 2^-l for l >= 0 in Q24; result in Q31 (as in modal_resonator.cc).
inline int32_t Exp2NegQ31(int32_t l) {
  int32_t n = l >> 24;
  if (n >= 31) return 0;
  int32_t frac = l & 0xffffff;
  int32_t i = frac >> 16;
  int32_t f = (frac & 0xffff) << 15;
  int32_t m = kExp2NegQ31[i] + MulQ31Fast(kExp2NegQ31[i + 1] - kExp2NegQ31[i], f);
  return m >> n;
}

// 2^l for l in Q24 (any sign), as an FFloat.
inline FFloat Exp2(int32_t l) {
  int32_t n = l >> 24;                       // floor
  int32_t frac = l & 0xffffff;               // [0, 1)
  // 2^frac = 2 * 2^-(1 - frac); 2^-(1 - frac) in (0.5, 1].
  int32_t m = Exp2NegQ31((1 << 24) - frac);
  return FFloat::FromParts(m, n + 1 - 31);
}

// SoftLimit(x) for x in Q27, by table over [0, 4] (clamped beyond).
inline int32_t SoftLimitQ27(int32_t x) {
  int32_t a = x < 0 ? -x : x;
  int32_t y;
  if (a >= (4 << 27)) {
    y = kSoftLimitQ30[256];
  } else {
    int32_t t = a << 2;                      // a / 4 in Q31
    int32_t i = t >> 23;
    int32_t frac = (t & 0x7fffff) << 8;
    y = kSoftLimitQ30[i] + MulQ31Fast(kSoftLimitQ30[i + 1] - kSoftLimitQ30[i], frac);
  }
  y >>= 3;                                   // Q30 -> Q27
  return x < 0 ? -y : y;
}

inline int32_t Abs(int32_t x) { return x < 0 ? -x : x; }

// Limiter gains per model (upstream Part::model_gains_: modal 1.4, FM 0.7).
constexpr LimiterGains kModalGains = { 375809638, 1202590843, FFloat(1.12) };
constexpr LimiterGains kFmGains = { 187904819, 601295421, FFloat(0.56) };

}  // namespace

// --- ExcitationFilter ---------------------------------------------------

void ExcitationFilter::Init() {
  g_ = k_ = 0;
  h_ = kOne;
  s1_ = s2_ = 0;
  pending_ = false;
  pending_output_ = pending_s1_ = pending_s2_ = 0;
}

void ExcitationFilter::SetCutoff(int32_t f) {
  // stmlib OnePole::tan<FREQUENCY_DIRTY>: f (pi + a f^2), a = 0.3736 pi^3.
  static constexpr FFloat kPi = FFloat(3.14159265358979);
  static constexpr FFloat kA = FFloat(0.3736 * 3.14159265358979 * 3.14159265358979 * 3.14159265358979);
  static constexpr FFloat kR = FFloat(1.0 / 1.5);
  static constexpr FFloat kOneF = FFloat(1.0);
  FFloat ff = FFloat::FromQ(f, 31);
  FFloat g = ff * (kPi + kA * ff * ff);
  FFloat h = FFloat::Reciprocal(kOneF + kR * g + g * g);
  g_ = g.ToQ(29);
  k_ = (kR + g).ToQ(29);
  h_ = h.ToQ31();
}

void ExcitationFilter::AddImpulse(int32_t first_output, int32_t s1, int32_t s2) {
  // The filter is linear: an impulse x0 at the next input adds g^2 h x0 to
  // that output and (2 g h x0, 2 g^2 h x0) to the state after it. Upstream
  // feeds the raw impulse (up to ~6000) through the filter; these terms stay
  // below ~1.
  pending_output_ = first_output;
  pending_s1_ = s1;
  pending_s2_ = s2;
  pending_ = true;
}

void ExcitationFilter::Process(int32_t* out, size_t size) {
  int32_t s1 = s1_, s2 = s2_;
  // The exciter only rings for a few ms after a strum: once its state has
  // decayed below ~2e-6, it outputs silence until the next impulse.
  const int32_t kQuiet = 1 << 8;
  if (!pending_ && (s1 < 0 ? -s1 : s1) < kQuiet && (s2 < 0 ? -s2 : s2) < kQuiet) {
    for (size_t i = 0; i < size; ++i) out[i] = 0;
    s1_ = s2_ = 0;
    return;
  }
  for (size_t i = 0; i < size; ++i) {
    // hp = (0 - k s1 - s2) h; bp = g hp + s1; s1 = g hp + bp;
    // lp = g bp + s2; s2 = g bp + lp. The states stay within about +-2 and
    // g, k within 4 (Q29), so the Q25 -> Q27 shifts can't overflow.
    int32_t hp = MulQ31Fast(-(MulQ31Fast(s1, k_) << 2) - s2, h_);
    int32_t g_hp = MulQ31Fast(hp, g_) << 2;
    int32_t bp = g_hp + s1;
    s1 = g_hp + bp;
    int32_t g_bp = MulQ31Fast(bp, g_) << 2;
    int32_t lp = g_bp + s2;
    s2 = g_bp + lp;
    if (pending_) {
      lp += pending_output_;
      s1 += pending_s1_;
      s2 += pending_s2_;
      pending_ = false;
    }
    out[i] = lp;
  }
  s1_ = s1;
  s2_ = s2;
}

// --- Limiter --------------------------------------------------------------

void Limiter::Init() {
  peak_ = 1 << 26;   // 0.5
  gain_ = kOne;
}

void Limiter::Process(int32_t* l, int32_t* r, size_t size, int32_t pre_gain_q28) {
  const int32_t kAttack = 107374182;     // 0.05, Q31
  const int32_t kRelease = 42950;        // 0.00002, Q31
  const int32_t k0_8 = 1717986918;       // 0.8, Q31
  for (size_t i = 0; i < size; ++i) {
    int32_t l_pre = ShlSat(MulQ31Fast(l[i], pre_gain_q28), 3);   // Q27
    int32_t r_pre = ShlSat(MulQ31Fast(r[i], pre_gain_q28), 3);
    int32_t peak = Abs(l_pre);
    int32_t r_peak = Abs(r_pre);
    int32_t s_peak = Abs(r_pre - l_pre);
    if (r_peak > peak) peak = r_peak;
    if (s_peak > peak) peak = s_peak;
    int32_t error = peak - peak_;
    peak_ += MulQ31Fast(error, error > 0 ? kAttack : kRelease);
    // gain = 1 / peak_ above 1: one Newton step per sample from the last.
    if (peak_ <= (1 << 27)) {
      gain_ = kOne;
    } else {
      int32_t pg = MulQ31Fast(peak_, gain_);                      // Q27, ~1
      int32_t two_minus = (2 << 27) - pg;                         // Q27
      gain_ = ShlSat(MulQ31Fast(gain_, two_minus), 4);            // Q31
    }
    l[i] = SoftLimitQ27(MulQ31Fast(MulQ31Fast(l_pre, gain_), k0_8));
    r[i] = SoftLimitQ27(MulQ31Fast(MulQ31Fast(r_pre, gain_), k0_8));
  }
}

// --- MonoLimiter ----------------------------------------------------------

void MonoLimiter::Init() {
  peak_ = 1 << 26;          // 0.5
  gain_ = 1202590843;       // 1.4 * 0.8 in Q30 (the modal model's)
}

void MonoLimiter::Process(int32_t* x, size_t size, const LimiterGains& gains) {
  const int32_t kAttack = 107374182;     // 0.05, Q31
  const int32_t kRelease = 42950;        // 0.00002, Q31
  int32_t peak = peak_;
  const int32_t gain = gain_;
  for (size_t i = 0; i < size; ++i) {
    // Peak follower on the pre-gain signal.
    int32_t pre = MulQ31Fast(x[i], gains.pre_q28) << 3;   // Q27 (no overflow: |x| < 2)
    int32_t a = pre < 0 ? -pre : pre;
    int32_t error = a - peak;
    peak += MulQ31Fast(error, error > 0 ? kAttack : kRelease);
    // y = SoftLimit(x * pre-gain * 0.8 / max(peak, 1)).
    x[i] = SoftLimitQ27(ShlSat(MulQ31Fast(x[i], gain), 1));
  }
  peak_ = peak;
  // Gain for the next sub-block: pre-gain * 0.8 / max(peak, 1). Once per
  // sub-block, so FFloat's self-seeding reciprocal is affordable.
  if (peak <= (1 << 27)) {
    gain_ = gains.out_q30;
  } else {
    gain_ = (gains.out * FFloat::Reciprocal(FFloat::FromQ(peak, 27))).ToQ(30);
  }
}

// --- RingsVoice -----------------------------------------------------------

void RingsVoice::Init(int32_t resolution) {
  model_ = MODEL_MODAL;
  strum_ = false;
  silent_blocks_ = 0;
  resonator_.Init();
  resonator_.set_resolution(resolution);
  fm_voice_.Init();
  excitation_filter_.Init();
  limiter_.Init();
  mono_limiter_.Init();
}

void RingsVoice::RenderSubblock(uint32_t phase_increment, const VoicePatch& patch,
                                int32_t* out, int32_t* aux) {
  static constexpr FFloat k0_499 = FFloat(0.499);
  static constexpr FFloat k0_25 = FFloat(0.25);
  const int32_t f0 = static_cast<int32_t>(phase_increment >> 1);   // Q31

  // Excitation filter cutoff (upstream Part::Process, internal exciter):
  // cutoff = b (2 - b); fc = f0 * 2^((cutoff - 0.5) * 8), at most 0.499.
  const int32_t b = patch.brightness;
  int32_t cutoff = ShlSat(b - (MulQ31Fast(b, b) >> 1), 1);         // Q31
  int32_t exponent = (cutoff - (1 << 30)) >> 4;                    // Q24
  FFloat fc = FFloat::FromQ(f0, 31) * Exp2(exponent);
  if (fc > k0_499) fc = k0_499;
  excitation_filter_.SetCutoff(fc.ToQ31());

  if (strum_) {
    // Upstream RenderModalVoice: impulse x0 = 0.25 * 2^(2 fc^2) / fc.
    FFloat fc2 = fc * fc;
    FFloat x0 = k0_25 * Exp2(fc2.Scaled(1).ToQ(24)) * FFloat::Reciprocal(fc);
    FFloat g = FFloat::FromQ(excitation_filter_.g(), 29);
    FFloat h = FFloat::FromQ(excitation_filter_.h(), 31);
    FFloat ghx = g * h * x0;
    excitation_filter_.AddImpulse((g * ghx).ToQ27(), ghx.Scaled(1).ToQ27(),
                                  (g * ghx).Scaled(1).ToQ27());
    strum_ = false;
  }
  int32_t* excitation = excitation_;
  excitation_filter_.Process(excitation, kSubblockSize);

  resonator_.set_frequency(f0);
  resonator_.set_structure(patch.structure);
  resonator_.set_brightness(MulQ31Fast(patch.brightness, patch.brightness));
  resonator_.set_damping(patch.damping);
  resonator_.set_position(patch.position);
  resonator_.Process(excitation, out, aux, kSubblockSize);

}

void RingsVoice::Render(const uint32_t* phase_increment, const VoicePatch& patch,
                        int32_t* out, int32_t* aux, size_t size) {
  // A voice that has been silent for a while costs nothing until its next
  // note: the OP-1 may render all 6 voices every block.
  const int32_t kSilence = 1 << 11;          // ~ -96 dBFS in Q27
  const int32_t kSilentBlocksToSleep = 8;
  if (!strum_ && silent_blocks_ >= kSilentBlocksToSleep) {
    for (size_t i = 0; i < size; ++i) out[i] = aux[i] = 0;
    return;
  }
  for (size_t i = 0; i < size; i += kControlBlockSize) {
    for (size_t j = 0; j < kControlBlockSize; j += kSubblockSize) {
      RenderSubblock(phase_increment[i], patch, out + i + j, aux + i + j);
      limiter_.Process(out + i + j, aux + i + j, kSubblockSize, 375809638);  // 1.4, Q28
    }
  }
  int32_t peak = 0;
  for (size_t i = 0; i < size; ++i) {
    int32_t a = out[i] < 0 ? -out[i] : out[i];
    int32_t b = aux[i] < 0 ? -aux[i] : aux[i];
    if (a > peak) peak = a;
    if (b > peak) peak = b;
  }
  silent_blocks_ = peak < kSilence ? silent_blocks_ + 1 : 0;
}

// Upstream Part::RenderFMVoice with the internal exciter: a strum triggers
// the voice's envelope; ratio = structure, brightness = brightness (not
// squared), damping = damping, feedback = position.
void RingsVoice::RenderFmSubblock(uint32_t phase_increment,
                                  const VoicePatch& patch, int32_t* out) {
  if (strum_) {
    fm_voice_.TriggerInternalEnvelope();
    strum_ = false;
  }
  fm_voice_.set_frequency(static_cast<int32_t>(phase_increment >> 1));
  fm_voice_.set_ratio(patch.structure);
  fm_voice_.set_brightness(patch.brightness);
  fm_voice_.set_damping(patch.damping);
  fm_voice_.set_feedback_amount(patch.position);
  fm_voice_.Process(out, kSubblockSize);
}

void RingsVoice::RenderMono(const uint32_t* phase_increment,
                            const VoicePatch& patch, int32_t* out, size_t size) {
  const int32_t kSilence = 1 << 11;          // ~ -96 dBFS in Q27
  const int32_t kSilentBlocksToSleep = 8;
  if (!strum_ && silent_blocks_ >= kSilentBlocksToSleep) {
    for (size_t i = 0; i < size; ++i) out[i] = 0;
    return;
  }
  const bool fm = model_ == MODEL_FM_VOICE;
  int32_t peak = 0;
  for (size_t i = 0; i < size; i += kControlBlockSize) {
    for (size_t j = 0; j < kControlBlockSize; j += kSubblockSize) {
      int32_t* o = out + i + j;
      if (fm) {
        RenderFmSubblock(phase_increment[i], patch, o);
      } else {
        RenderSubblock(phase_increment[i], patch, o, aux_);
        for (size_t k = 0; k < kSubblockSize; ++k) o[k] += aux_[k];
      }
      for (size_t k = 0; k < kSubblockSize; ++k) {
        int32_t a = o[k] < 0 ? -o[k] : o[k];
        if (a > peak) peak = a;
      }
      mono_limiter_.Process(o, kSubblockSize, fm ? kFmGains : kModalGains);
    }
  }
  silent_blocks_ = peak < kSilence ? silent_blocks_ + 1 : 0;
}

}  // namespace op1rings
