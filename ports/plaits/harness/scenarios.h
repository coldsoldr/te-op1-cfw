// Deterministic test scenarios for the Plaits port, per engine.
//
// Every parameter is generated in integers (Q15 for harmonics, timbre, morph
// and accent; 1/256 semitone for the note), so the float builds (upstream
// and the copy) and the fixed-point port get exactly the same inputs.

#ifndef PORTS_PLAITS_HARNESS_SCENARIOS_H_
#define PORTS_PLAITS_HARNESS_SCENARIOS_H_

#include <cstdint>
#include <cstdio>

namespace harness {

// Upstream's TriggerState bits (plaits/dsp/engine/engine.h).
enum {
  kTriggerLow = 0,
  kTriggerRisingEdge = 1,
  kTriggerUnpatched = 2,
  kTriggerHigh = 4,
};

const int32_t kQ15One = 32768;

struct BlockParams {
  int trigger;
  int32_t note;        // 1/256 semitone
  int32_t harmonics;   // Q15, 0..32768
  int32_t timbre;
  int32_t morph;
  int32_t accent;
};

enum Kind {
  KIND_NOTES,       // un-triggered, a note sequence, parameters at 0.5
  KIND_HARMONICS,   // un-triggered, harmonics ramps 0 -> 1
  KIND_TIMBRE,      // un-triggered, timbre ramps 0 -> 1
  KIND_MORPH,       // un-triggered, morph ramps 0 -> 1
  KIND_TRIGGER,     // triggered hits (patched trigger), parameters per hit
  KIND_COMBO,       // un-triggered, all three moving, vibrato on the note
  KIND_LOW_TRIGGER, // triggered hits at low notes, long gaps (drums, decays)
  kNumKinds
};

static const char* const kKindNames[kNumKinds] = {
  "notes", "harm", "timbre", "morph", "trig", "combo", "lowtrig",
};

static const char* const kEngineNames[16] = {
  "va", "waveshaping", "fm", "grain", "additive", "wavetable", "chords",
  "speech", "swarm", "noise", "particle", "string", "modal", "bassdrum",
  "snare", "hihat",
};

// Triangle 0..32768..0 with a period of `period` blocks.
inline int32_t Triangle(int32_t block, int32_t period) {
  int32_t phase = block % period;
  int32_t half = period / 2;
  int64_t v = phase < half ? phase : period - phase;
  return static_cast<int32_t>(v * kQ15One / half);
}

// Linear ramp 0..32768 over `total` blocks.
inline int32_t Ramp(int32_t block, int32_t total) {
  int64_t v = static_cast<int64_t>(block) * kQ15One / (total - 1);
  return static_cast<int32_t>(v > kQ15One ? kQ15One : v);
}

// A small integer hash for per-hit parameters.
inline uint32_t Hash(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return x;
}

// block: block index; blocks_per_second: sample rate / block size (the
// scenario's time base, so the same scenario takes the same time at any
// rate); total: number of blocks.
inline void ScenarioParams(int kind, int32_t block, int32_t blocks_per_second,
                           int32_t total, BlockParams* p) {
  static const int32_t kNotes[8] = { 48, 36, 60, 55, 72, 67, 84, 96 };
  static const int32_t kLowNotes[4] = { 24, 30, 36, 43 };
  p->trigger = kTriggerUnpatched;
  p->note = 48 * 256;
  p->harmonics = p->timbre = p->morph = kQ15One / 2;
  p->accent = 26214;   // ~0.8, upstream's level when LEVEL is unpatched
  switch (kind) {
    case KIND_NOTES: {
      int32_t step = block / (blocks_per_second / 5);   // 0.2 s per note
      p->note = kNotes[step & 7] * 256;
      break;
    }
    case KIND_HARMONICS:
      p->harmonics = Ramp(block, total);
      break;
    case KIND_TIMBRE:
      p->timbre = Ramp(block, total);
      break;
    case KIND_MORPH:
      p->morph = Ramp(block, total);
      break;
    case KIND_TRIGGER:
    case KIND_LOW_TRIGGER: {
      const bool low = kind == KIND_LOW_TRIGGER;
      const int32_t hit_length = low ? blocks_per_second / 2 : blocks_per_second / 4;
      const int32_t gate_length = blocks_per_second / 10;
      const int32_t hit = block / hit_length;
      const int32_t t = block - hit * hit_length;
      const uint32_t h = Hash(static_cast<uint32_t>(hit) * 2654435761U + 12345U);
      p->trigger = (t == 0 ? kTriggerRisingEdge : 0) | (t < gate_length ? kTriggerHigh : 0);
      p->note = (low ? kLowNotes[hit & 3] : kNotes[(h >> 3) & 7]) * 256;
      p->harmonics = static_cast<int32_t>((h >> 8) & 0x7fff);
      p->timbre = static_cast<int32_t>((h >> 12) & 0x7fff);
      p->morph = static_cast<int32_t>((h >> 17) & 0x7fff);
      p->accent = (hit & 1) ? 26214 : 13107;
      if (hit == 0) {   // the first hit with every parameter at 0.5
        p->harmonics = p->timbre = p->morph = kQ15One / 2;
      }
      break;
    }
    case KIND_COMBO: {
      p->harmonics = Triangle(block, blocks_per_second * 3 / 2);
      p->timbre = Triangle(block + blocks_per_second / 3, blocks_per_second);
      p->morph = Triangle(block + blocks_per_second / 7, blocks_per_second * 2 / 3);
      // +-2 semitones of vibrato at ~5 Hz, around 52.
      int32_t v = Triangle(block, blocks_per_second / 5);
      p->note = 52 * 256 + (v - kQ15One / 2) * 4 * 256 / kQ15One;
      break;
    }
  }
}

// Scenario length in seconds.
inline float ScenarioSeconds(int kind) {
  return kind == KIND_LOW_TRIGGER ? 2.0f : 1.5f;
}

}  // namespace harness

#endif  // PORTS_PLAITS_HARNESS_SCENARIOS_H_
