// Marbles' T generator (clock, rhythm models, jitter, deja vu) in fixed point
// (upstream: marbles/random/t_generator.{h,cc}).
//
// Controls: rate in Q8 semitones (re 2 Hz at range 1x), bias, jitter, deja vu,
// pulse width mean/std in Q16. Ramps are Q32 phases; the master and slave
// phases and the frequencies are Q40 internally. The internal clock frequency
// is upstream's float value exactly (stmlib's SemitonesToRatio emulated), so
// without jitter the clock edges fall on the same samples as upstream's.
//
// Decisions follow upstream exactly where its float arithmetic can be
// emulated cheaply (the random vectors, the Bernoulli and three-state
// thresholds, the drum pattern index, the Markov logit, the divider pattern
// quantizer); the clusters probability and the clock phases use plain
// fixed point (see docs/marbles-port.md for the measured agreement).
//
// Not ported: the external clock input (RampExtractor). The OP-1 has no clock
// jack; set_external_frequency() lets the host drive the master clock from
// the OP-1 tempo instead.
//
// Based on Mutable Instruments Marbles (MIT, (c) 2015 Emilie Gillet).

#ifndef OP1MARBLES_FX_T_GENERATOR_FX_H_
#define OP1MARBLES_FX_T_GENERATOR_FX_H_

#include <stdint.h>

#include "src/fx.h"
#include "src/ramps_fx.h"
#include "src/random_fx.h"

namespace op1marbles {
namespace fx {

enum TGeneratorModel {
  T_GENERATOR_MODEL_COMPLEMENTARY_BERNOULLI,
  T_GENERATOR_MODEL_CLUSTERS,
  T_GENERATOR_MODEL_DRUMS,
  T_GENERATOR_MODEL_INDEPENDENT_BERNOULLI,
  T_GENERATOR_MODEL_DIVIDER,
  T_GENERATOR_MODEL_THREE_STATES,
  T_GENERATOR_MODEL_MARKOV,
};

enum TGeneratorRange {
  T_GENERATOR_RANGE_0_25X,
  T_GENERATOR_RANGE_1X,
  T_GENERATOR_RANGE_4X,
};

const int kNumTChannels = 2;
const int kMarkovHistorySize = 16;
const int kNumDrumPatterns = 18;
const int kDrumPatternSize = 8;
const int kNumDividerPatterns = 17;

struct DividerPattern {
  Ratio ratios[kNumTChannels];
  int32_t length;
};

struct Ramps {
  uint32_t* external;
  uint32_t* master;
  uint32_t* slave[kNumTChannels];
};

// stmlib's HysteresisQuantizer2 with its float arithmetic emulated (the
// hysteresis constant is a float, given as mantissa * 2^-28).
class HysteresisQuantizer2 {
 public:
  void Init(int num_steps, int32_t hysteresis_q28, bool symmetric) {
    num_steps_ = num_steps;
    hysteresis_q28_ = hysteresis_q28;
    scale_ = symmetric ? num_steps - 1 : num_steps;
    offset_q16_ = symmetric ? 0 : -kQ16One / 2;
    quantized_value_ = 0;
  }
  // value Q16.
  int Process(int32_t value) {
    int64_t v = static_cast<int64_t>(value) * scale_ + offset_q16_;  // exact
    int64_t v28 = v * 4096;
    bool above = v > (static_cast<int64_t>(quantized_value_) << 16);
    int64_t x = RoundToFloatSigned(v28 + (above ? -hysteresis_q28_ : hysteresis_q28_));
    x = RoundToFloatSigned(x + (1LL << 27));  // + 0.5f
    int q = static_cast<int>(x >= 0 ? (x >> 28) : -((-x) >> 28));  // int(): toward zero
    if (q < 0) q = 0;
    if (q > num_steps_ - 1) q = num_steps_ - 1;
    quantized_value_ = q;
    return q;
  }
 private:
  int num_steps_;
  int32_t hysteresis_q28_;
  int scale_;
  int32_t offset_q16_;
  int quantized_value_;
};

class TGenerator {
 public:
  // one_hertz: 1 / sample rate, as upstream's float (1.0f / sr).
  void Init(RandomStream* random_stream, Float24 one_hertz);

  void Process(bool* reset, Ramps ramps, bool* gate, int size);

