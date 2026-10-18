// Grids as the OP-1's drum sequencer: the controls around DrumSequencer
// (the user's decisions, docs/grids-port.md "Decisions"). Platform-free:
// the host (op1/grids_op1.cc, tests/sim_grids.cc) passes ticks, keys, knobs
// and clicks in, and plays the events that come out.
//
//   - Encoders (k 0..3): drums mode map X, map Y, chaos, swing; Euclidean
//     mode BD / SD / HH length, swing. With shift (k 4..7): BD / SD / HH
//     fill (Euclidean: hits) and accent.
//   - Blue click: drums <-> Euclidean (shift: 16ths <-> 32nds, 16ths by
//     default). Green: re-roll chaos at the next bar (shift: forget the
//     accent keys). White: assign target BD -> SD -> HH -> BD accent ->
//     SD accent -> HH accent -> off; while one is selected, the next key
//     pressed becomes its sound (pressing a part's own key for its accent
//     forgets the accent key). Shift + white: random voice on/off.
//     Orange belongs to the host (swap; shift: print).
//   - Keys: holding a part's key fills that part up while held; other keys
//     play straight through (from the next Tick, as tombola plays keys).
//   - Accents: louder on the part's key, or on its accent key once one is
//     assigned.
//   - The last bar's steps are kept as 16 key masks for "print".
// Part of the Grids port; Grids (c) 2011-2012 Emilie Gillet, GPL v3.

#ifndef OP1GRIDS_OP1_GRIDS_SEQ_H_
#define OP1GRIDS_OP1_GRIDS_SEQ_H_

#include <stdint.h>

#include "src/drum_sequencer.h"

namespace op1grids {

enum Assign {
  ASSIGN_OFF = 0, ASSIGN_BD, ASSIGN_SD, ASSIGN_HH, ASSIGN_BD_ACCENT, ASSIGN_SD_ACCENT,
  ASSIGN_HH_ACCENT, kNumAssign
};
enum GridsClick { GRIDS_BLUE = 0, GRIDS_GREEN = 1, GRIDS_WHITE = 2 };

const int kPrintSteps = 16;        // stock pattern holds 16 sixteenths
const uint8_t kRandomKey = 12;     // the random voice's key when on
const uint8_t kFillBoost = 96;     // a held part's extra fill

class NoteOut {
 public:
  virtual void NoteOn(int32_t key, int32_t velocity) = 0;
  virtual void NoteOff(int32_t key) = 0;
};

struct GridsUi {
  int32_t x, y, chaos, swing;   // Q16 (0..65535)
  int32_t fill[3];              // BD, SD, HH fill (Euclidean: hits)
  int32_t length[3];            // Euclidean lengths
  int32_t accent;               // accent loudness
  uint8_t mode;                 // OUTPUT_MODE_DRUMS / OUTPUT_MODE_EUCLIDEAN
  uint8_t resolution;           // Resolution
  uint8_t assign;               // Assign
  uint8_t random_voice;         // 0 / 1
  uint8_t key[3];               // part keys
  uint8_t accent_key[3];        // kNoKey = louder on the part's own key
};

class GridsSeq {
 public:
  void Init(uint16_t seed);

  // Keys 0..23. Returns nothing: every key is handled here (filled,
  // assigned or played through).
  void KeyDown(int32_t key);
  void KeyUp(int32_t key);

  // One sequencer tick (t = 0..47).
  void Tick(int32_t t, NoteOut* out);
  // Note-offs for everything sounding; the next bar starts at the next
  // t == 0.
  void AllNotesOff(NoteOut* out);

  void Knob(int32_t k, int32_t delta_q16);
  void OnClick(GridsClick click, int32_t direction);

  const GridsUi& ui() const { return ui_; }
  GridsUi* mutable_ui() { return &ui_; }
  uint32_t held_parts() const { return held_parts_; }
  // The step in the bar (0..15 in 16ths, 0..31 in 32nds) last played, and
  // the parts that played on it (bits 0..2, accents bits 3..5).
  int32_t step() const { return step_; }
  uint8_t last_state() const { return shown_state_; }
  // The last full bar as 16 sixteenths, keys 0..23 per step (bit k).
  const uint32_t* bar() const { return bar_; }

 private:
  void Apply();
  // Notes out, counted per key: several sources (parts, accents, keys
  // played through) can sound one key; a new hit retriggers it, and its
  // note-off goes out when the last source ends.
  void On(int32_t key, int32_t velocity, NoteOut* out);
  void Off(int32_t key, NoteOut* out);

  DrumSequencer sequencer_;
  GridsUi ui_;
  uint16_t seed_;
  uint32_t step_base_;          // step_count at the bar's start
  int32_t step_;
  uint32_t recording_[kPrintSteps];
  uint32_t bar_[kPrintSteps];

  // Keys (written on the key thread, read by Tick).
  volatile uint32_t held_parts_;     // bit p: part p's key is held
  volatile uint32_t through_on_;
  volatile uint32_t through_off_;
  uint32_t through_sounding_;
  uint8_t sources_[32];         // per key: sources sounding it
  volatile uint8_t shown_state_; // the last step's parts (0 after AllNotesOff)
};

}  // namespace op1grids

#endif  // OP1GRIDS_OP1_GRIDS_SEQ_H_
