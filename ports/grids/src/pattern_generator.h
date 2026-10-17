// Grids pattern generator, ported for the OP-1 (Blackfin BF524).
//
// A line-by-line port of upstream grids/pattern_generator.{h,cc}
// (c) 2011 Emilie Gillet, GPL v3. Changes from upstream:
// - an instance with plain members instead of a class of statics (no static
//   constructors; Init() sets everything);
// - its own 16-bit Galois LFSR (avrlib Random, same polynomial and seed
//   0x21) instead of the global one;
// - drum-map nodes are indexed through const tables, no PROGMEM/pointers;
// - no EEPROM: options are set through setters;
// - X/Y/randomness and the Euclidean lengths are separate fields (upstream
//   overlays them in a union because they share the same knobs).
// The arithmetic (U8Mix, U8U8MulShift8, the clip rule, the thresholds, the
// order of the random draws) is unchanged, so the trigger output is
// bit-exact with upstream for the same inputs (tests/compare.cc).
//
// State byte (as upstream):
// OUTPUT MODE  OUTPUT CLOCK  BIT7  BIT6  BIT5  BIT4  BIT3  BIT2  BIT1  BIT0
// DRUMS        FALSE          RND   CLK  HHAC  SDAC  BDAC    HH    SD    BD
// DRUMS        TRUE           RND   CLK   CLK   BAR   ACC    HH    SD    BD
// EUCLIDEAN    FALSE          RND   CLK  RST3  RST2  RST1  EUC3  EUC2  EUC1
// EUCLIDEAN    TRUE           RND   CLK   CLK  STEP   RST  EUC3  EUC2  EUC1

#ifndef PORTS_GRIDS_SRC_PATTERN_GENERATOR_H_
#define PORTS_GRIDS_SRC_PATTERN_GENERATOR_H_

#include <stdint.h>

namespace op1grids {

const uint8_t kNumParts = 3;
const uint8_t kPulsesPerStep = 3;  // 24 ppqn; 8 steps per quarter note.
const uint8_t kStepsPerPattern = 32;
const uint8_t kPulseDuration = 8;

enum OutputMode {
  OUTPUT_MODE_EUCLIDEAN = 0,
  OUTPUT_MODE_DRUMS = 1
};

enum OutputBits {
  OUTPUT_BIT_COMMON = 0x08,
  OUTPUT_BIT_CLOCK = 0x10,
  OUTPUT_BIT_RESET = 0x20
};

struct Settings {
  uint8_t x;
  uint8_t y;
  uint8_t randomness;
  uint8_t euclidean_length[kNumParts];
  uint8_t density[kNumParts];
};

class PatternGenerator {
 public:
  void Init(uint16_t seed = 0x21);

  void Reset() {
    step_ = 0;
    pulse_ = 0;
    euclidean_step_[0] = euclidean_step_[1] = euclidean_step_[2] = 0;
  }

  void Retrigger() { Evaluate(); }

  void TickClock(uint8_t num_pulses) {
    Evaluate();
    beat_ = (step_ & 0x7) == 0;
    first_beat_ = step_ == 0;
    pulse_ += num_pulses;
    while (pulse_ >= kPulsesPerStep) {
      pulse_ -= kPulsesPerStep;
      if (!(step_ & 1)) {
        for (uint8_t i = 0; i < kNumParts; ++i) {
          ++euclidean_step_[i];
        }
      }
      ++step_;
    }
    if (step_ >= kStepsPerPattern) {
      step_ -= kStepsPerPattern;
    }
  }

  void IncrementPulseCounter() {
    ++pulse_duration_counter_;
    if (pulse_duration_counter_ >= kPulseDuration && !gate_mode_) {
      state_ = 0;
    }
  }

  void ClockFallingEdge() {
    if (gate_mode_) {
      state_ = 0;
    }
  }

  uint8_t state() const { return state_; }
  uint8_t step() const { return step_; }
  bool on_first_beat() const { return first_beat_; }
  bool on_beat() const { return beat_; }
  int8_t swing_amount() const;

  Settings* mutable_settings() { return &settings_; }
  const Settings& settings() const { return settings_; }

  void set_output_mode(uint8_t m) { output_mode_ = m; }
  void set_output_clock(bool b) { output_clock_ = b; }
  void set_swing(bool b) { swing_ = b; }
  void set_gate_mode(bool b) { gate_mode_ = b; }
  uint8_t output_mode() const { return output_mode_; }
  bool output_clock() const { return output_clock_; }
  bool swing() const { return swing_; }

  uint16_t rng_state() const { return rng_; }
  void set_rng_state(uint16_t s) { rng_ = s; }

  // The interpolated map level (0..254) of one part at one step; also usable
  // by a screen to draw the current pattern.
  static uint8_t ReadDrumMap(uint8_t step, uint8_t instrument,
                             uint8_t x, uint8_t y);

 private:
  void Evaluate();
  void EvaluateDrums();
  void EvaluateEuclidean();

  void RandomUpdate() {
    rng_ = (rng_ >> 1) ^ (-(rng_ & 1) & 0xb400);
  }
  uint8_t RandomGetByte() {
    RandomUpdate();
    return static_cast<uint8_t>(rng_ >> 8);
  }

  Settings settings_;
  uint16_t rng_;
  uint8_t output_mode_;
  bool output_clock_;
  bool swing_;
  bool gate_mode_;

  uint8_t pulse_;
  uint8_t step_;
  uint8_t euclidean_step_[kNumParts];
  bool first_beat_;
  bool beat_;
  uint8_t state_;
  uint8_t part_perturbation_[kNumParts];
  uint8_t pulse_duration_counter_;
};

}  // namespace op1grids

#endif  // PORTS_GRIDS_SRC_PATTERN_GENERATOR_H_
