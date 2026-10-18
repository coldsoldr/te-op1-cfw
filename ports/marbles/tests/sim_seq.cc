// Checks of the Marbles sequencer (op1/marbles_seq.*), driven as the OP-1
// drives a sequencer: one Tick per sequencer tick (96 per beat), t = the
// tick's phase in half a beat. Host and Blackfin simulator (make sim-seq):
// prints the checks and, on Blackfin, the cycles per tick.
//
//   - every note-on is matched by a note-off, no pitch is started twice,
//     nothing sounds after AllNotesOff;
//   - without jitter, T2's notes land on the grid of the chosen division,
//     also after a rate change;
//   - keys mode plays only the latched chord; scale mode stays within the
//     root's two octaves;
//   - X deja vu locked with length 4 repeats T2's pitches every 4 notes;
//   - with full jitter the clock stays near the grid (pulled back);
//   - a key played through (shift) sounds as itself.

#include <cstdint>
#include <cstdio>

#include "op1/marbles_seq.h"

#ifdef __bfin__
#include "harness/cycles.h"
#endif

using namespace op1marbles;

namespace {

int failures;

void Check(bool ok, const char* what) {
  printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

class Sink : public NoteSink {
 public:
  void Clear() {
    for (int i = 0; i < 256; ++i) on_[i] = 0;
    ons = offs = double_ons = stray_offs = 0;
    min_pitch = 1000;
    max_pitch = -1000;
  }
  void NoteOn(int32_t pitch, int32_t velocity) override {
    (void)velocity;
    const int i = pitch + 64;
    if (on_[i]) ++double_ons;
    on_[i] = 1;
    ++ons;
    last_on = pitch;
    if (pitch < min_pitch) min_pitch = pitch;
    if (pitch > max_pitch) max_pitch = pitch;
    if (record && pitch >= 0 && pitch < 32) pool_seen |= 1u << pitch;
  }
  void NoteOff(int32_t pitch) override {
    const int i = pitch + 64;
    if (!on_[i]) ++stray_offs;
    on_[i] = 0;
    ++offs;
  }
  int Sounding() const {
    int n = 0;
    for (int i = 0; i < 256; ++i) n += on_[i];
    return n;
  }
  int ons, offs, double_ons, stray_offs, min_pitch, max_pitch, last_on;
  bool record = false;
  uint32_t pool_seen = 0;

 private:
  uint8_t on_[256];
};

MarblesSeq seq;
// Built in main: the simulator's startup code doesn't run global
// constructors (Sink has a vtable).
Sink* sink_ptr;
#define sink (*sink_ptr)
uint32_t tick;   // absolute, starts on the grid

void Run(int n, int* t2_on_grid = nullptr, int* t2_off_grid = nullptr, int32_t* t2_pitches = nullptr,
         int* num_pitches = nullptr) {
  for (int i = 0; i < n; ++i, ++tick) {
    const uint8_t before = seq.gates();
    const int ons_before = sink.ons;
    seq.Tick(static_cast<int32_t>(tick % 48), &sink);
    const bool t2_rise = (seq.gates() & 2) && !(before & 2);
    if (t2_rise && t2_on_grid) {
      if (tick % static_cast<uint32_t>(seq.division_ticks()) == 0) {
        ++*t2_on_grid;
      } else {
        ++*t2_off_grid;
      }
    }
    if (t2_rise && t2_pitches && sink.ons > ons_before && *num_pitches < 64) {
      t2_pitches[(*num_pitches)++] = sink.last_on;
    }
  }
}

}  // namespace

int main() {
  Sink the_sink;
  sink_ptr = &the_sink;
  sink.Clear();
  seq.Init(0x12345678);

  // Defaults: scale mode, root 7, 1/8.
  int on = 0, off = 0;
  Run(4000, &on, &off);
  printf("default: %d notes, T2 on grid %d, off grid %d, pitches %d..%d\n", sink.ons, on, off,
         sink.min_pitch, sink.max_pitch);
  Check(sink.ons > 100, "notes play");
  Check(off == 0 && on > 0, "T2 on the 1/8 grid");
  Check(sink.min_pitch >= 7 && sink.max_pitch <= 7 + 24, "scale mode within root + 2 octaves");
  Check(sink.double_ons == 0 && sink.stray_offs == 0, "no double note-ons or stray note-offs");
  // The history: T2 plays on every clock step, so its lane is full; T1/T3
  // marks appear only where those gates played.
  {
    int heights_ok = 1;
    for (int c = 0; c < kHistory; ++c) heights_ok &= seq.history_height(1, c) <= 255;
    printf("history: T1 %04x T2 %04x T3 %04x\n", seq.history_gates(0), seq.history_gates(1),
           seq.history_gates(2));
    Check(seq.history_gates(1) == 0xffff && heights_ok, "history: T2's lane is full");
    Check((seq.history_gates(0) | seq.history_gates(2)) != 0 && (seq.history_gates(0) & seq.history_gates(2)) == 0,
          "history: coin toss marks T1 or T3, never both");
  }

  // Rate change to 1/16, then 1/4 triplet.
  seq.mutable_settings()->rate = 7 * 65536 / kNumDivisions + 100;   // index 7 = 24 ticks
  on = off = 0;
  Run(2000, &on, &off);
  printf("1/16: division %ld ticks, T2 on grid %d, off grid %d\n", static_cast<long>(seq.division_ticks()), on, off);
  Check(seq.division_ticks() == 24 && off <= 1 && on > 50, "T2 on the 1/16 grid after a change");
  seq.mutable_settings()->rate = 4 * 65536 / kNumDivisions + 100;   // 64 ticks
  on = off = 0;
  Run(2000, &on, &off);
  Check(seq.division_ticks() == 64 && off <= 1 && on > 20, "T2 on the 1/4 triplet grid");

  // Keys mode: chord 2, 5, 9 (latched), released.
  seq.mutable_settings()->rate = 0x8000;
  seq.OnClick(CLICK_WHITE, 1);
  Check(seq.settings().pitch_source == PITCH_KEYS, "white: keys mode");
  seq.KeyDown(2, false);
  seq.KeyDown(5, false);
  seq.KeyDown(9, false);
  seq.KeyUp(2);
  seq.KeyUp(5);
  seq.KeyUp(9);
  seq.AllNotesOff(&sink);
  sink.pool_seen = 0;
  sink.record = true;
  Run(3000);
  sink.record = false;
  printf("keys: pool seen %03lx\n", static_cast<unsigned long>(sink.pool_seen));
  Check(sink.pool_seen != 0 && (sink.pool_seen & ~((1u << 2) | (1u << 5) | (1u << 9))) == 0,
        "keys mode plays only the latched chord");
  // A new chord replaces it.
  seq.KeyDown(12, false);
  seq.KeyUp(12);
  sink.pool_seen = 0;
  sink.record = true;
  Run(2000);
  sink.record = false;
  Check(sink.pool_seen == (1u << 12), "a new chord replaces the latched one");
  seq.OnClick(CLICK_WHITE, 1);

  // X deja vu locked, length 4: T2's pitches repeat every 4 notes.
  seq.OnClick(CLICK_WHITE, -1);   // X page
  Check(seq.settings().page == PAGE_X, "shift + white: X page");
  seq.OnClick(CLICK_GREEN, 1);
  seq.OnClick(CLICK_GREEN, 1);
  Check(seq.settings().x_deja_vu == DEJA_VU_LOCKED, "green twice: X deja vu locked");
  seq.mutable_settings()->length = 3 * 65536 / kMaxLength + 100;   // 4 steps
  seq.mutable_settings()->t_model = 4;   // divider: T2 alone carries the loop
  Run(2000);
  int32_t pitches[64];
  int n = 0;
  Run(3000, nullptr, nullptr, pitches, &n);
  int repeats = 0;
  for (int i = 4; i < n; ++i) repeats += pitches[i] == pitches[i - 4];
  printf("deja vu locked, length 4: %d T2 notes, %d of %d repeat 4 back:", n, repeats, n - 4);
  for (int i = 0; i < n && i < 12; ++i) printf(" %ld", static_cast<long>(pitches[i]));
  printf("\n");
  Check(n > 8 && repeats == n - 4, "locked loop of 4");

  // Full jitter: no stuck notes; T2 near the grid on average.
  seq.OnClick(CLICK_WHITE, -1);   // T page
  seq.mutable_settings()->jitter = 65535;
  seq.mutable_settings()->t_model = 0;
  seq.mutable_settings()->x_deja_vu = DEJA_VU_OFF;
  on = off = 0;
  const uint32_t start = tick;
  const int ons_start = sink.ons;
  Run(9600, &on, &off);
  const int t2_notes = on + off;
  printf("jitter: %d T2 notes in %lu ticks (straight: %lu)\n", t2_notes,
         static_cast<unsigned long>(tick - start), static_cast<unsigned long>((tick - start) / 48));
  Check(t2_notes > 160 && t2_notes < 240 && sink.ons > ons_start, "jittery clock stays near the grid");

  // Play-through key.
  seq.mutable_settings()->jitter = 0;
  seq.AllNotesOff(&sink);
  sink.pool_seen = 0;
  sink.record = true;
  seq.KeyDown(20, true);
  Run(1);
  sink.record = false;
  Check((sink.pool_seen >> 20) & 1, "shift + key sounds");
  seq.KeyUp(20);
  Run(1);

  // All notes off.
  seq.AllNotesOff(&sink);
  printf("after AllNotesOff: %d sounding; total %d ons, %d offs\n", sink.Sounding(), sink.ons, sink.offs);
  Check(sink.Sounding() == 0, "nothing sounds after AllNotesOff");
  Check(sink.double_ons == 0 && sink.stray_offs == 0, "no double note-ons or stray note-offs, overall");

#ifdef __bfin__
  // Cycles per tick, default settings with deja vu on.
  harness::EnableCycleCounter();
  seq.Init(7);
  seq.mutable_settings()->t_deja_vu = DEJA_VU_ON;
  seq.mutable_settings()->x_deja_vu = DEJA_VU_ON;
  uint64_t total = 0, worst = 0;
  const int kTicks = 9600;
  for (int i = 0; i < kTicks; ++i, ++tick) {
    const uint64_t t0 = harness::ReadCycles();
    seq.Tick(static_cast<int32_t>(tick % 48), &sink);
    const uint64_t c = harness::ReadCycles() - t0;
    total += c;
    if (c > worst) worst = c;
  }
  printf("cycles per tick: average %lu, worst %lu (192 ticks/s at 120 BPM: %.2f MHz)\n",
         static_cast<unsigned long>(total / kTicks), static_cast<unsigned long>(worst),
         static_cast<double>(total / kTicks) * 192.0 / 1e6);
#endif

  printf(failures ? "sim-seq: %d FAILED\n" : "sim-seq: all checks passed\n", failures);
  return failures ? 1 : 0;
}
