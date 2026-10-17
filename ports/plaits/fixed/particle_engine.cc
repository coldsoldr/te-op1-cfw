#include "fixed/particle_engine.h"

namespace op1plaits {

void ParticleFixed::Render(bool sync, uint32_t density, uint32_t frequency, int32_t spread,
                           int32_t q, int32_t* out, int32_t* aux, size_t size) {
  uint32_t u = Random::GetWord();
  if (sync) u = density;
  bool can_randomize_frequency = true;
  while (size--) {
    int32_t s = 0;   // u * gain = u / density, Q31
    if (u <= density) {
      s = u < density ? Fraction(u, density) : INT32_MAX;
      if (can_randomize_frequency) {
        const int32_t r = static_cast<int32_t>(Random::GetWord() ^ 0x80000000u);   // 2 u - 1, Q31
        const FFloat ratio = SemitonesToRatio(Mul(spread, r));
        static constexpr FFloat kQuarter(0.25);
        FFloat f = ratio * FFloatFromQ32(frequency);
        if (kQuarter < f) f = kQuarter;
        const uint32_t f_q32 = static_cast<uint32_t>(f.ToQ(31)) << 1;
        // pre_gain = 0.5 / sqrt(q f sqrt(density)), Q20 (saturates at 2048).
        const FFloat x = FFloat::FromQ(q, 16) * f * FFloat::Sqrt(FFloatFromQ32(density));
        const FFloat pre_gain = FFloat::ReciprocalSqrt(x).Scaled(-1);
        pre_gain_ = pre_gain.mantissa();
        pre_gain_shift_ = pre_gain.exponent() + 25;   // s (Q31) x m (Q31) -> Q25
        filter_.set_f_q<FREQUENCY_DIRTY>(f_q32, q);
        filter_.InjectionGains(&gh_, &g2h_);
        // Keep the cutoff constant for this whole block.
        can_randomize_frequency = false;
      }
    }
    *aux++ += s >> 6;
    int32_t lp, bp, hp;
    filter_.Tick(0, &lp, &bp, &hp);
    if (s) {
      // The impulse pre_gain * s, applied through its linear contribution
      // (states in Q25; the input itself can reach ~1600).
      const int32_t a = MulRound(MulRound(s, gh_), pre_gain_);
      const int32_t b = MulRound(MulRound(s, g2h_), pre_gain_);
      const int32_t gh_x = pre_gain_shift_ >= 0 ? ShlSat(a, pre_gain_shift_) : a >> (-pre_gain_shift_);
      const int32_t g2h_x = pre_gain_shift_ >= 0 ? ShlSat(b, pre_gain_shift_) : b >> (-pre_gain_shift_);
      filter_.Inject(gh_x, g2h_x);
      bp += gh_x;
    }
    *out++ += bp;
    u = Random::GetWord();
  }
}

void CosineOscillatorFixed::Init(uint32_t frequency) {
  // InitApproximate: sign 16, f - 0.25, folded; iir = sign f (1 - 2 f).
  int32_t f = static_cast<int32_t>(frequency >> 1) - (1 << 29);   // Q31, f - 0.25
  int32_t sign = 16;
  if (f < 0) {
    f = -f;
  } else if (f > (1 << 30)) {
    f -= 1 << 30;
  } else {
    sign = -16;
  }
  // iir = sign f (1 - 2 f), Q30 (|iir| <= 2).
  const int32_t t = Mul(f, INT32_MAX - (f << 1));   // f (1 - 2 f), Q31 (<= 0.125)
  iir_coefficient_ = sign > 0 ? t << 3 : -(t << 3);   // 16 t in Q30 = t << 3
  y1_ = iir_coefficient_ >> 1;   // iir * 0.25, Q31
  y0_ = 1 << 30;                 // 0.5
}

namespace {

// The diffuser's delay lines (FxEngine::Reserve): base and length.
const int32_t kAp1Base = 0, kAp1Length = 126;
const int32_t kAp2Base = 127, kAp2Length = 180;
const int32_t kAp3Base = 308, kAp3Length = 269;
const int32_t kAp4Base = 578, kAp4Length = 444;
const int32_t kDapaBase = 1023, kDapaLength = 1653;
const int32_t kDapbBase = 2677, kDapbLength = 2010;
const int32_t kDelBase = 4688, kDelLength = 3411;
const int32_t kMask = 8191;

// FORMAT_12_BIT: value * 4096 clipped to int16; Q25 <-> Q12.
inline int16_t Compress(int32_t x) {
  int32_t v = x >> 13;
  return static_cast<int16_t>(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
}
inline int32_t Decompress(int16_t v) { return static_cast<int32_t>(v) << 13; }

}  // namespace

void DiffuserFixed::Init(int16_t* buffer) {
  buffer_ = buffer;
  write_ptr_ = 0;
  // engine_.SetLFOFrequency(LFO_1, 0.3 Hz): stepped every 32 samples.
  lfo_.Init(Q32(0.3 / kSampleRateD * 32.0));
  lfo_value_ = lfo_.value();
  lp_decay_ = 0;
}

void DiffuserFixed::Reset() {
  for (int i = 0; i <= kMask; ++i) buffer_[i] = 0;
  write_ptr_ = 0;
}

void DiffuserFixed::Process(int32_t amount, int32_t rt, int32_t* in_out, size_t size) {
  const int32_t kap = Q31(0.625);
  const int32_t klp = Q31(0.75);
  int32_t lp = lp_decay_;
  int16_t* b = buffer_;
  while (size--) {
    --write_ptr_;
    if (write_ptr_ < 0) write_ptr_ += kMask + 1;
    const int32_t w = write_ptr_;
    lfo_value_ = (w & 31) == 0 ? lfo_.Next() : lfo_.value();

    int32_t acc = *in_out;
    int32_t r;
    // c.Read(apN TAIL, kap); c.WriteAllPass(apN, -kap).
#define ALLPASS(base, length, k) \
    r = Decompress(b[(w + base + length - 1) & kMask]); \
    acc += Mul(r, k); \
    b[(w + base) & kMask] = Compress(acc); \
    acc = Mul(acc, -(k)) + r;
    ALLPASS(kAp1Base, kAp1Length, kap)
    ALLPASS(kAp2Base, kAp2Length, kap)
    ALLPASS(kAp3Base, kAp3Length, kap)
    // c.Interpolate(ap4, 400 + 43 lfo, kap); c.WriteAllPass(ap4, -kap).
    {
      const int32_t offset = (400 << 16) + Mul(43 << 16, lfo_value_);   // Q16
      const int32_t oi = offset >> 16;
      const int32_t of = (offset & 0xffff) << 15;
      const int32_t x0 = Decompress(b[(w + oi + kAp4Base) & kMask]);
      const int32_t x1 = Decompress(b[(w + oi + kAp4Base + 1) & kMask]);
      r = x0 + Mul(x1 - x0, of);
      acc += Mul(r, kap);
      b[(w + kAp4Base) & kMask] = Compress(acc);
      acc = Mul(acc, -kap) + r;
    }
    // c.Interpolate(del, 3070 + 340 lfo, rt); c.Lp(lp, klp).
    {
      const int32_t offset = (3070 << 16) + Mul(340 << 16, lfo_value_);
      const int32_t oi = offset >> 16;
      const int32_t of = (offset & 0xffff) << 15;
      const int32_t x0 = Decompress(b[(w + oi + kDelBase) & kMask]);
      const int32_t x1 = Decompress(b[(w + oi + kDelBase + 1) & kMask]);
      acc += Mul(x0 + Mul(x1 - x0, of), rt);
      lp += Mul(acc - lp, klp);
      acc = lp;
    }
    ALLPASS(kDapaBase, kDapaLength, -kap)
    ALLPASS(kDapbBase, kDapbLength, kap)
#undef ALLPASS
    // c.Write(del, 2.0); c.Write(wet, 0.0).
    b[(w + kDelBase) & kMask] = Compress(acc);
    const int32_t wet = acc << 1;
    *in_out += Mul(wet - *in_out, amount);
    ++in_out;
  }
  lp_decay_ = lp;
}

void ParticleEngineFixed::Init(Allocator* allocator) {
  for (int i = 0; i < kNumParticles; ++i) particle_[i].Init();
  diffuser_.Init(allocator->Allocate<int16_t>(8192));
  post_filter_.Init();
}

void ParticleEngineFixed::Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
                                 size_t size, bool* already_enveloped) {
  (void)already_enveloped;
  const uint32_t f0 = NoteToFrequency(parameters.note);
  const int32_t t2 = MulQ15(parameters.timbre, parameters.timbre);
  const uint32_t density_sqrt = NoteToFrequency((60 << 16) + t2 * 144);   // 72 semitones
  // density = density_sqrt^2 / 6, Q32.
  const uint32_t density = static_cast<uint32_t>(
      ((static_cast<uint64_t>(density_sqrt) * density_sqrt) >> 32) * 715827883ULL >> 32);
  // q = 0.5 + SemitonesToRatio(morph >= 0.5 ? (morph - 0.5) 120 : 0)^2, Q16.
  const int32_t m = parameters.morph;
  const FFloat q_sqrt = SemitonesToRatio(m >= kQ15One / 2 ? (m - kQ15One / 2) * 240 : 0);
  static constexpr FFloat kHalf(0.5);
  const int32_t q = (kHalf + q_sqrt * q_sqrt).ToQ(16);
  // spread = 48 harmonics^2, Q16.
  const int32_t h2 = MulQ15(parameters.harmonics, parameters.harmonics);   // Q15
  const int32_t spread = h2 * 96;
  // raw_diffusion = (2 |morph - 0.5|)^2; diffusion = morph < 0.5 ? it : 0.
  const int32_t rds = Abs(m - kQ15One / 2) << 17;   // Q31 (2 |.|), saturates below
  const int32_t raw_diffusion_sqrt = rds < 0 ? INT32_MAX : rds;
  const int32_t diffusion = m < kQ15One / 2 ? Mul(raw_diffusion_sqrt, raw_diffusion_sqrt) : 0;
  const bool sync = parameters.trigger & TRIGGER_RISING_EDGE;

  for (size_t i = 0; i < size; ++i) out[i] = aux[i] = 0;
  for (int i = 0; i < kNumParticles; ++i) {
    particle_[i].Render(sync, density, f0, spread, q, out, aux, size);
  }

  // post_filter_: low-pass at min(f0, 0.49), Q 0.5 (Q25 signals).
  const uint32_t f_post = f0 < Q32(0.49) ? f0 : Q32(0.49);
  post_filter_.set_f_q<FREQUENCY_DIRTY>(f_post, 1 << 15);
  post_filter_.Process<FILTER_MODE_LOW_PASS>(out, out, size);


  diffuser_.Process(Mul(Mul(diffusion, diffusion), Q31(0.8)),
                    Mul(diffusion, 1 << 30) + Q31(0.25), out, size);
  for (size_t i = 0; i < size; ++i) {
    out[i] = ShlSat(out[i], kAudioBits - 25);
    aux[i] = ShlSat(aux[i], kAudioBits - 25);
  }
}

}  // namespace op1plaits
