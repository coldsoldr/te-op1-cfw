// Grids pattern generator port: see pattern_generator.h.
// Upstream grids/pattern_generator.cc (c) 2011 Emilie Gillet, GPL v3.

#include "src/pattern_generator.h"

#include "src/grids_tables.h"

namespace op1grids {

namespace {

// avrlib op.h, C versions of the AVR asm (identical results).
inline uint8_t U8Mix(uint8_t a, uint8_t b, uint8_t balance) {
  return static_cast<uint8_t>((a * (255 - balance) + b * balance) >> 8);
}

inline uint8_t U8U8MulShift8(uint8_t a, uint8_t b) {
  return static_cast<uint8_t>((a * b) >> 8);
}

}  // namespace

void PatternGenerator::Init(uint16_t seed) {
  settings_.x = settings_.y = 128;
  settings_.randomness = 0;
  for (uint8_t i = 0; i < kNumParts; ++i) {
    settings_.euclidean_length[i] = 255;
    settings_.density[i] = 128;
    part_perturbation_[i] = 0;
  }
  rng_ = seed;
  output_mode_ = OUTPUT_MODE_DRUMS;
  output_clock_ = false;
  swing_ = false;
  gate_mode_ = false;
  first_beat_ = beat_ = false;
  state_ = 0;
  pulse_duration_counter_ = 0;
  Reset();
}

/* static */
uint8_t PatternGenerator::ReadDrumMap(uint8_t step, uint8_t instrument,
                                      uint8_t x, uint8_t y) {
  uint8_t i = x >> 6;
  uint8_t j = y >> 6;
  const uint8_t* a_map = kDrumNodes[kDrumMap[i][j]];
  const uint8_t* b_map = kDrumNodes[kDrumMap[i + 1][j]];
  const uint8_t* c_map = kDrumNodes[kDrumMap[i][j + 1]];
  const uint8_t* d_map = kDrumNodes[kDrumMap[i + 1][j + 1]];
  uint8_t offset = (instrument * kStepsPerPattern) + step;
  uint8_t a = a_map[offset];
  uint8_t b = b_map[offset];
  uint8_t c = c_map[offset];
  uint8_t d = d_map[offset];
  // x << 2 and y << 2 are truncated to 8 bits, as upstream's uint8_t args.
  uint8_t xf = static_cast<uint8_t>(x << 2);
  uint8_t yf = static_cast<uint8_t>(y << 2);
  return U8Mix(U8Mix(a, b, xf), U8Mix(c, d, xf), yf);
}

void PatternGenerator::EvaluateDrums() {
  // At the beginning of a pattern, decide on perturbation levels.
  if (step_ == 0) {
    for (uint8_t i = 0; i < kNumParts; ++i) {
      uint8_t randomness = swing_ ? 0 : settings_.randomness >> 2;
      part_perturbation_[i] = U8U8MulShift8(RandomGetByte(), randomness);
    }
  }

  uint8_t instrument_mask = 1;
  uint8_t x = settings_.x;
  uint8_t y = settings_.y;
  uint8_t accent_bits = 0;
  for (uint8_t i = 0; i < kNumParts; ++i) {
    uint8_t level = ReadDrumMap(step_, i, x, y);
    if (level < 255 - part_perturbation_[i]) {
      level += part_perturbation_[i];
    } else {
      level = 255;
    }
    uint8_t threshold = ~settings_.density[i];
    if (level > threshold) {
      if (level > 192) {
        accent_bits |= instrument_mask;
      }
      state_ |= instrument_mask;
    }
    instrument_mask <<= 1;
  }
  if (output_clock_) {
    state_ |= accent_bits ? OUTPUT_BIT_COMMON : 0;
    state_ |= step_ == 0 ? OUTPUT_BIT_RESET : 0;
  } else {
    state_ |= accent_bits << 3;
  }
}

void PatternGenerator::EvaluateEuclidean() {
  // Refresh only on sixteenth notes.
  if (step_ & 1) {
    return;
  }
  uint8_t instrument_mask = 1;
  uint8_t reset_bits = 0;
  for (uint8_t i = 0; i < kNumParts; ++i) {
    uint8_t length = (settings_.euclidean_length[i] >> 3) + 1;
    uint8_t density = settings_.density[i] >> 3;
    uint16_t address = static_cast<uint16_t>((length - 1) * 32) + density;
    while (euclidean_step_[i] >= length) {
      euclidean_step_[i] -= length;
    }
    uint32_t step_mask = 1UL << euclidean_step_[i];
    uint32_t pattern_bits = kEuclidean[address];
    if (pattern_bits & step_mask) {
      state_ |= instrument_mask;
    }
    if (euclidean_step_[i] == 0) {
      reset_bits |= instrument_mask;
    }
    instrument_mask <<= 1;
  }
  if (output_clock_) {
    state_ |= reset_bits ? OUTPUT_BIT_COMMON : 0;
    state_ |= (reset_bits == 0x07) ? OUTPUT_BIT_RESET : 0;
  } else {
    state_ |= reset_bits << 3;
  }
}

void PatternGenerator::Evaluate() {
  state_ = 0;
  pulse_duration_counter_ = 0;

  RandomUpdate();
  // Highest bits: clock and random bit.
  state_ |= 0x40;
  state_ |= rng_ & 0x80;

  if (output_clock_) {
    state_ |= OUTPUT_BIT_CLOCK;
  }

  // Refresh only at step changes.
  if (pulse_ != 0) {
    return;
  }

  if (output_mode_ == OUTPUT_MODE_EUCLIDEAN) {
    EvaluateEuclidean();
  } else {
    EvaluateDrums();
  }
}

int8_t PatternGenerator::swing_amount() const {
  if (swing_ && output_mode_ == OUTPUT_MODE_DRUMS) {
    int8_t value = U8U8MulShift8(settings_.randomness, 42 + 1);
    return (!(step_ & 2)) ? value : -value;
  } else {
    return 0;
  }
}

}  // namespace op1grids
