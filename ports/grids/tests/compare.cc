// Bit-exact comparison: the port (op1grids::PatternGenerator) against
// upstream Grids' pattern_generator.cc compiled on the host with the shims
// in shim/ (upstream source unchanged), plus checks of the OP-1 layer
// (op1grids::DrumSequencer) against upstream driven step by step.
//
// Both generators keep their state across all scenarios (upstream's is a
// class of statics), so any divergence anywhere propagates and is caught.
//
// usage: compare [patterns]   (patterns: also print ASCII patterns)

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "grids/pattern_generator.h"
#include "avrlib/random.h"

#include "src/drum_sequencer.h"
#include "src/pattern_generator.h"

typedef grids::PatternGenerator Up;

namespace {

op1grids::PatternGenerator port;

uint32_t g_rng = 0x12345678;
uint32_t Rand() {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
uint8_t RandByte() { return static_cast<uint8_t>(Rand() >> 24); }

struct Stats {
  uint64_t calls;
  uint64_t mismatches;
  uint64_t triggers[3];
  uint64_t accents;
  uint32_t hash;
  void Mix(uint32_t v) { hash = (hash ^ v) * 16777619u; }
};

Stats stats;
int reported = 0;

void Report(const char* where, const char* what, int up, int po) {
  ++stats.mismatches;
  if (reported < 20) {
    ++reported;
    printf("MISMATCH %s: %s upstream=%d port=%d\n", where, what, up, po);
  }
}

struct Params {
  uint8_t mode, output_clock, swing, gate_mode;
  uint8_t x, y, randomness;
  uint8_t len[3];
  uint8_t density[3];
};

void Apply(const Params& p) {
  Up::set_output_mode(p.mode);
  Up::set_output_clock(p.output_clock);
  Up::set_swing(p.swing);
  Up::set_gate_mode(p.gate_mode);
  grids::PatternGeneratorSettings* s = Up::mutable_settings();
  if (p.mode == grids::OUTPUT_MODE_DRUMS) {
    s->options.drums.x = p.x;
    s->options.drums.y = p.y;
    s->options.drums.randomness = p.randomness;
  } else {
    for (int i = 0; i < 3; ++i) s->options.euclidean_length[i] = p.len[i];
  }
  for (int i = 0; i < 3; ++i) s->density[i] = p.density[i];

  port.set_output_mode(p.mode);
  port.set_output_clock(p.output_clock);
  port.set_swing(p.swing);
  port.set_gate_mode(p.gate_mode);
  op1grids::Settings* ps = port.mutable_settings();
  // Upstream overlays x/y/randomness and the lengths; in Euclidean mode the
  // drums fields hold the lengths too (it matters for swing_amount()).
  if (p.mode == grids::OUTPUT_MODE_DRUMS) {
    ps->x = p.x;
    ps->y = p.y;
    ps->randomness = p.randomness;
  } else {
    ps->x = p.len[0];
    ps->y = p.len[1];
    ps->randomness = p.len[2];
    for (int i = 0; i < 3; ++i) ps->euclidean_length[i] = p.len[i];
  }
  for (int i = 0; i < 3; ++i) ps->density[i] = p.density[i];
}

void Check(const char* where) {
  ++stats.calls;
  uint8_t us = Up::state(), ps = port.state();
  if (us != ps) Report(where, "state", us, ps);
  if (Up::step() != port.step()) Report(where, "step", Up::step(), port.step());
  if (Up::swing_amount() != port.swing_amount())
    Report(where, "swing_amount", Up::swing_amount(), port.swing_amount());
  if (Up::on_beat() != port.on_beat()) Report(where, "beat", Up::on_beat(), port.on_beat());
  if (Up::on_first_beat() != port.on_first_beat())
    Report(where, "first_beat", Up::on_first_beat(), port.on_first_beat());
  if (avrlib::Random::state() != port.rng_state())
    Report(where, "rng", avrlib::Random::state(), port.rng_state());
  for (int i = 0; i < 3; ++i) if (ps & (1 << i)) ++stats.triggers[i];
  if (ps & 0x38) ++stats.accents;
  stats.Mix(ps | (port.step() << 8) | (port.rng_state() << 16));
}

void Tick(uint8_t pulses, const char* where) {
  Up::TickClock(pulses);
  port.TickClock(pulses);
  Check(where);
}

void Reset() {
  Up::Reset();
  port.Reset();
}

// 1. Static sweeps over the map, densities and randomness.
void SweepDrums() {
  static const uint8_t xy[] = {0, 1, 31, 63, 64, 65, 96, 127, 128, 129, 160,
                               191, 192, 193, 224, 254, 255};
  static const uint8_t dens[][3] = {
    {0, 0, 0}, {255, 255, 255}, {128, 128, 128}, {64, 192, 32},
    {200, 100, 250}, {191, 63, 127}};
  static const uint8_t rnd[] = {0, 1, 64, 128, 200, 255};
  static const uint8_t pulses[] = {1, 3, 6};
  uint64_t before = stats.calls;
  for (int oc = 0; oc < 2; ++oc)
  for (int sw = 0; sw < 2; ++sw)
  for (int pi = 0; pi < 3; ++pi)
  for (unsigned xi = 0; xi < sizeof(xy); ++xi)
  for (unsigned yi = 0; yi < sizeof(xy); ++yi)
  for (unsigned di = 0; di < 6; ++di)
  for (unsigned ri = 0; ri < sizeof(rnd); ++ri) {
    Params p = {grids::OUTPUT_MODE_DRUMS, (uint8_t)oc, (uint8_t)sw, 0,
                xy[xi], xy[yi], rnd[ri], {0, 0, 0},
                {dens[di][0], dens[di][1], dens[di][2]}};
    Apply(p);
    Reset();
    int ticks = 2 * 32 * 3 / pulses[pi];  // two patterns
    for (int t = 0; t < ticks; ++t) Tick(pulses[pi], "sweep-drums");
  }
  printf("sweep-drums:     %8llu calls\n", (unsigned long long)(stats.calls - before));
}

// 2. Euclidean: every length x every density for each part position.
void SweepEuclidean() {
  uint64_t before = stats.calls;
  static const uint8_t pulses[] = {1, 3, 6};
  for (int oc = 0; oc < 2; ++oc)
  for (int pi = 0; pi < 3; ++pi)
  for (int l = 0; l < 256; l += 8)
  for (int d = 0; d < 256; d += 8) {
    Params p = {grids::OUTPUT_MODE_EUCLIDEAN, (uint8_t)oc, 0, 0, 0, 0, 0,
                {(uint8_t)l, (uint8_t)(255 - l), (uint8_t)(l ^ 0x55)},
                {(uint8_t)d, (uint8_t)(255 - d), (uint8_t)(d + 7)}};
    Apply(p);
    Reset();
    int ticks = 2 * 32 * 3 / pulses[pi];
    for (int t = 0; t < ticks; ++t) Tick(pulses[pi], "sweep-euclid");
  }
  printf("sweep-euclidean: %8llu calls\n", (unsigned long long)(stats.calls - before));
}

// 3. Random walks: knobs move, modes and options flip, resets/retriggers and
// the pulse counter / falling edge run at random, many seeds.
void RandomWalks() {
  uint64_t before = stats.calls;
  for (int seed = 0; seed < 300; ++seed) {
    uint16_t s = static_cast<uint16_t>(Rand());
    if (!s) s = 1;
    avrlib::Random::Seed(s);
    port.set_rng_state(s);
    Params p;
    memset(&p, 0, sizeof(p));
    p.mode = grids::OUTPUT_MODE_DRUMS;
    for (int t = 0; t < 4000; ++t) {
      uint32_t r = Rand();
      if ((r & 7) == 0) p.x = RandByte();
      if ((r & 0x38) == 0) p.y = RandByte();
      if ((r & 0x1c0) == 0) p.randomness = RandByte();
      if ((r & 0xe00) == 0) p.density[Rand() % 3] = RandByte();
      if ((r & 0x7000) == 0) p.len[Rand() % 3] = RandByte();
      if ((r & 0xff8000) == 0) p.mode ^= 1;
      if ((r & 0x3f000000) == 0) p.output_clock ^= 1;
      if ((Rand() & 0x3ff) == 0) p.swing ^= 1;
      if ((Rand() & 0x3ff) == 0) p.gate_mode ^= 1;
      Apply(p);
      uint32_t e = Rand();
      if ((e & 0x1ff) == 0) Reset();
      if ((e & 0x3fe00) == 0) {
        Up::Retrigger();
        port.Retrigger();
        Check("walk-retrigger");
      }
      static const uint8_t pulses[] = {1, 1, 3, 6};
      Tick(pulses[(e >> 18) & 3], "walk");
      int n = (e >> 20) & 15;
      for (int k = 0; k < n; ++k) {
        Up::IncrementPulseCounter();
        port.IncrementPulseCounter();
      }
      if (e & 0x80000000u) {
        Up::ClockFallingEdge();
        port.ClockFallingEdge();
      }
      Check("walk-after-pulses");
    }
  }
  printf("random-walks:    %8llu calls\n", (unsigned long long)(stats.calls - before));
}

// 4. The OP-1 layer: the per-step states it produces must equal upstream's
// fed the same settings with TickClock(3) (32nds) or TickClock(6) (16ths),
// and its note stream must be well formed (timing, gates, ranges).
uint64_t g_layer_steps, g_layer_events, g_layer_errors;

void LayerError(const char* what, long a, long b) {
  ++g_layer_errors;
  if (g_layer_errors <= 20) printf("LAYER ERROR: %s (%ld, %ld)\n", what, a, b);
}

void LayerRun(int seed, uint8_t resolution, uint8_t swing, uint8_t gate) {
  static op1grids::DrumSequencer seq;  // POD-like; Init() sets everything
  seq.Init(static_cast<uint16_t>(0x21 + seed));
  op1grids::DrumSeqSettings* s = seq.mutable_settings();
  s->resolution = resolution;
  s->swing = swing;
  s->gate_ticks = gate;
  s->key[3] = (seed & 1) ? 12 : op1grids::kNoKey;
  s->accent_mode = (seed & 2) ? op1grids::ACCENT_KEY : op1grids::ACCENT_VELOCITY;

  // Upstream replica, with its own RNG seed.
  avrlib::Random::Seed(static_cast<uint16_t>(0x21 + seed));
  Up::set_output_clock(false);
  Up::set_swing(false);
  Up::set_gate_mode(false);
  Up::Reset();

  bool on[24];
  int on_since[24];
  memset(on, 0, sizeof(on));
  uint32_t steps_seen = 0;
  int last_step_tick = -1000;
  uint8_t pulses = resolution == op1grids::RESOLUTION_16TH ? 6 : 3;
  const int kTicks = 384 * 16;  // 16 bars
  for (int tick = 0; tick < kTicks + 8; ++tick) {
    // Change knobs between ticks (the layer reads them at the next step).
    uint32_t r = Rand();
    if ((r & 63) == 0) s->x = RandByte();
    if ((r & (63 << 6)) == 0) s->y = RandByte();
    if ((r & (63 << 12)) == 0) s->chaos = RandByte();
    if ((r & (63 << 18)) == 0) s->density[Rand() % 3] = RandByte();
    if ((r & (63 << 24)) == 0) s->euclid_length[Rand() % 3] = RandByte();
    if ((Rand() & 2047) == 0) s->mode ^= 1;

    // The settings in force if a step happens on this tick.
    op1grids::DrumSeqSettings snap = *s;
    op1grids::NoteEvent ev[op1grids::kMaxEvents];
    uint8_t t = static_cast<uint8_t>(tick % 48);
    uint8_t n = tick < kTicks ? seq.Tick(t, ev) : seq.AllNotesOff(ev);
    if (n > op1grids::kMaxEvents) LayerError("too many events", n, 0);
    g_layer_events += n;

    if (tick < kTicks && seq.step_count() != steps_seen) {
      ++steps_seen;
      ++g_layer_steps;
      if (tick - last_step_tick < 8) LayerError("steps too close", tick, last_step_tick);
      last_step_tick = tick;
      grids::PatternGeneratorSettings* us = Up::mutable_settings();
      Up::set_output_mode(snap.mode);
      if (snap.mode == grids::OUTPUT_MODE_DRUMS) {
        us->options.drums.x = snap.x;
        us->options.drums.y = snap.y;
        us->options.drums.randomness = snap.chaos;
      } else {
        for (int i = 0; i < 3; ++i) us->options.euclidean_length[i] = snap.euclid_length[i];
      }
      for (int i = 0; i < 3; ++i) us->density[i] = snap.density[i];
      Up::TickClock(pulses);
      if (Up::state() != seq.last_state()) LayerError("state", Up::state(), seq.last_state());
      // Expected note ons from the upstream state.
      int expected = 0;
      for (int part = 0; part < 3; ++part) if (Up::state() & (1 << part)) ++expected;
      if ((Up::state() & 0x80) && snap.key[3] != op1grids::kNoKey) ++expected;
      int ons = 0;
      for (int i = 0; i < n; ++i) if (ev[i].on) ++ons;
      if (ons != expected) LayerError("note-on count", expected, ons);
      // Swing / resolution timing: the step must fall on its onset tick.
      uint16_t v = static_cast<uint16_t>((snap.swing * 43) >> 8);
      int onsets[4] = {0, (12 * (128 + v) + 64) >> 7, (24 * (128 + v) + 64) >> 7, 0};
      onsets[3] = onsets[2] + ((12 * (128 - v) + 64) >> 7);
      bool ok = false;
      for (int k = 0; k < 4; ++k) {
        if (resolution == op1grids::RESOLUTION_16TH && (k & 1)) continue;
        if (t == onsets[k]) ok = true;
      }
      if (!ok) LayerError("step off its onset tick", t, swing);
    } else {
      for (int i = 0; i < n; ++i) if (ev[i].on) LayerError("note on without a step", tick, ev[i].key);
    }

    for (int i = 0; i < n; ++i) {
      const op1grids::NoteEvent& e = ev[i];
      if (e.key > 23) { LayerError("key out of range", e.key, 0); continue; }
      if (e.on) {
        if (on[e.key]) LayerError("note on while on", tick, e.key);
        if (e.velocity < 1 || e.velocity > 127) LayerError("velocity", e.velocity, 0);
        on[e.key] = true;
        on_since[e.key] = tick;
      } else {
        if (!on[e.key]) LayerError("note off while off", tick, e.key);
        // A gate ends after gate_ticks unless retriggered or stopped first.
        if (tick < kTicks && tick - on_since[e.key] > gate) LayerError("gate too long", tick - on_since[e.key], gate);
        on[e.key] = false;
      }
    }
  }
  for (int k = 0; k < 24; ++k) if (on[k]) LayerError("hanging note", k, 0);
  // 16 bars: 32 steps per bar at 32nds, 16 at 16ths (the first step waits
  // for t == 0, which tick 0 is).
  uint32_t want = resolution == op1grids::RESOLUTION_16TH ? 16 * 16 : 16 * 32;
  if (steps_seen != want) LayerError("step count", want, steps_seen);
}

void LayerTests() {
  int runs = 0;
  static const uint8_t swings[] = {0, 1, 64, 128, 200, 255};
  static const uint8_t gates[] = {1, 6, 12, 30};
  for (int res = 0; res < 2; ++res)
  for (unsigned si = 0; si < sizeof(swings); ++si)
  for (unsigned gi = 0; gi < sizeof(gates); ++gi)
  for (int seed = 0; seed < 8; ++seed) {
    LayerRun(seed, static_cast<uint8_t>(res), swings[si], gates[gi]);
    ++runs;
  }
  printf("layer:           %8d runs, %llu steps, %llu events, %llu errors\n", runs,
         (unsigned long long)g_layer_steps, (unsigned long long)g_layer_events,
         (unsigned long long)g_layer_errors);
}

// ASCII patterns (one bar, 32 steps; '#' accent, 'x' trigger, '.' rest).
void PrintPatterns() {
  static const uint8_t pts[][2] = {{0, 0}, {128, 128}, {255, 0}, {0, 255},
                                   {255, 255}, {64, 192}, {192, 64}};
  static const uint8_t dens[] = {96, 160, 224};
  static const char* names[] = {"BD", "SD", "HH"};
  printf("\nPatterns (one bar = 32 steps of 32nd notes; | every quarter; # accent):\n");
  for (unsigned pi = 0; pi < sizeof(pts) / 2; ++pi)
  for (unsigned di = 0; di < sizeof(dens); ++di) {
    printf("\nx=%3d y=%3d density=%3d (chaos 0)\n", pts[pi][0], pts[pi][1], dens[di]);
    for (int part = 0; part < 3; ++part) {
      printf("  %s ", names[part]);
      for (int st = 0; st < 32; ++st) {
        if (st % 8 == 0) printf("|");
        uint8_t level = op1grids::PatternGenerator::ReadDrumMap(st, part, pts[pi][0], pts[pi][1]);
        uint8_t thr = static_cast<uint8_t>(~dens[di]);
        putchar(level > thr ? (level > 192 ? '#' : 'x') : '.');
      }
      printf("|\n");
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  memset(&stats, 0, sizeof(stats));
  stats.hash = 2166136261u;
  port.Init(0x21);  // upstream's statics start zeroed with rng 0x21
  SweepDrums();
  SweepEuclidean();
  RandomWalks();
  printf("generator: %llu checked calls, %llu mismatches, triggers BD %llu SD %llu HH %llu, "
         "accent/reset %llu, hash %08x\n",
         (unsigned long long)stats.calls, (unsigned long long)stats.mismatches,
         (unsigned long long)stats.triggers[0], (unsigned long long)stats.triggers[1],
         (unsigned long long)stats.triggers[2], (unsigned long long)stats.accents, stats.hash);
  LayerTests();
  bool pass = stats.mismatches == 0 && g_layer_errors == 0;
  printf("%s\n", pass ? "PASS: port bit-exact with upstream; OP-1 layer consistent"
                      : "FAIL");
  if (argc > 1 && !strcmp(argv[1], "patterns")) PrintPatterns();
  return pass ? 0 : 1;
}
