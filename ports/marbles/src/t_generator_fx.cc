// Marbles' T generator in fixed point. See t_generator_fx.h.
//
// Based on Mutable Instruments Marbles (MIT, (c) 2015 Emilie Gillet).

#include "src/t_generator_fx.h"

#include "src/distributions_fx.h"
#include "src/tables_fx.h"

namespace op1marbles {
namespace fx {

/* static */
const DividerPattern TGenerator::divider_patterns[kNumDividerPatterns] = {
  { { { 1, 1 }, { 1, 1 } }, 1 },
  { { { 1, 1 }, { 2, 1 } }, 1 },
  { { { 1, 2 }, { 1, 1 } }, 2 },
  { { { 1, 1 }, { 4, 1 } }, 1 },
  { { { 1, 2 }, { 2, 1 } }, 2 },
  { { { 1, 1 }, { 3, 2 } }, 2 },
  { { { 1, 4 }, { 4, 1 } }, 4 },
  { { { 1, 4 }, { 2, 1 } }, 4 },
  { { { 1, 2 }, { 3, 2 } }, 2 },
  { { { 1, 1 }, { 8, 1 } }, 1 },
  { { { 1, 1 }, { 3, 1 } }, 1 },
  { { { 1, 3 }, { 1, 1 } }, 3 },
  { { { 1, 1 }, { 5, 4 } }, 4 },
  { { { 1, 2 }, { 5, 4 } }, 4 },
  { { { 1, 1 }, { 6, 1 } }, 1 },
  { { { 1, 3 }, { 2, 1 } }, 3 },
  { { { 1, 1 }, { 16, 1 } }, 1 },
};

/* static */
const DividerPattern TGenerator::fixed_divider_patterns[kNumDividerPatterns] = {
  { { { 8, 1 }, { 1, 8 } }, 8 },
  { { { 6, 1 }, { 1, 6 } }, 6 },
  { { { 4, 1 }, { 1, 4 } }, 4 },
  { { { 3, 1 }, { 1, 3 } }, 3 },
  { { { 2, 1 }, { 1, 2 } }, 2 },
  { { { 3, 2 }, { 2, 3 } }, 6 },
  { { { 4, 3 }, { 3, 4 } }, 12 },
  { { { 5, 4 }, { 4, 5 } }, 20 },
  { { { 1, 1 }, { 1, 1 } }, 1 },
  { { { 4, 5 }, { 5, 4 } }, 20 },
  { { { 3, 4 }, { 4, 3 } }, 12 },
  { { { 2, 2 }, { 3, 2 } }, 6 },
  { { { 1, 2 }, { 2, 1 } }, 2 },
  { { { 1, 3 }, { 3, 1 } }, 3 },
  { { { 1, 4 }, { 4, 1 } }, 4 },
  { { { 1, 6 }, { 6, 1 } }, 6 },
  { { { 1, 8 }, { 8, 1 } }, 8 },
};

/* static */
const uint8_t TGenerator::drum_patterns[kNumDrumPatterns][kDrumPatternSize] = {
  { 1, 0, 0, 0, 2, 0, 0, 0 },
  { 0, 0, 1, 0, 2, 0, 0, 0 },
  { 1, 0, 1, 0, 2, 0, 0, 0 },
  { 0, 0, 1, 0, 2, 0, 0, 2 },
  { 1, 0, 1, 0, 2, 0, 1, 0 },
  { 0, 2, 1, 0, 2, 0, 0, 2 },
  { 1, 0, 0, 0, 2, 0, 1, 0 },
  { 0, 2, 1, 0, 2, 0, 1, 2 },
  { 1, 0, 0, 1, 2, 0, 0, 0 },
  { 0, 2, 1, 1, 2, 0, 1, 2 },
  { 1, 0, 0, 1, 2, 0, 1, 0 },
  { 0, 2, 1, 1, 2, 2, 1, 2 },
  { 1, 0, 0, 1, 2, 0, 1, 2 },
  { 0, 2, 0, 1, 2, 0, 1, 2 },
  { 1, 0, 1, 1, 2, 0, 1, 2 },
  { 2, 0, 1, 2, 0, 1, 2, 0 },
  { 1, 2, 1, 1, 2, 0, 1, 2 },
  { 2, 0, 1, 2, 0, 1, 2, 2 }
};

// 0.1f = 13421773 * 2^-27 = 26843546 * 2^-28.
static const int32_t kHysteresis01fQ28 = 26843546;
// 0.05f, 0.9f in Q32 (exact).
static const uint32_t k09fQ32 = 3865470464u;
// 12.8f = 13421773 * 2^-20.
static const int64_t k128fMantissa = 13421773;

void TGenerator::Init(RandomStream* random_stream, Float24 one_hertz) {
  one_hertz_ = one_hertz;
  model_ = T_GENERATOR_MODEL_COMPLEMENTARY_BERNOULLI;
  range_ = T_GENERATOR_RANGE_1X;
  rate_q8_ = 0;
  bias_ = kQ16One / 2;
  jitter_ = 0;
  pulse_width_mean_ = 0;
  pulse_width_std_ = 0;
  external_frequency_ = 0;
  cached_rate_q8_ = 0x7fffffff;
  cached_range_ = T_GENERATOR_RANGE_1X;
  cached_frequency_ = 0;

  master_phase_ = 0;
  jitter_multiplier_ = RatioToFloat(1, 1);
  phase_difference_ = 0;

  divider_pattern_length_ = 0;
  for (int i = 0; i < kMarkovHistorySize; ++i) {
    streak_counter_[i] = 0;
    markov_history_[i] = 0;
  }
  markov_history_ptr_ = 0;
  drum_pattern_step_ = 0;
  drum_pattern_index_ = 0;

  sequence_.Init(random_stream);
  for (int i = 0; i < kNumTChannels; ++i) {
    slave_ramp_[i].Init();
  }
  bias_quantizer_.Init(kNumDividerPatterns, kHysteresis01fQ28, false);
}

// 0.05 + 0.9 * x, x Q32 in 64 bits (may be 1.0). Result Q32.
static inline uint32_t PulseWidthFromUnit(uint64_t x) {
  uint64_t scaled = x >= (1ULL << 32) ? k09fQ32 : MulU32(static_cast<uint32_t>(x), k09fQ32);
  return k005fQ32 + static_cast<uint32_t>(scaled);
}

uint32_t TGenerator::RandomPulseWidth(int i, uint32_t u) {
  if (pulse_width_std_ == 0) {
    return PulseWidthFromUnit(static_cast<uint64_t>(pulse_width_mean_) << 16);
  }
  uint32_t b = BetaDistributionSample(u, pulse_width_std_, pulse_width_mean_);  // Q31
  return PulseWidthFromUnit(static_cast<uint64_t>(b) << 1);
}

// (u > bias) for a Q32 u and a Q16 bias, exactly as the float comparison.
static inline bool Above(uint32_t u, int32_t bias) {
  return static_cast<uint64_t>(u) > (static_cast<uint64_t>(bias) << 16);
}

int TGenerator::GenerateComplementaryBernoulli(const uint32_t* v) {
  int bitmask = 0;
  for (int i = 0; i < kNumTChannels; ++i) {
    if (Above(v[U0 + (i >> 1)], bias_) ^ (i & 1)) {
      bitmask |= 1 << i;
    }
  }
  return bitmask;
}

int TGenerator::GenerateIndependentBernoulli(const uint32_t* v) {
  int bitmask = 0;
  for (int i = 0; i < kNumTChannels; ++i) {
    if (Above(v[U0 + i], bias_) ^ (i & 1)) {
      bitmask |= 1 << i;
    }
  }
  return bitmask;
}

int TGenerator::GenerateThreeStates(const uint32_t* v) {
  int bitmask = 0;
  // p_none = 0.75 - |bias - 0.5| (exact, Q16)
  int32_t dev = bias_ - kQ16One / 2;
  if (dev < 0) dev = -dev;
  int64_t p_none = 3 * kQ16One / 4 - dev;
  // threshold = p_none + (1 - p_none) * (0.25 + bias * 0.5), float roundings
  // emulated at scale 2^-33.
  int64_t a = kQ16One - p_none;                    // Q16
  int64_t b = (kQ16One / 2) + bias_;               // 0.25 + bias / 2, Q17
  int64_t prod = static_cast<int64_t>(RoundToFloat(static_cast<uint64_t>(a * b)));  // Q33
  int64_t threshold = static_cast<int64_t>(RoundToFloat(
      static_cast<uint64_t>((p_none << 17) + prod)));  // Q33
  for (int i = 0; i < kNumTChannels; ++i) {
    uint64_t u = v[U0 + (i >> 1)];
    bool above_none = static_cast<int64_t>(u) > (p_none << 16);
    bool above_threshold = static_cast<int64_t>(u << 1) > threshold;
    if (above_none && (above_threshold ^ (i & 1))) {
      bitmask |= 1 << i;
    }
  }
  return bitmask;
}

int TGenerator::GenerateDrums(const uint32_t* v) {
  ++drum_pattern_step_;
  if (drum_pattern_step_ >= kDrumPatternSize) {
    drum_pattern_step_ = 0;
    // u = u0 * 2 * |bias - 0.5|; index = int(18 * u) (float roundings
    // emulated: u0 * 2 exact, the product rounded, then 18 * u rounded).
    int32_t dev = bias_ - kQ16One / 2;
    if (dev < 0) dev = -dev;  // Q16
    uint64_t u = RoundToFloat(Mul64(v[U0], static_cast<uint32_t>(dev)));  // * 2^-47
    uint64_t x = RoundToFloat(u * kNumDrumPatterns);
    drum_pattern_index_ = static_cast<int>(x >> 47);
    if (bias_ <= kQ16One / 2) {
      drum_pattern_index_ -= drum_pattern_index_ % 2;
    }
  }
  return drum_patterns[drum_pattern_index_][drum_pattern_step_];
}

int TGenerator::GenerateMarkov(const uint32_t* v) {
  int bitmask = 0;
  // b = 1.5 * bias - 0.5, exact in Q17. The logit is kept in Q34 with every
  // float addition/multiplication rounded as upstream's.
  const int64_t b = 3 * static_cast<int64_t>(bias_) - kQ16One;  // Q17
  const int64_t abs8b = 8 * (b < 0 ? -b : b);                   // Q17
  markov_history_[markov_history_ptr_] = 0;
  const int32_t p = markov_history_ptr_;
  for (int i = 0; i < kNumTChannels; ++i) {
    int32_t mask = 1 << i;
    bool periodic = markov_history_[(p + 8) % kMarkovHistorySize] & mask;
    bool simultaneous = markov_history_[(p + 8) % kMarkovHistorySize] & ~mask;
    bool dense = markov_history_[(p + 1) % kMarkovHistorySize] & mask;
    bool alternate = markov_history_[(p + 4) % kMarkovHistorySize] & ~mask;

    int64_t logit = -(3LL << 33);  // -1.5
    logit = RoundToFloatSigned(logit + (streak_counter_[i] > 24 ? (10LL << 34) : 0));
    int64_t t = RoundToFloatSigned(abs8b * (periodic ? b : -b));  // Q34
    logit = RoundToFloatSigned(logit + t);
    logit = RoundToFloatSigned(logit - (simultaneous ? 2 * b : -2 * b) * (1LL << 17));
    logit = RoundToFloatSigned(logit - (dense ? b : 0) * (1LL << 17));
    logit = RoundToFloatSigned(logit + (alternate ? b : 0) * (1LL << 17));
    if (logit < -(10LL << 34)) logit = -(10LL << 34);
    if (logit > (10LL << 34)) logit = 10LL << 34;
    // int(logit * 12.8f + 128.0f), in Q54.
    int64_t x = RoundToFloatSigned(logit * k128fMantissa);
    x = RoundToFloatSigned(x + (1LL << 61));
    int index = static_cast<int>(x >> 54);
    bool state = v[U0 + i] <= lut_logit_threshold[index];

    if ((static_cast<uint64_t>(sequence_.deja_vu()) << 16) >= v[P]) {
      state = markov_history_[(p + sequence_.length()) % kMarkovHistorySize] & mask;
    }
    if (state) {
      bitmask |= mask;
      streak_counter_[i] = 0;
    } else {
      ++streak_counter_[i];
    }
  }
  markov_history_[p] |= bitmask;
  markov_history_ptr_ = (p + kMarkovHistorySize - 1) % kMarkovHistorySize;
  return bitmask;
}

void TGenerator::ScheduleOutputPulses(const uint32_t* v, int bitmask) {
  for (int i = 0; i < kNumTChannels; ++i) {
    slave_ramp_[i].Init(bitmask & 1, RandomPulseWidth(i, v[PW0 + i]));
    bitmask >>= 1;
  }
}

void TGenerator::ConfigureSlaveRamps(const uint32_t* v) {
  switch (model_) {
    case T_GENERATOR_MODEL_COMPLEMENTARY_BERNOULLI:
      ScheduleOutputPulses(v, GenerateComplementaryBernoulli(v));
      break;
    case T_GENERATOR_MODEL_INDEPENDENT_BERNOULLI:
      ScheduleOutputPulses(v, GenerateIndependentBernoulli(v));
      break;
    case T_GENERATOR_MODEL_THREE_STATES:
      ScheduleOutputPulses(v, GenerateThreeStates(v));
      break;
    case T_GENERATOR_MODEL_DRUMS:
      ScheduleOutputPulses(v, GenerateDrums(v));
      break;
    case T_GENERATOR_MODEL_MARKOV:
      ScheduleOutputPulses(v, GenerateMarkov(v));
      break;
    case T_GENERATOR_MODEL_CLUSTERS:
    case T_GENERATOR_MODEL_DIVIDER:
      --divider_pattern_length_;
      if (divider_pattern_length_ <= 0) {
        DividerPattern pattern;
        if (model_ == T_GENERATOR_MODEL_DIVIDER) {
          pattern = fixed_divider_patterns[bias_quantizer_.Process(bias_)];
        } else {
          // strength = |bias - 0.5| * 2; u *= u + strength^2 (1 - u);
          // u *= strength; pattern = int(u * 17). Q32 arithmetic.
          int32_t dev = bias_ - kQ16One / 2;
          if (dev < 0) dev = -dev;
          uint64_t strength = static_cast<uint64_t>(dev) << 17;     // Q32
          uint64_t s2 = (strength * strength) >> 32;                 // Q32
          uint64_t u = v[U0];
          uint64_t inner = u + ((s2 * ((1ULL << 32) - u)) >> 32);    // Q32
          u = (u * inner) >> 32;
          u = (u * strength) >> 32;
          int index = static_cast<int>((u * kNumDividerPatterns) >> 32);
          if (index >= kNumDividerPatterns) index = kNumDividerPatterns - 1;
          pattern = divider_patterns[index];
          if (bias_ < kQ16One / 2) {
            Ratio r = pattern.ratios[0];
            pattern.ratios[0] = pattern.ratios[1];
            pattern.ratios[1] = r;
          }
        }
        for (int i = 0; i < kNumTChannels; ++i) {
          slave_ramp_[i].Init(
              pattern.length,
              pattern.ratios[i],
              RandomPulseWidth(i, v[PW0 + i]));
        }
        divider_pattern_length_ = pattern.length;
      }
      break;
  }
}

uint64_t TGenerator::InternalFrequency() {
  if (rate_q8_ == cached_rate_q8_ && range_ == cached_range_) {
    return cached_frequency_;
  }
  cached_rate_q8_ = rate_q8_;
  cached_range_ = range_;
  // rate * one_hertz * SemitonesToRatio(rate), rate = 2 Hz (x 4 or / 4):
  // (rate * one_hertz) is exact, the product with the ratio is rounded.
  Float24 base = one_hertz_;
  base.e += 1;
  if (range_ == T_GENERATOR_RANGE_4X) {
    base.e += 2;
  } else if (range_ == T_GENERATOR_RANGE_0_25X) {
    base.e -= 2;
  }
  cached_frequency_ = Float24ToFixed(Float24Mul(base, SemitonesToRatioFloat(rate_q8_)), 40);
  return cached_frequency_;
}

// --- The jitter multiplier, with upstream's float arithmetic emulated ----
// Values are held at scale 2^-48 (every float above 2^-24 is exact there).

static inline int64_t FMul48(int64_t a, int64_t b) {
  bool negative = (a < 0) != (b < 0);
  uint64_t ua = a < 0 ? static_cast<uint64_t>(-a) : static_cast<uint64_t>(a);
  uint64_t ub = b < 0 ? static_cast<uint64_t>(-b) : static_cast<uint64_t>(b);
  if (!ua || !ub) return 0;
  int sa = 40 - Clz64(ua);
  int sb = 40 - Clz64(ub);
  if (sa < 0) sa = 0;
  if (sb < 0) sb = 0;
  uint64_t p = RoundToFloat(Mul64(static_cast<uint32_t>(ua >> sa), static_cast<uint32_t>(ub >> sb)));
  int shift = sa + sb - 48;
  uint64_t v = shift >= 0 ? (p << shift) : (shift <= -64 ? 0 : (p >> -shift));
  return negative ? -static_cast<int64_t>(v) : static_cast<int64_t>(v);
}

static inline int64_t FAdd48(int64_t a, int64_t b) {
  return RoundToFloatSigned(a + b);
}

// SemitonesToRatio((2 * FastBeta(u) - 1) * jitter^4 * 36), as upstream
// computes it in float. jitter Q16, u Q32.
static Float24 JitterRatio(int32_t jitter, uint32_t u) {
  int64_t j = static_cast<int64_t>(jitter) << 32;
  int64_t amount = FMul48(FMul48(FMul48(FMul48(j, j), j), j), 36LL << 48);
  // FastBetaDistributionSample: Interpolate(dist_icdf_4_3, u, 128).
  uint32_t i = u >> 25;
  int64_t frac = static_cast<int64_t>(u & 0x1ffffff) << 23;
  int64_t a = static_cast<int64_t>(dist_icdf_4_3[i]) << 17;
  int64_t b = static_cast<int64_t>(dist_icdf_4_3[i + 1]) << 17;
  int64_t x = FAdd48(a, FMul48(FAdd48(b, -a), frac));
  int64_t semitones = FMul48(FAdd48(2 * x, -(1LL << 48)), amount);
  // SemitonesToRatio: pitch = s + 128 (rounded), table indices from the
  // integral part and int(fractional * 256).
  int64_t pitch = FAdd48(semitones, 128LL << 48);
  int32_t integral = static_cast<int32_t>(pitch >> 48);
  int32_t low = static_cast<int32_t>((pitch & ((1LL << 48) - 1)) >> 40);
  return Float24Mul(lut_pitch_ratio_high[integral], lut_pitch_ratio_low[low]);
}

void TGenerator::Process(bool* reset, Ramps ramps, bool* gate, int size) {
  uint64_t internal_frequency = external_frequency_
      ? external_frequency_ : InternalFrequency();

  if (*reset) {
    for (int i = 0; i < kNumTChannels; ++i) {
      slave_ramp_[i].Reset();
    }
    sequence_.Reset();
    divider_pattern_length_ = 0;
    drum_pattern_step_ = kDrumPatternSize;
    if (model_ != T_GENERATOR_MODEL_DIVIDER) {
      uint32_t v[kVectorSize];
      sequence_.NextVector(v, kVectorSize);
      ConfigureSlaveRamps(v);
    }
  }

  // The products below only change when the jitter multiplier does (at a
  // clock tick), so they're computed once per block and per tick.
  const int64_t frequency = static_cast<int64_t>(internal_frequency);
  int64_t jittery_frequency = FloatMul(frequency, jitter_multiplier_);
  int64_t difference = FloatAdd(frequency, -jittery_frequency);
  while (size--) {
    master_phase_ = FloatAdd(master_phase_, jittery_frequency);
    if (difference) {  // adding 0 is exact: skipped without jitter
      phase_difference_ = FloatAdd(phase_difference_, difference);
    }

    if (master_phase_ > kQ40One) {  // upstream: > 1.0f
      master_phase_ -= kQ40One;
      uint32_t v[kVectorSize];
      sequence_.NextVector(v, kVectorSize);

      Float24 multiplier = JitterRatio(jitter_, v[JITTER]);

      // Keep the jittery clock close to the straight one:
      // multiplier *= pd > 0 ? 1 + pd : 1 / (1 - pd).
      if (phase_difference_ > 0) {
        multiplier = Float24Mul(multiplier, Float24FromFixed(
            static_cast<uint64_t>(FloatAdd(kQ40One, phase_difference_)), 40));
      } else {
        multiplier = Float24Mul(multiplier, Float24Reciprocal(Float24FromFixed(
            static_cast<uint64_t>(FloatAdd(kQ40One, -phase_difference_)), 40)));
      }
      jitter_multiplier_ = multiplier;
      jittery_frequency = FloatMul(frequency, jitter_multiplier_);
      difference = FloatAdd(frequency, -jittery_frequency);
      ConfigureSlaveRamps(v);
    }

    const uint32_t master = master_phase_ >= kQ40One
        ? 0xffffffffu : static_cast<uint32_t>(master_phase_ >> 8);  // Q32
    *ramps.external++ = master;
    *ramps.master++ = master;
    // upstream: frequency * jitter_multiplier_, i.e. the jittery frequency
    // with the multiplier as updated at this sample's tick.
    for (int j = 0; j < kNumTChannels; ++j) {
      slave_ramp_[j].Process(jittery_frequency, ramps.slave[j], gate);
      ramps.slave[j]++;
      gate++;
    }
  }
}

}  // namespace fx
}  // namespace op1marbles
