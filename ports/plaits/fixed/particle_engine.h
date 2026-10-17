// Plaits' particle engine (plaits/dsp/engine/particle_engine.cc, noise/
// particle.h, fx/diffuser.h) in fixed point: six dust generators, each
// through a band-pass whose frequency is randomised per burst, a low-pass,
// and the 12-bit diffuser (allpass chain + modulated delay).

#ifndef OP1PLAITS_FIXED_PARTICLE_ENGINE_H_
#define OP1PLAITS_FIXED_PARTICLE_ENGINE_H_

#include "fixed/fixed_math.h"
#include "fixed/svf.h"

namespace op1plaits {

const int kNumParticles = 6;

// The particle filters keep their states in Q25; the impulse (pre-gain up to
// ~1600) is applied through its linear contribution (SvfFixed::Inject).
class ParticleFixed {
 public:
  void Init() {
    pre_gain_ = 0;
    pre_gain_shift_ = 0;
    gh_ = g2h_ = 0;
    filter_.Init();
  }
  // density: Q32; frequency: Q32; spread: Q16 semitones; q: Q16.
  // out: Q25 (adds); aux: Q25 (adds).
  void Render(bool sync, uint32_t density, uint32_t frequency, int32_t spread, int32_t q,
              int32_t* out, int32_t* aux, size_t size);

 private:
  int32_t pre_gain_;         // FFloat mantissa
  int32_t pre_gain_shift_;   // its exponent + 25
  int32_t gh_;               // g h, Q31 (InjectionGains)
  int32_t g2h_;              // g^2 h, Q31
  SvfFixed filter_;
};

// stmlib::CosineOscillator, COSINE_OSCILLATOR_APPROXIMATE.
class CosineOscillatorFixed {
 public:
  // frequency: Q32 (cycles per call of Next()).
  void Init(uint32_t frequency);
  // y + 0.5 in Q31, saturated (1.0 is INT32_MAX).
  inline int32_t value() const { return AddSat(y1_, 1 << 30); }   // Q31, [0, 1]
  inline int32_t Next() {
    const int32_t temp = y0_;
    y0_ = (Mul(iir_coefficient_, y0_) << 1) - y1_;
    y1_ = temp;
    return AddSat(temp, 1 << 30);
  }

 private:
  int32_t y1_;                  // Q31
  int32_t y0_;
  int32_t iir_coefficient_;     // Q30
};

// plaits::Diffuser on FxEngine<8192, FORMAT_12_BIT>. Signals Q25; the
// buffer stores value * 4096 as int16, as upstream.
class DiffuserFixed {
 public:
  void Init(int16_t* buffer);
  void Reset();
  // amount, rt: Q31.
  void Process(int32_t amount, int32_t rt, int32_t* in_out, size_t size);

 private:
  int16_t* buffer_;
  int32_t write_ptr_;
  CosineOscillatorFixed lfo_;
  int32_t lfo_value_;
  int32_t lp_decay_;   // Q25
};

class ParticleEngineFixed {
 public:
  void Init(Allocator* allocator);
  void Reset() { diffuser_.Reset(); }
  void Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
              size_t size, bool* already_enveloped);

 private:
  ParticleFixed particle_[kNumParticles];
  DiffuserFixed diffuser_;
  SvfFixed post_filter_;
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_PARTICLE_ENGINE_H_
