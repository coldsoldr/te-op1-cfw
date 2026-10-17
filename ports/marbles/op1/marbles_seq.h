// Marbles as an OP-1 sequencer (docs/sequencer-interface.md, the user's
// choices in docs/marbles-port.md §9): the port's T and X generators run
// once per sequencer tick (96 per beat), the T clock synced to the tick grid
// (TGenerator::set_synced_period). Each T gate plays a note: T1 with X1, T2
// with X2, T3 with X3, so the gates overlap into chords; Y sets the
// velocity. A note's pitch comes from:
//   - scale: the root (the last key pressed) + X in semitones (0..2 V =
//     2 octaves, quantized by Marbles' steps and scale);
//   - keys: the held keys (latched: a new chord starts when a key goes down
//     with none held), X picking one of them.
//
// Platform-free: the host (op1/marbles_op1.cc, tests/sim_seq.cc) passes the
// ticks, keys, knobs and clicks in, and receives notes through a NoteSink
// as pitches relative to key 0 (the OP-1 adds 53 + its octave).

#ifndef OP1MARBLES_OP1_MARBLES_SEQ_H_
#define OP1MARBLES_OP1_MARBLES_SEQ_H_

#include <stdint.h>

#include "src/random_fx.h"
#include "src/t_generator_fx.h"
#include "src/x_y_generator_fx.h"

namespace op1marbles {

class NoteSink {
 public:
  // pitch: semitones above key 0 (may be negative or past the keyboard).
  virtual void NoteOn(int32_t pitch, int32_t velocity) = 0;
  virtual void NoteOff(int32_t pitch) = 0;
};

enum Page { PAGE_T = 0, PAGE_X = 1 };
enum PitchSource { PITCH_SCALE = 0, PITCH_KEYS = 1 };
enum DejaVuMode { DEJA_VU_OFF = 0, DEJA_VU_ON = 1, DEJA_VU_LOCKED = 2 };
enum Click { CLICK_BLUE = 0, CLICK_GREEN = 1, CLICK_WHITE = 2 };

const int32_t kNumTModels = 7;
const int32_t kNumScales = 6;
const int32_t kNumDivisions = 10;
const int32_t kMaxLength = 16;
const int32_t kNumKeys = 24;

// The beat divisions the T rate encoder picks, in ticks (96 per beat):
// 1 bar, 1/2, 1/2 triplet, 1/4, 1/4 T, 1/8, 1/8 T, 1/16, 1/16 T, 1/32.
extern const int32_t kDivisionTicks[kNumDivisions];

struct Settings {
  int32_t page;           // Page
  int32_t pitch_source;   // PitchSource
  int32_t t_model;        // 0..6 (TGeneratorModel)
  int32_t scale;          // 0..5 (Marbles' preset scales)
  int32_t t_deja_vu;      // DejaVuMode
  int32_t x_deja_vu;      // DejaVuMode
  // Encoder values, Q16 0..65535.
  int32_t rate;           // the division (index from the top)
  int32_t t_bias;
  int32_t jitter;
  int32_t deja_vu;        // the amount, shared by T and X
  int32_t spread;
  int32_t x_bias;
  int32_t steps;
  int32_t length;         // 1..16 steps (index from the top)
};

class MarblesSeq {
 public:
  void Init(uint32_t seed);

  // Keys 0..23 (the OP-1 keyboard as the sequencer receives it). A key
  // played through (shift held) sounds as itself, from the next Tick, and
  // doesn't join the chord.
  void KeyDown(int32_t key, bool play_through);
  void KeyUp(int32_t key);

  // One sequencer tick; t = the tick's phase in half a beat (0..47), as
  // the OP-1 passes it.
  void Tick(int32_t t, NoteSink* sink);
  void AllNotesOff(NoteSink* sink);

  // Encoder k (0..3: blue, green, white, orange) on the shown page; delta
  // in Q16 of the range (the host scales the OP-1's).
  void Knob(int32_t k, int32_t delta_q16);
  // Blue: T model (T page) or scale (X page), direction +-1; green: the
  // page's deja vu off/on/locked; white: page (direction -1: pitch source).
  void OnClick(Click click, int32_t direction);

  const Settings& settings() const { return settings_; }
  Settings* mutable_settings() { return &settings_; }
  int32_t division_ticks() const { return kDivisionTicks[DivisionIndex()]; }
  int32_t length_steps() const;
  // For the screen: the gates and voltages of the last tick, and the pool.
  uint8_t gates() const { return gates_; }
  const int32_t* voltages() const { return voltages_; }
  uint32_t held_mask() const { return pool_mask_; }
  int32_t root() const { return root_; }

 private:
  int32_t DivisionIndex() const;
  int32_t Pitch(int voice, int32_t voltage) const;
  void Release(int voice, NoteSink* sink);

  Settings settings_;
  fx::RandomGenerator random_generator_;
  fx::RandomStream random_stream_;
  fx::TGenerator t_generator_;
  fx::XYGenerator xy_generator_;
  fx::GroupSettings x_, y_;
  bool reset_;

  uint32_t abs_tick_;
  bool synced_;
  int32_t period_;

  // Keys (written by KeyDown/KeyUp on the key thread, read by Tick).
  volatile uint32_t held_keys_;   // physically down
  volatile uint32_t pool_mask_;   // the latched chord
  volatile int32_t root_;

  volatile uint32_t through_on_;  // played-through keys to start / stop
  volatile uint32_t through_off_;
  uint32_t through_sounding_;

  uint8_t gates_;                 // bit i = voice i's gate at the last tick
  int32_t voltages_[fx::kNumChannels];
  int32_t sounding_[3];           // pitch per voice, or kNone
  uint8_t owners_[256];           // per pitch + 64: voices holding it (bits)
};

}  // namespace op1marbles

#endif  // OP1MARBLES_OP1_MARBLES_SEQ_H_
