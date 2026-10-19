// Checks of the Grids drum sequencer's controls (op1/grids_seq.*), driven
// as the OP-1 drives a sequencer (96 ticks per beat, t = tick mod 48).
// Host and Blackfin simulator (make sim-grids).
//
//   - 16ths by default: steps land on the sixteenth grid; 16 per bar;
//   - every note-on is matched by a note-off; nothing sounds after
//     AllNotesOff;
//   - the transport: silent with no key held; a press starts the bar on
//     its own tick; shift while held latches; a new press takes over and
//     its release stops;
//   - assigning a part's key moves its notes to that key; an accent key
//     takes the accents; the part's own key forgets it;
//   - the recorded bar (for print) holds exactly the keys played in it.
// Part of the Grids port; Grids (c) 2011-2012 Emilie Gillet, GPL v3.

#include <cstdint>
#include <cstdio>

#include "op1/grids_seq.h"

using namespace op1grids;

namespace {

int failures;

void Check(bool ok, const char* what) {
  printf("%-56s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

class Out : public NoteOut {
 public:
  void Clear() {
    for (int i = 0; i < 32; ++i) on_[i] = count_[i] = 0;
    ons = stray = 0;
    off_grid = 0;
    accents = 0;
  }
  void NoteOn(int32_t key, int32_t velocity) override {
    if (key >= 0 && key < 32) {
      on_[key] = 1;
      ++count_[key];
      bar_mask |= 1u << key;
    }
    if (velocity > 100 && velocity != 127) ++accents;
    ++ons;
    if (check_grid && tick % 24 != 0) ++off_grid;
  }
  void NoteOff(int32_t key) override {
    if (key >= 0 && key < 32) {
      if (!on_[key]) ++stray;
      on_[key] = 0;
    }
  }
  int Sounding() const {
    int n = 0;
    for (int i = 0; i < 32; ++i) n += on_[i];
    return n;
  }
  int count(int key) const { return count_[key]; }
  int ons, stray, off_grid, accents;
  bool check_grid = false;
  uint32_t tick = 0;
  uint32_t bar_mask = 0;

 private:
  uint8_t on_[32];
  int count_[32];
};

GridsSeq seq;
Out* out_ptr;
uint32_t tick;

void Run(int n) {
  for (int i = 0; i < n; ++i, ++tick) {
    out_ptr->tick = tick;
    seq.Tick(static_cast<int32_t>(tick % 48), out_ptr);
  }
}

}  // namespace

int main() {
  Out out;   // built here: the simulator's startup runs no global constructors
  out_ptr = &out;
  out.Clear();
  seq.Init(0x21);
  seq.mutable_ui()->fill[0] = seq.mutable_ui()->fill[1] = seq.mutable_ui()->fill[2] = 0xa000;

  // Nothing plays until a key is held.
  Run(384 * 2);
  Check(out.ons == 0, "transport: silent with no key held");

  // Defaults: 16ths, keys 0 / 2 / 8; key 20 held from tick 0 (the grid's origin).
  seq.KeyDown(20);
  out.check_grid = true;
  Run(384 * 8);
  out.check_grid = false;
  printf("8 bars: %d notes, BD %d SD %d HH %d, off the 16th grid %d\n", out.ons, out.count(0), out.count(2),
         out.count(8), out.off_grid);
  Check(out.ons > 32 && out.off_grid == 0, "16ths: every note on the sixteenth grid");
  Check(out.count(0) > 0 && out.count(2) > 0 && out.count(8) > 0, "BD, SD and HH play on keys 0, 2, 8");
  Check(out.stray == 0, "no stray note-offs");

  // The transport: the key up stops it; a press restarts the bar on its
  // own tick (a step there: the bar's first); shift while held latches; a
  // new press takes over and its release stops.
  seq.KeyUp(20);
  Run(2);
  const int ons_stopped = out.ons;
  Run(384 * 2);
  Check(out.Sounding() == 0 && out.ons == ons_stopped, "transport: the key up stops it");
  tick += 7;   // off the old grid
  seq.KeyDown(20);
  const int ons_press = out.ons;
  Run(1);
  Check(out.ons > ons_press && seq.step() == 0, "transport: a press plays the bar's first step on its tick");
  Run(384 * 2);
  Check(out.count(20) == 0, "transport: the key doesn't sound itself");
  seq.set_shift_held(true);
  Run(1);
  seq.set_shift_held(false);
  seq.KeyUp(20);
  const int ons_latched = out.ons;
  Run(384 * 2);
  Check(out.ons > ons_latched + 4, "transport: shift while held latches");
  seq.KeyDown(21);
  Run(384);
  seq.KeyUp(21);
  Run(2);
  const int ons_after = out.ons;
  Run(384 * 2);
  Check(out.Sounding() == 0 && out.ons == ons_after, "transport: a new press takes over, its release stops");
  seq.KeyDown(20);   // held for the rest
  Run(384);

  // Assign: white -> BD, then key 5; then off.
  seq.OnClick(GRIDS_WHITE, 1);
  Check(seq.ui().assign == ASSIGN_BD, "white: assign BD");
  seq.KeyDown(5);
  seq.KeyUp(5);
  for (int i = 1; i < kNumAssign; ++i) seq.OnClick(GRIDS_WHITE, 1);
  Check(seq.ui().assign == ASSIGN_OFF && seq.ui().key[0] == 5, "BD now on key 5, assign off");
  const int k5 = out.count(5), k0 = out.count(0);
  Run(384 * 4);
  Check(out.count(5) > k5 + 1 && out.count(0) == k0, "BD plays key 5, not key 0");

  // Accent key for SD: white x5 = SD accent, key 3; accents move to key 3.
  for (int i = 0; i < 5; ++i) seq.OnClick(GRIDS_WHITE, 1);
  Check(seq.ui().assign == ASSIGN_SD_ACCENT, "white x5: assign SD accent");
  seq.KeyDown(3);
  seq.KeyUp(3);
  seq.OnClick(GRIDS_WHITE, 1);
  seq.OnClick(GRIDS_WHITE, 1);
  const int k3 = out.count(3);
  Run(384 * 8);
  printf("SD accents on key 3: %d\n", out.count(3) - k3);
  Check(seq.ui().accent_key[1] == 3 && out.count(3) > k3, "SD accents play the accent key");
  seq.OnClick(GRIDS_GREEN, -1);
  Check(seq.ui().accent_key[1] == kNoKey, "shift + green forgets the accent keys");

  // The recorded bar: run whole bars and compare with what played.
  seq.AllNotesOff(&out);
  Run(48 - static_cast<int>(tick % 48));   // to the next t == 0
  Run(384);                                 // a bar to start recording
  out.bar_mask = 0;
  Run(384);                                 // the bar to print...
  const uint32_t played = out.bar_mask;
  Run(24);                                  // ...latched at the next bar's first step
  uint32_t printed = 0;
  int steps_with_notes = 0;
  for (int s = 0; s < kPrintSteps; ++s) {
    printed |= seq.bar()[s];
    steps_with_notes += seq.bar()[s] != 0;
  }
  printf("bar: played keys %06lx, printed keys %06lx, %d of 16 steps with notes\n",
         static_cast<unsigned long>(played), static_cast<unsigned long>(printed), steps_with_notes);
  Check(printed == played && steps_with_notes > 0, "the recorded bar holds the keys played in it");

  seq.AllNotesOff(&out);
  printf("after AllNotesOff: %d sounding; %d note-ons in all\n", out.Sounding(), out.ons);
  Check(out.Sounding() == 0 && out.stray == 0, "nothing sounds after AllNotesOff; no stray offs");

  printf(failures ? "sim-grids: %d FAILED\n" : "sim-grids: all checks passed\n", failures);
  return failures ? 1 : 0;
}