  inline void set_model(TGeneratorModel model) { model_ = model; }
  inline void set_range(TGeneratorRange range) { range_ = range; }
  // Clamped to +-127 semitones (stmlib's table range).
  inline void set_rate(int32_t rate_q8) {
    rate_q8_ = rate_q8 < -(127 << 8) ? -(127 << 8) : (rate_q8 > (127 << 8) ? (127 << 8) : rate_q8);
  }
  inline void set_bias(int32_t bias) { bias_ = bias; }
  inline void set_jitter(int32_t jitter) { jitter_ = jitter; }
  inline void set_deja_vu(int32_t deja_vu) { sequence_.set_deja_vu(deja_vu); }
  inline void set_length(int length) { sequence_.set_length(length); }
  inline void set_pulse_width_mean(int32_t m) { pulse_width_mean_ = m; }
  inline void set_pulse_width_std(int32_t s) { pulse_width_std_ = s; }
  // Q40 cycles per sample; 0 = use the internal rate. (OP-1 tempo sync.)
  inline void set_external_frequency(uint64_t f) { external_frequency_ = f; }

  // Synced to a host clock (an OP-1 sequencer, op1/marbles_seq.cc): with a
  // period > 0 every sample is one host tick and the master ramp completes
  // exactly every `period` samples, so its ticks land on the host's grid.
  // Jitter stretches each cycle by upstream's multiplier and the drift from
  // the straight grid is pulled back by half each cycle. 0 = free running
  // (upstream's float clock, as before). Not part of upstream.
  inline void set_synced_period(int32_t period) { synced_period_ = period; }
  // Places the synced clock on the host's grid: `phase` samples into a
  // cycle of `period` (0 = the next sample is a master tick).
  void SyncTo(int32_t phase, int32_t period);

 private:
  // pulse_width[2], u[2], p, jitter (upstream's RandomVector layout).
  enum { PW0 = 0, PW1 = 1, U0 = 2, U1 = 3, P = 4, JITTER = 5, kVectorSize = 6 };

  void ConfigureSlaveRamps(const uint32_t* v);
  int GenerateComplementaryBernoulli(const uint32_t* v);
  int GenerateIndependentBernoulli(const uint32_t* v);
  int GenerateThreeStates(const uint32_t* v);
  int GenerateDrums(const uint32_t* v);
  int GenerateMarkov(const uint32_t* v);
  void ScheduleOutputPulses(const uint32_t* v, int bitmask);
  uint32_t RandomPulseWidth(int i, uint32_t u);
  uint64_t InternalFrequency();
  void ResetSequence(bool* reset);
  void ProcessSynced(bool* reset, Ramps ramps, bool* gate, int size);

  Float24 one_hertz_;
  TGeneratorModel model_;
  TGeneratorRange range_;
  int32_t rate_q8_;
  int32_t bias_;
  int32_t jitter_;
  int32_t pulse_width_mean_;
  int32_t pulse_width_std_;
  uint64_t external_frequency_;
  int32_t cached_rate_q8_;
  TGeneratorRange cached_range_;
  uint64_t cached_frequency_;

  int64_t master_phase_;        // Q40 (a float value)
  Float24 jitter_multiplier_;
  int64_t phase_difference_;    // Q40 (a float value)

  // Synced clock.
  int32_t synced_period_;
  int32_t cycle_count_;         // samples since the last master tick
  int32_t cycle_length_;        // samples in the current cycle
  int32_t grid_offset_;         // samples the ticks are late (+) on the grid
  uint64_t synced_frequency_;   // Q40 cycles per sample of the current cycle

  int32_t divider_pattern_length_;
  int32_t streak_counter_[kMarkovHistorySize];
  int32_t markov_history_[kMarkovHistorySize];
  int32_t markov_history_ptr_;
  int drum_pattern_step_;
  int drum_pattern_index_;

  RandomSequence sequence_;
  SlaveRamp slave_ramp_[kNumTChannels];
  HysteresisQuantizer2 bias_quantizer_;

  static const DividerPattern divider_patterns[kNumDividerPatterns];
  static const DividerPattern fixed_divider_patterns[kNumDividerPatterns];
  static const uint8_t drum_patterns[kNumDrumPatterns][kDrumPatternSize];
};

}  // namespace fx
}  // namespace op1marbles

#endif  // OP1MARBLES_FX_T_GENERATOR_FX_H_
