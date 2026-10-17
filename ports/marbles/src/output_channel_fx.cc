// Marbles' output channel and lag processor in fixed point. See
// output_channel_fx.h.
//
// Based on Mutable Instruments Marbles (MIT, (c) 2015 Emilie Gillet).

#include "src/output_channel_fx.h"

#include "src/distributions_fx.h"
#include "src/fx.h"
#include "src/tables_fx.h"

namespace op1marbles {
namespace fx {

static const uint32_t k06fQ32 = 2576980480u;      // 0.6f * 2^32 (exact)
static const uint32_t k0999999fQ31 = 2147481472u;  // 0.999999f * 2^31 (exact)

// Interpolate(lut_raised_cosine, phase, 256), Q30.
static inline int32_t RaisedCosine(uint32_t phase) {
  uint32_t i = phase >> 24;
  uint32_t frac = phase << 8;  // Q32
  int32_t a = static_cast<int32_t>(lut_raised_cosine[i] >> 1);
  int32_t b = static_cast<int32_t>(lut_raised_cosine[i + 1] >> 1);
  return a + MulHiS32(b - a, frac);
}

// a + (b - a) * t, t Q30 in [0, 2^30].
static inline int32_t CrossfadeQ30(int32_t a, int32_t b, uint32_t t) {
  if (t >= (1u << 30)) return b;
  return a + MulHiS32(b - a, t << 2);
}

// Per-sample code: 32-bit products only (MulHiU32/MulHiS32, error of a few
// LSB at Q24 volts, i.e. below 1 uV).
int32_t LagProcessor::Process(int32_t value_q16, int32_t smoothness, uint32_t phase) {
  uint32_t frequency = phase - previous_phase_;  // + 1.0 when negative
  previous_phase_ = phase;

  // frequency *= 0.25 * 2^(7 (1 - smoothness)), capped at 1 (Q31).
  int32_t exponent = ((1 << 18) - smoothness) * 7 >> 2;  // Q16 octaves, 0..7
  int32_t octave;
  uint32_t m = Exp2Split(exponent, &octave);  // Q30, 1..2
  uint32_t p = MulHiU32(frequency >> 2, m);   // 0.25 f m, Q30
  uint32_t f = p >= (1u << (30 - octave)) ? (1u << 31) : (p << (1 + octave));  // Q31

  if (smoothness <= 13107) {  // smoothness <= 0.05f
    // frequency += 20 (0.05 - smoothness) (1 - frequency)
    uint32_t d = k005fQ32 - (static_cast<uint32_t>(smoothness) << 14);  // Q32
    uint32_t d20 = d > 0xffffffffu / 20 ? 0xffffffffu : d * 20;
    f += MulHiU32(d20, (1u << 31) - f);
    if (f > (1u << 31)) f = 1u << 31;
  }

  int32_t value = value_q16 * 256;  // Q24
  lp_state_ += MulHiS32(value - lp_state_, f) * 2;

  // interp_amount = clamp((s - 0.6) * 5, 0, 1), Q16.
  uint32_t s32 = static_cast<uint32_t>(smoothness) << 14;  // Q32 (s < 1 here)
  int32_t interp_amount = 0;
  if (smoothness >= (1 << 18)) {
    interp_amount = 65536;
  } else if (s32 > k06fQ32) {
    uint32_t d = s32 - k06fQ32;
    interp_amount = d >= 0xffffffffu / 5 ? 65536 : static_cast<int32_t>((d * 5) >> 16);
  }
  // interp_linearity = clamp((1 - s) * 5, 0, 1), Q30.
  int32_t il = ((1 << 18) - smoothness) * 5;
  uint32_t interp_linearity = il >= (1 << 18) ? (1u << 30) : static_cast<uint32_t>(il << 12);

  int32_t warped_phase = RaisedCosine(phase);               // Q30
  int32_t linear_phase = static_cast<int32_t>(phase >> 2);  // Q30
  int32_t interp_phase = CrossfadeQ30(warped_phase, linear_phase, interp_linearity);
  int32_t interp = CrossfadeQ30(ramp_start_, value,
                                static_cast<uint32_t>(interp_phase < 0 ? 0 : interp_phase));
  ramp_value_ = interp;

  int32_t out = interp_amount >= 65536
      ? interp : lp_state_ + MulHiS32(interp - lp_state_, static_cast<uint32_t>(interp_amount) << 16);
  return out >> 8;
}

void OutputChannel::Init() {
  spread_ = kQ16One / 2;
  bias_ = kQ16One / 2;
  steps_ = kQ16One / 2;
  scale_index_ = 0;
  previous_steps_ = 0;
  previous_phase_ = 0;
  previous_voltage_ = 0;
  voltage_ = 0;
  quantized_voltage_ = 0;
  scale_offset_.scale = 10;
  scale_offset_.offset = -5;
  lag_processor_.Init();
  ScaleFx scale;
  scale.base_interval = 65536;
  scale.num_degrees = 1;
  scale.degree[0].voltage = 0;
  scale.degree[0].weight = 0;
  for (int i = 0; i < 6; ++i) {
    quantizer_[i].Init(scale);
  }
}

int32_t OutputChannel::GenerateNewVoltage(RandomSequence* random_sequence) {
  uint32_t u = random_sequence->NextValue(false, 0);
  // degenerate_amount = clamp(1.25 - 25 spread), bernoulli_amount =
  // clamp(25 spread - 23.75): exact in Q16.
  int32_t degenerate_amount = Clamp(81920 - spread_ * 25, 0, kQ16One);
  int32_t bernoulli_amount = Clamp(spread_ * 25 - 1556480, 0, kQ16One);

  int64_t value = BetaDistributionSample(u, spread_, bias_);  // Q31
  bool high = static_cast<uint64_t>(u) >= (static_cast<uint64_t>(kQ16One - bias_) << 16);
  int64_t bernoulli_value = high ? k0999999fQ31 : 0;
  int64_t bias31 = static_cast<int64_t>(bias_) << 15;
  value += ((bias31 - value) * degenerate_amount) >> 16;
  value += ((bernoulli_value - value) * bernoulli_amount) >> 16;
  // value * scale + offset, in Q16 volts.
  return static_cast<int32_t>((value * scale_offset_.scale) >> 15) + scale_offset_.offset * 65536;
}

void OutputChannel::Process(RandomSequence* random_sequence, const uint32_t* phase,
                            int32_t* output, int size, int stride) {
  int log2_size = size >= 16 ? 4 : size >= 8 ? 3 : size >= 4 ? 2 : size >= 2 ? 1 : 0;
  int32_t target = steps_ * 4;  // Q18
  int32_t increment = (target - previous_steps_) >> log2_size;
  int32_t steps = previous_steps_;
  while (size--) {
    steps += increment;
    if (*phase < previous_phase_) {
      previous_voltage_ = voltage_;
      voltage_ = GenerateNewVoltage(random_sequence);
      lag_processor_.ResetRamp();
      quantized_voltage_ = Quantize(voltage_, 2 * steps - (1 << 18));
    }
    if (steps >= (1 << 17)) {
      *output = quantized_voltage_;
    } else {
      const int32_t smoothness = (1 << 18) - 2 * steps;
      *output = lag_processor_.Process(voltage_, smoothness, *phase);
    }
    output += stride;
    previous_phase_ = *phase++;
  }
  previous_steps_ = steps;
}

}  // namespace fx
}  // namespace op1marbles
