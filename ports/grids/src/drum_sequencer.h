// Grids as an OP-1 drum sequencer: the OP-1 side of the port.
// Part of the Grids port; Grids (c) 2011-2012 Emilie Gillet, GPL v3.
//
// Turns the OP-1 sequencer clock (player Tick(t), 96 ticks per beat, t =
// tick mod 48 = position inside an eighth note; docs/sequencer-interface.md
// §4) into Grids steps, applies swing, and produces note events for drum
// keys 0..23 (the glue plays them with MakeMidiNote + VoiceCtl::NoteOn /
// NoteOff from the sequencer thread). Integer only, no division, no static
// constructors; Init() sets everything. See docs/grids-port.md §6.
//
// Timing: a Grids step is a 32nd note = 12 ticks; a pattern is 32 steps =
// one 4/4 bar = 384 ticks. Swing (0..255, mapped to upstream's 0..42 range)
// delays the second sixteenth of every eighth note as Grids' clock.Wrap()
// does (max 42/128 of a sixteenth: about triplet swing).

#ifndef PORTS_GRIDS_SRC_DRUM_SEQUENCER_H_
#define PORTS_GRIDS_SRC_DRUM_SEQUENCER_H_

#include <stdint.h>

#include "src/pattern_generator.h"

namespace op1grids {

const uint8_t kTicksPerEighth = 48;  // the host's t wraps here
const uint8_t kNumVoices = 4;        // BD, SD, HH, RND (bit 7)
const uint8_t kMaxEvents = 2 * kNumVoices;
const uint8_t kNoKey = 0xff;

enum Resolution {
  RESOLUTION_16TH = 0,  // even steps only, as Grids at 4 ppqn
  RESOLUTION_32ND = 1   // all 32 steps, as Grids' internal clock / 8, 24 ppqn
};

enum AccentMode {
  ACCENT_VELOCITY = 0,  // accent -> accent_velocity on the same key
  ACCENT_KEY = 1,       // accent -> accent_key[part] at accent_velocity
};

struct DrumSeqSettings {
  uint8_t mode;              // OUTPUT_MODE_DRUMS or OUTPUT_MODE_EUCLIDEAN
  uint8_t x, y, chaos;       // map position, randomness (drums mode)
  uint8_t density[3];        // BD, SD, HH fill
  uint8_t euclid_length[3];  // euclidean mode: (v >> 3) + 1 = 1..32 steps
  uint8_t swing;             // 0..255
  uint8_t resolution;        // Resolution
  uint8_t key[kNumVoices];         // drum key 0..23 per part, kNoKey = mute
  uint8_t accent_key[3];           // used by ACCENT_KEY
  uint8_t accent_mode;             // AccentMode
  uint8_t velocity;                // 1..127
  uint8_t accent_velocity;         // 1..127
  uint8_t gate_ticks;              // note length in ticks (>= 1)
};

struct NoteEvent {
  uint8_t key;       // 0..23
  uint8_t velocity;  // 0 for note off
  uint8_t on;        // 1 note on, 0 note off
};

class DrumSequencer {
 public:
  void Init(uint16_t seed = 0x21);

  // Rewind to the start of the bar; the next step plays at the next t == 0.
  void Start();

  // One host tick (t = 0..47). Writes up to kMaxEvents events (note offs
  // before note ons) and returns how many.
  uint8_t Tick(uint8_t t, NoteEvent* out);

  // Note offs for every sounding voice (player AllNotesOff, swap, stop).
  uint8_t AllNotesOff(NoteEvent* out);

  // Settings are read at the next step; swing and resolution are latched at
  // t == 0 so the step grid stays aligned with the host clock.
  DrumSeqSettings* mutable_settings() { return &settings_; }
  const PatternGenerator& generator() const { return generator_; }
  // A new random draw for chaos (the perturbation is drawn each bar, so it
  // changes at the next bar).
  void Reseed(uint16_t seed) { generator_.set_rng_state(seed ? seed : 1); }
  uint8_t step() const { return generator_.step(); }
  uint8_t last_state() const { return last_state_; }
  uint32_t step_count() const { return step_count_; }

 private:
  void LatchTiming();
  void ApplySettings();
  void Trigger(uint8_t voice, uint8_t key, uint8_t velocity,
               NoteEvent* out, uint8_t* n);

  PatternGenerator generator_;
  DrumSeqSettings settings_;
  uint8_t onset_[4];         // tick (0..47) of each 32nd step in an eighth
  uint8_t resolution_;       // latched
  uint8_t next_onset_;       // index into onset_ of the next step
  bool waiting_;             // after Start(): wait for t == 0
  uint8_t last_state_;
  uint32_t step_count_;      // Grids steps evaluated (for tests and a screen)
  uint8_t voice_key_[kNumVoices];
  uint8_t voice_gate_[kNumVoices];
};

}  // namespace op1grids

#endif  // PORTS_GRIDS_SRC_DRUM_SEQUENCER_H_
