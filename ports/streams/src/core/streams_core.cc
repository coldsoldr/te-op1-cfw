// The Streams effect core (src/core/streams_core.h).
//
// Formats: audio int16 in and out; inside the VCA and filter Q25 in int32
// (+-64: room for +18 dB of gain, the resonant peak and the SVF's doubled
// states); gains Q16; filter coefficients Q31 (src/core/core_tables.cc).

#include "src/core/streams_core.h"

#include <string.h>

#include "src/core/core_tables.h"
#include "src/dsp/resources.h"

#include "../../../rings/src/fixed.h"

namespace op1streams {

using op1rings::MulQ31Fast;

#if defined(OP1STREAMS_PROFILE) && defined(__bfin__)
static inline uint32_t Cycles() {
  uint32_t c;
  __asm__ __volatile__("%0 = CYCLES;" : "=d"(c));
  return c;
}
#define PROFILE_START uint32_t t0_ = Cycles(), t1_;
#define PROFILE_MARK(k) t1_ = Cycles(); profile[k] += t1_ - t0_; t0_ = t1_;
#else
#define PROFILE_START
#define PROFILE_MARK(k)
#endif

namespace {

inline int16_t SatInt16(int32_t x) {
  return x > 32767 ? 32767 : (x < -32768 ? -32768 : static_cast<int16_t>(x));
}

// |x| for an int16-range x, with |-32768| = 32767 on every target: GCC 14
// for Blackfin compiles `x < 0 ? -x : x` on such values to the saturating
// 16-bit `ABS (V)`, which gives 32767 where the host gives 32768.
inline int32_t Abs16(int32_t x) {
  return x < -32767 ? 32767 : (x < 0 ? -x : x);
}

// Knob 0..32767 -> upstream's 16-bit pot value 0..65535.
inline uint16_t KnobToPot(int16_t v) {
  int32_t x = v < 0 ? 0 : v;
  return static_cast<uint16_t>((x << 1) | (x >> 14));
}

// (x * g) >> 6 for a 16-bit x and a gain g in [0, 2^19] (Q16): Q15 -> Q25,
// exact, without a 64-bit product.
inline int32_t VcaMultiply(int32_t x, int32_t g) {
  return ((x * (g >> 16)) << 10) + ((x * (g & 0xffff)) >> 6);
}

}  // namespace

int32_t Exp2Q16(int32_t x) {
  int32_t n = x >> 16;
  uint32_t frac = static_cast<uint32_t>(x) & 0xffff;
  int32_t a = lut_exp2[frac >> 8];
  int32_t b = lut_exp2[(frac >> 8) + 1];
  int32_t m = a + ((b - a) * static_cast<int32_t>(frac & 0xff) >> 8);
  return n >= 0 ? m << n : m >> -n;
}

int32_t VcaGainQ16(int32_t code, int32_t response_q15) {
  int32_t lin = code << 1;  // code / 32767, Q16 (32767 -> 0.99997)
  if (response_q15 >= 32767) {
    return lin;
  }
  // log2(gain) in Q16: (code - 32767) / 991 octaves = x * 66.125 / 65536.
  int32_t log2_gain = ((code - kVcaUnityCode) * 4232) >> 6;
  int32_t ex;
  if (log2_gain >= kVcaMaxLog2Q16) {
    ex = Exp2Q16(kVcaMaxLog2Q16);
  } else if (log2_gain < kVcaMinLog2Q16) {
    ex = 0;
  } else {
    ex = Exp2Q16(log2_gain);
  }
  if (response_q15 <= 0) {
    return ex;
  }
  int32_t g = ex + ((((lin - ex) >> 5) * response_q15) >> 10);
  return g > kVcaMaxGainQ16 ? kVcaMaxGainQ16 : g;
}

void DefaultControls(Controls* c) {
  c->shape = 16384;
  c->mod = 12288;
  c->sense = 0;
  c->mix = 32767;
  c->attack = 8192;
  c->decay = 16384;
  c->response = 32767;
  c->output = 16384;
  c->function = PROCESSOR_FUNCTION_COMPRESSOR;
  c->alternate = 0;
  c->split = 0;
  c->filter_mode = FILTER_LP;
  c->detector = DETECTOR_ENVELOPE;
}

void StreamsCore::Init() {
  // Upstream relies on zeroed globals for members its Init()s don't set.
  memset(static_cast<void*>(this), 0, sizeof(*this));
  processor_.Init(0);
  configured_ = false;
}

void StreamsCore::Restart() {
  Init();
}

void StreamsCore::Apply(const Controls& c) {
  const bool first = !configured_;
  if (first || c.function != applied_.function) {
    processor_.set_function(static_cast<ProcessorFunction>(
        c.function < PROCESSOR_FUNCTION_LAST ? c.function : 0));
    svf_[0].ic1 = svf_[0].ic2 = svf_[1].ic1 = svf_[1].ic2 = 0;
    coefficients_valid_ = false;
  }
  if (first || c.filter_mode != applied_.filter_mode) {
    coefficients_valid_ = false;  // another damping table
  }
  if (first || c.alternate != applied_.alternate) {
    processor_.set_alternate(c.alternate != 0);
  }
  if (first || c.split != applied_.split) {
    processor_.set_linked(c.split != 0);
  }
  if (first || c.shape != applied_.shape || c.mod != applied_.mod ||
      c.attack != applied_.attack || c.decay != applied_.decay) {
    // Upstream's order (Ui::OnPotMoved): set_global writes dirty_ = linked_.
    // Linked globals: ch1 SHAPE = attack, ch1 MOD = threshold (compressor),
    // ch2 SHAPE = decay, ch2 MOD = ratio (compressor).
    processor_.set_global(0, KnobToPot(c.attack));
    processor_.set_global(1, KnobToPot(c.shape));
    processor_.set_global(2, KnobToPot(c.decay));
    processor_.set_global(3, KnobToPot(c.mod));
    processor_.set_parameter(0, KnobToPot(c.shape));
    processor_.set_parameter(1, KnobToPot(c.mod));
  }
  processor_.Configure();  // no-op unless something changed

  const uint8_t f = c.function;
  bypass_filter_ = f == PROCESSOR_FUNCTION_COMPRESSOR ||
      (f == PROCESSOR_FUNCTION_FOLLOWER && c.alternate);
  unity_vca_ = f == PROCESSOR_FUNCTION_FILTER_CONTROLLER;
  response_ = f == PROCESSOR_FUNCTION_COMPRESSOR ? 0 : c.response;

  sense_target_q8_ = Exp2Q16((c.sense < 0 ? 0 : c.sense) * (2 * kSenseOctaves)) >> 8;
  trim_target_q16_ = Exp2Q16((c.output - 16384) * 8);
  mix_target_q14_ = ((c.mix < 0 ? 0 : c.mix) + 1) >> 1;
  if (first) {
    sense_q8_ = sense_target_q8_;
    trim_q16_ = trim_target_q16_;
    mix_q14_ = mix_target_q14_;
  }
  applied_ = c;
  configured_ = true;
}

void StreamsCore::Detect(int32_t n) {
  int32_t s = sense_q8_;
  int32_t step = 0;
  if (n == kBlockSize) {
    step = (sense_target_q8_ - s) >> 6;
  } else {
    s = sense_target_q8_;
  }
  const uint8_t f = applied_.function;
  const bool envelope = applied_.detector == DETECTOR_ENVELOPE &&
      f != PROCESSOR_FUNCTION_FOLLOWER && f != PROCESSOR_FUNCTION_COMPRESSOR;
  int32_t env = detector_env_;
  for (int32_t i = 0; i < n; ++i) {
    s += step;
    int32_t e = SatInt16((detector_in_[i] * s) >> 8);
    if (envelope) {
      int32_t a = Abs16(e) << 15;
      int32_t d = a - env;
      env += d > 0 ? d >> kDetectorAttackShift : d >> kDetectorReleaseShift;
      e = env >> 15;
      if (e > 32767) e = 32767;
    }
    excite_[i] = static_cast<int16_t>(e);
  }
  detector_env_ = env;
  sense_q8_ = sense_target_q8_;
}

void StreamsCore::RunProcessor(int32_t n) {
#if OP1STREAMS_SAMPLE_RATE == 22050
  const int32_t m = n >> 1;
  for (int32_t k = 0; k < m; ++k) {
    excite_decimated_[k] = static_cast<int16_t>(
        (excite_[2 * k] + excite_[2 * k + 1]) >> 1);
  }
  processor_.ProcessBlock(excite_decimated_, excite_decimated_,
                          gain_decimated_, frequency_decimated_, m);
  if (!last_valid_ && m > 0) {
    last_gain_ = gain_decimated_[0];
    last_frequency_ = frequency_decimated_[0];
    last_valid_ = true;
  }
  int32_t lg = last_gain_, lf = last_frequency_;
  for (int32_t k = 0; k < m; ++k) {
    int32_t g = gain_decimated_[k], f = frequency_decimated_[k];
    gain_[2 * k] = static_cast<uint16_t>((lg + g) >> 1);
    frequency_[2 * k] = static_cast<uint16_t>((lf + f) >> 1);
    gain_[2 * k + 1] = static_cast<uint16_t>(g);
    frequency_[2 * k + 1] = static_cast<uint16_t>(f);
    lg = g;
    lf = f;
  }
  if (n & 1) {
    gain_[n - 1] = static_cast<uint16_t>(lg);
    frequency_[n - 1] = static_cast<uint16_t>(lf);
  }
  last_gain_ = lg;
  last_frequency_ = lf;
#else
  processor_.ProcessBlock(excite_, excite_, gain_, frequency_, n);
#endif
}

void StreamsCore::ComputeGainsAndCoefficients(int32_t n) {
  int32_t t = trim_q16_;
  int32_t step = 0;
  if (n == kBlockSize) {
    step = (trim_target_q16_ - t) >> 6;
  } else {
    t = trim_target_q16_;
  }
  // Per-block special cases keep the common loops trivial: linear response
  // (every function but the compressor, by default) is code * 2, and a
  // steady 0 dB trim needs no multiply. Same results as the general path.
  if (unity_vca_) {
    const int32_t g = VcaGainQ16(kVcaUnityCode, response_);
    for (int32_t i = 0; i < n; ++i) vca_q16_[i] = g;
  } else if (response_ >= 32767) {
    for (int32_t i = 0; i < n; ++i) vca_q16_[i] = gain_[i] << 1;
  } else {
    for (int32_t i = 0; i < n; ++i) vca_q16_[i] = VcaGainQ16(gain_[i], response_);
  }
  if (step != 0 || t != 65536) {
    for (int32_t i = 0; i < n; ++i) {
      t += step;
      // g * t >> 16, both < 2^19.
      int32_t g = MulQ31Fast(vca_q16_[i] << 11, t << 11) >> 7;
      vca_q16_[i] = g > kVcaMaxGainQ16 ? kVcaMaxGainQ16 : g;
    }
  }
  trim_q16_ = trim_target_q16_;
  if (bypass_filter_) {
    return;
  }
  const int32_t damping = (applied_.filter_mode == FILTER_LP_RESONANT ||
                           applied_.filter_mode == FILTER_BP_RESONANT) ? 1 : 0;
  const int32_t (*table)[3] = kSvfTable[damping];
  // Coefficients from the cutoff code of every kCoefficientStride-th sample
  // (the last of each group), stepped linearly from the previous group's:
  // a quarter of the table work, about 2 samples of extra lag on the cutoff.
  // The filter controller modulates the cutoff at audio rate (EXCITE is the
  // audio): per-sample coefficients there (its processor is cheap).
  const int32_t grouped = unity_vca_ ? 0 : n;
  int32_t i = 0;
  for (; i + kCoefficientStride <= grouped; i += kCoefficientStride) {
    int32_t f = frequency_[i + kCoefficientStride - 1];
    const int32_t* lo = table[f >> 8];
    const int32_t* hi = lo + 3;
    int32_t frac = (f & 0xff) << 23;
    int32_t target[3];
    target[0] = lo[0] + MulQ31Fast(hi[0] - lo[0], frac);
    target[1] = lo[1] + MulQ31Fast(hi[1] - lo[1], frac);
    target[2] = lo[2] + MulQ31Fast(hi[2] - lo[2], frac);
    if (!coefficients_valid_) {
      for (int k = 0; k < 3; ++k) last_coefficients_[k] = target[k];
      coefficients_valid_ = true;
    }
    for (int k = 0; k < 3; ++k) {
      int32_t c = last_coefficients_[k];
      int32_t d = (target[k] - c) >> kCoefficientStrideShift;
      for (int32_t j = 0; j < kCoefficientStride - 1; ++j) {
        c += d;
        coefficients_[i + j][k] = c;
      }
      coefficients_[i + kCoefficientStride - 1][k] = target[k];
      last_coefficients_[k] = target[k];
    }
  }
  for (; i < n; ++i) {  // partial blocks, filter controller
    int32_t f = frequency_[i];
    const int32_t* lo = table[f >> 8];
    const int32_t* hi = lo + 3;
    int32_t frac = (f & 0xff) << 23;
    for (int k = 0; k < 3; ++k) {
      coefficients_[i][k] = lo[k] + MulQ31Fast(hi[k] - lo[k], frac);
      last_coefficients_[k] = coefficients_[i][k];
    }
    coefficients_valid_ = true;
  }
}

void StreamsCore::RenderChannel(const int16_t* in, int16_t* wet, SvfState* svf, int32_t n) {
  if (bypass_filter_) {
    for (int32_t i = 0; i < n; ++i) {
      int32_t s = VcaMultiply(in[i], vca_q16_[i]);
      wet[i] = SatInt16((s + 512) >> 10);
    }
    return;
  }
  const uint8_t mode = applied_.filter_mode;
  const bool band_pass = mode == FILTER_BP || mode == FILTER_BP_RESONANT;
  const bool resonant = mode == FILTER_LP_RESONANT || mode == FILTER_BP_RESONANT;
  int32_t ic1 = svf->ic1;
  int32_t ic2 = svf->ic2;
  for (int32_t i = 0; i < n; ++i) {
    const int32_t a1 = coefficients_[i][0];
    const int32_t a2 = coefficients_[i][1];
    const int32_t a3 = coefficients_[i][2];
    int32_t x = VcaMultiply(in[i], vca_q16_[i]);
    int32_t v3 = x - ic2;
    int32_t v1 = MulQ31Fast(a1, ic1) + MulQ31Fast(a2, v3);
    int32_t v2 = ic2 + MulQ31Fast(a2, ic1) + MulQ31Fast(a3, v3);
    ic1 = 2 * v1 - ic1;
    ic2 = 2 * v2 - ic2;
    int32_t y;
    if (band_pass) {
      // k * v1: unity gain at the centre frequency.
      y = resonant ? v1 >> 1 : v1 + MulQ31Fast(v1, 889516852);  // sqrt(2) - 1
    } else {
      y = v2;
    }
    wet[i] = SatInt16((y + 512) >> 10);
  }
  svf->ic1 = ic1;
  svf->ic2 = ic2;
}

void StreamsCore::Mix(int16_t* buf, const int16_t* wet, int32_t n) {
  int32_t m = mix_q14_;
  int32_t step = 0;
  if (n == kBlockSize) {
    step = (mix_target_q14_ - m) >> 6;
  } else {
    m = mix_target_q14_;
  }
  if (m == 16384 && step == 0) {
    for (int32_t i = 0; i < n; ++i) buf[i] = wet[i];
    return;
  }
  for (int32_t i = 0; i < n; ++i) {
    m += step;
    int32_t dry = buf[i];
    buf[i] = static_cast<int16_t>(dry + (((wet[i] - dry) * m) >> 14));
  }
}

void StreamsCore::ProcessMono(const Controls& c, int16_t* buf, int32_t n) {
  if (n > kBlockSize) n = kBlockSize;
  PROFILE_START
  Apply(c);
  for (int32_t i = 0; i < n; ++i) detector_in_[i] = buf[i];
  Detect(n);
  PROFILE_MARK(0)
  RunProcessor(n);
  PROFILE_MARK(1)
  ComputeGainsAndCoefficients(n);
  PROFILE_MARK(2)
  RenderChannel(buf, wet_, &svf_[0], n);
  Mix(buf, wet_, n);
  mix_q14_ = mix_target_q14_;
  PROFILE_MARK(3)
}

void StreamsCore::ProcessStereo(const Controls& c, int16_t* l, int16_t* r, int32_t n) {
  if (n > kBlockSize) n = kBlockSize;
  PROFILE_START
  Apply(c);
  // Linked detection. The follower's band analysis and the filter
  // controller want a waveform: the mid signal. The others react to level:
  // whichever side is larger, sign kept.
  const uint8_t f = applied_.function;
  if (f == PROCESSOR_FUNCTION_FOLLOWER || f == PROCESSOR_FUNCTION_FILTER_CONTROLLER) {
    for (int32_t i = 0; i < n; ++i) detector_in_[i] = (l[i] + r[i]) >> 1;
  } else {
    for (int32_t i = 0; i < n; ++i) {
      int32_t a = l[i], b = r[i];
      int32_t ma = Abs16(a);
      int32_t mb = Abs16(b);
      detector_in_[i] = SatInt16(ma >= mb ? a : b);
    }
  }
  Detect(n);
  PROFILE_MARK(0)
  RunProcessor(n);
  PROFILE_MARK(1)
  ComputeGainsAndCoefficients(n);
  PROFILE_MARK(2)
  RenderChannel(l, wet_, &svf_[0], n);
  Mix(l, wet_, n);
  RenderChannel(r, wet_, &svf_[1], n);
  Mix(r, wet_, n);
  mix_q14_ = mix_target_q14_;
  PROFILE_MARK(3)
}

}  // namespace op1streams
