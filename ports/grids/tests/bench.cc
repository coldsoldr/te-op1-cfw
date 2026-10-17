// Cycle benchmark of the OP-1 layer (DrumSequencer::Tick, which runs the
// Grids generator on step ticks) on the Blackfin simulator, plus an event
// checksum that must equal the host's (same code, same inputs).
//
// usage: bench [bars]   (default 64 bars at 96 ticks per beat)

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "harness/cycles.h"
#include "src/drum_sequencer.h"

namespace {

op1grids::DrumSequencer seq;
op1grids::PatternGenerator gen;

uint32_t g_rng = 0xcafef00d;
uint32_t Rand() {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

}  // namespace

int main(int argc, char** argv) {
  int bars = argc > 1 ? atoi(argv[1]) : 64;
  harness::EnableCycleCounter();
  uint32_t hash = 2166136261u;
  harness::CycleStats idle, step, all, tickclock;

  for (int config = 0; config < 4; ++config) {
    seq.Init(0x21);
    op1grids::DrumSeqSettings* s = seq.mutable_settings();
    s->mode = config == 3 ? op1grids::OUTPUT_MODE_EUCLIDEAN : op1grids::OUTPUT_MODE_DRUMS;
    s->resolution = config == 1 ? op1grids::RESOLUTION_16TH : op1grids::RESOLUTION_32ND;
    s->swing = config == 2 ? 200 : 0;
    s->chaos = 128;
    s->key[3] = 12;
    s->accent_mode = config & 1;
    s->euclid_length[0] = 7 << 3;
    s->euclid_length[1] = 11 << 3;
    s->euclid_length[2] = 15 << 3;
    for (int tick = 0; tick < bars * 384; ++tick) {
      if ((tick % 97) == 0) {
        uint32_t r = Rand();
        s->x = r;
        s->y = r >> 8;
        s->density[(r >> 16) % 3] = r >> 24;
      }
      op1grids::NoteEvent ev[op1grids::kMaxEvents];
      uint32_t before_steps = seq.step_count();
      uint64_t c0 = harness::ReadCycles();
      uint8_t n = seq.Tick(static_cast<uint8_t>(tick % 48), ev);
      uint64_t c1 = harness::ReadCycles();
      uint64_t c = c1 - c0;
      all.Add(c);
      if (seq.step_count() != before_steps) step.Add(c); else idle.Add(c);
      for (int i = 0; i < n; ++i) {
        hash = (hash ^ (ev[i].key | (ev[i].velocity << 8) | (ev[i].on << 16) |
                        (static_cast<uint32_t>(tick) << 17))) * 16777619u;
      }
    }
  }

  // The bare generator: TickClock(3) at the worst settings (drums, step 0
  // perturbation draws included).
  gen.Init(0x21);
  gen.mutable_settings()->randomness = 255;
  for (int i = 0; i < 32 * 64; ++i) {
    uint64_t c0 = harness::ReadCycles();
    gen.TickClock(3);
    uint64_t c1 = harness::ReadCycles();
    tickclock.Add(c1 - c0);
    hash = (hash ^ gen.state()) * 16777619u;
  }

  printf("checksum %08x\n", static_cast<unsigned>(hash));
  printf("sizeof(DrumSequencer) %u bytes\n",
         static_cast<unsigned>(sizeof(op1grids::DrumSequencer)));
#ifdef __bfin__
  struct Row { const char* name; const harness::CycleStats* s; };
  Row rows[] = {{"Tick (all)", &all}, {"Tick (no step)", &idle},
                {"Tick (step)", &step}, {"TickClock(3)", &tickclock}};
  for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
    const harness::CycleStats* st = rows[i].s;
    printf("%-16s n %7lu  cycles avg %6.1f min %5lu max %5lu\n", rows[i].name,
           static_cast<unsigned long>(st->n),
           st->n ? static_cast<double>(st->total) / st->n : 0.0,
           static_cast<unsigned long>(st->min), static_cast<unsigned long>(st->max));
  }
  // At 120 BPM: 192 ticks/s. Worst case: every tick a step's cost.
  double avg = static_cast<double>(all.total) / all.n;
  printf("at 120 BPM (192 ticks/s): avg %.4f MHz, worst-tick-every-tick %.4f MHz\n",
         avg * 192 / 1e6, static_cast<double>(step.max) * 192 / 1e6);
  printf("at 300 BPM (480 ticks/s): avg %.4f MHz\n", avg * 480 / 1e6);
#endif
  return 0;
}
