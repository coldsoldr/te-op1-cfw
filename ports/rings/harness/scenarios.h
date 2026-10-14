// Deterministic test scenarios shared by the float reference renderer and the
// fixed-point port, so both can be rendered and compared sample by sample.

#ifndef PORTS_RINGS_HARNESS_SCENARIOS_H_
#define PORTS_RINGS_HARNESS_SCENARIOS_H_

#include <cstddef>

namespace harness {

// Values match rings::ResonatorModel.
enum Model {
  MODEL_MODAL = 0,
  MODEL_SYMPATHETIC_STRING = 1,
  MODEL_STRING = 2,
  MODEL_FM_VOICE = 3,
  MODEL_SYMPATHETIC_STRING_QUANTIZED = 4,
  MODEL_STRING_AND_REVERB = 5,
};

enum Input {
  INPUT_NONE,            // silence; sound comes from the internal exciter
  INPUT_IMPULSE,         // decaying impulse on every strum (external exciter)
};

enum Sweep {
  SWEEP_NONE,
  SWEEP_STRUCTURE,
  SWEEP_BRIGHTNESS,
  SWEEP_DAMPING,
  SWEEP_POSITION,
};

struct Scenario {
  const char* name;
  Model model;
  int polyphony;
  bool internal_exciter;
  float structure;
  float brightness;
  float damping;
  float position;
  const float* notes;      // MIDI notes, strummed in turn
  int num_notes;
  float strum_period;      // seconds between strums
  Input input;
  Sweep sweep;             // swept parameter goes 0 -> 1 over the scenario
  float seconds;
};

// Controls for one block, derived from a scenario.
struct BlockControl {
  bool strum;
  float note;
  float structure;
  float brightness;
  float damping;
  float position;
};

static const float kMelody[] = { 48.0f, 55.0f, 60.0f, 67.0f, 72.0f, 36.0f, 43.0f, 84.0f };
static const float kLow[] = { 28.0f, 36.0f, 40.0f, 33.0f };

static const Scenario kScenarios[] = {
  { "modal_pluck", MODEL_MODAL, 1, true, 0.25f, 0.5f, 0.7f, 0.3f,
    kMelody, 8, 0.5f, INPUT_NONE, SWEEP_NONE, 5.0f },
  { "modal_low", MODEL_MODAL, 1, true, 0.4f, 0.7f, 0.85f, 0.6f,
    kLow, 4, 1.0f, INPUT_NONE, SWEEP_NONE, 5.0f },
  { "modal_bell", MODEL_MODAL, 1, true, 0.92f, 0.6f, 0.75f, 0.3f,
    kMelody, 8, 0.25f, INPUT_NONE, SWEEP_NONE, 5.0f },
  { "modal_bell_held", MODEL_MODAL, 1, true, 0.92f, 0.6f, 0.75f, 0.3f,
    kMelody + 2, 1, 0.25f, INPUT_NONE, SWEEP_NONE, 5.0f },
  { "modal_impulse_in", MODEL_MODAL, 1, false, 0.25f, 0.3f, 0.95f, 0.75f,
    kMelody, 8, 0.5f, INPUT_IMPULSE, SWEEP_NONE, 5.0f },
  { "modal_sweep_structure", MODEL_MODAL, 1, true, 0.0f, 0.6f, 0.75f, 0.3f,
    kMelody, 8, 0.25f, INPUT_NONE, SWEEP_STRUCTURE, 8.0f },
  { "modal_sweep_brightness", MODEL_MODAL, 1, true, 0.3f, 0.0f, 0.75f, 0.3f,
    kMelody, 8, 0.25f, INPUT_NONE, SWEEP_BRIGHTNESS, 8.0f },
  { "modal_sweep_damping", MODEL_MODAL, 1, true, 0.3f, 0.5f, 0.0f, 0.3f,
    kMelody, 8, 0.25f, INPUT_NONE, SWEEP_DAMPING, 8.0f },
  { "string_pluck", MODEL_STRING, 1, true, 0.25f, 0.5f, 0.7f, 0.3f,
    kMelody, 8, 0.5f, INPUT_NONE, SWEEP_NONE, 5.0f },
  { "string_low", MODEL_STRING, 1, true, 0.25f, 0.6f, 0.85f, 0.6f,
    kLow, 4, 1.0f, INPUT_NONE, SWEEP_NONE, 5.0f },
  { "string_sweep_structure", MODEL_STRING, 1, true, 0.0f, 0.5f, 0.75f, 0.3f,
    kMelody, 8, 0.25f, INPUT_NONE, SWEEP_STRUCTURE, 8.0f },
  { "string_sweep_position", MODEL_STRING, 1, true, 0.25f, 0.5f, 0.75f, 0.0f,
    kMelody, 8, 0.25f, INPUT_NONE, SWEEP_POSITION, 8.0f },
  { "sympathetic_pluck", MODEL_SYMPATHETIC_STRING, 1, true, 0.5f, 0.5f, 0.7f, 0.3f,
    kMelody, 8, 0.5f, INPUT_NONE, SWEEP_NONE, 5.0f },
};

static const int kNumScenarios = sizeof(kScenarios) / sizeof(kScenarios[0]);

inline const Scenario* FindScenario(const char* name) {
  for (int i = 0; i < kNumScenarios; ++i) {
    const char* a = kScenarios[i].name;
    const char* b = name;
    while (*a && *a == *b) { ++a; ++b; }
    if (*a == *b) return &kScenarios[i];
  }
  return NULL;
}

inline size_t NumBlocks(const Scenario& s, float sample_rate, size_t block) {
  return static_cast<size_t>(s.seconds * sample_rate) / block;
}

inline BlockControl GetBlockControl(
    const Scenario& s, size_t block_index, size_t num_blocks,
    float sample_rate, size_t block) {
  BlockControl c;
  size_t strum_blocks = static_cast<size_t>(s.strum_period * sample_rate) / block;
  if (strum_blocks == 0) strum_blocks = 1;
  size_t strum_index = block_index / strum_blocks;
  c.strum = (block_index % strum_blocks) == 0;
  c.note = s.notes[strum_index % s.num_notes];
  c.structure = s.structure;
  c.brightness = s.brightness;
  c.damping = s.damping;
  c.position = s.position;
  float sweep = static_cast<float>(block_index) / static_cast<float>(num_blocks);
  switch (s.sweep) {
    case SWEEP_STRUCTURE: c.structure = sweep; break;
    case SWEEP_BRIGHTNESS: c.brightness = sweep; break;
    case SWEEP_DAMPING: c.damping = sweep; break;
    case SWEEP_POSITION: c.position = sweep; break;
    default: break;
  }
  return c;
}

// Fills one block of external input. `impulse` is the decaying impulse state.
inline void FillInput(const Scenario& s, const BlockControl& c, float* impulse,
                      float* in, size_t size) {
  if (s.input == INPUT_IMPULSE && c.strum) *impulse = 1.0f;
  for (size_t i = 0; i < size; ++i) {
    in[i] = s.input == INPUT_IMPULSE ? *impulse : 0.0f;
    *impulse *= 0.99f;
  }
}

}  // namespace harness

#endif  // PORTS_RINGS_HARNESS_SCENARIOS_H_
