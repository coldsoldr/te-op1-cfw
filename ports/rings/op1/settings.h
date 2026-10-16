// Engine, model and polyphony settings, stored in the phase engine's spare
// preset values knobs[6], knobs[4] and knobs[5] (docs/screen-interface.md
// §5: stock code never writes them for phase, every preset stores 0, and
// they're saved and restored with presets). Values are setting x 4096, so 0
// (every existing preset) decodes as stock phase, and for Rings as the modal
// model and polyphony "all".

#ifndef OP1RINGS_OP1_SETTINGS_H_
#define OP1RINGS_OP1_SETTINGS_H_

#include <cstdint>

namespace op1 {

const int32_t kModelKnob = 4;
const int32_t kPolyKnob = 5;
const int32_t kEngineKnob = 6;
const int32_t kSettingStep = 4096;

// The engines the phase slot hosts ("MULTI"): stock phase first, so every
// existing phase preset keeps its sound.
enum Engine { ENGINE_PHASE = 0, ENGINE_RINGS = 1 };
const int32_t kNumEngines = 2;

const int32_t kNumModels = 6;   // Rings' models, as op1rings::ResonatorModel

// Polyphony states: "all" (6 voices) first so that 0 keeps the original
// behaviour; then Rings' 1, 2 and 4 voices.
enum PolyState { POLY_ALL = 0, POLY_1 = 1, POLY_2 = 2, POLY_4 = 3 };
const int32_t kNumPolyStates = 4;

// Rings voices and resonances per voice (Rings: 64 / polyphony - 4 modes;
// "all" has 14: 16 when it was sized from the measured free CPU, cut to 14
// in multi4 because six 16-mode voices crackled when the encoders turned,
// Tests 12-14; polyphony 1, 2 and 4 are upstream's and clean).
const int32_t kPolyVoices[kNumPolyStates] = { 6, 1, 2, 4 };
const int32_t kPolyModes[kNumPolyStates] = { 14, 60, 28, 12 };
// Rings' polyphony per state, for the string models: 8 / polyphony strings
// per sympathetic voice and the chord table. "All" plays as Rings'
// polyphony 4 (2 strings per voice).
const int32_t kRingsPolyphony[kNumPolyStates] = { 4, 1, 2, 4 };

inline int32_t DecodeSetting(int16_t k, int32_t count) {
  int32_t s = (static_cast<int32_t>(k) + kSettingStep / 2) / kSettingStep;
  if (s < 0) s = 0;
  if (s >= count) s = count - 1;
  return s;
}

inline int16_t EncodeSetting(int32_t s) { return static_cast<int16_t>(s * kSettingStep); }

// The models reachable by clicks, in Rings' order: modal, sympathetic
// strings, string, FM voice, quantized sympathetic strings, string + reverb.
const int32_t kModelCycle[] = { 0, 1, 2, 3, 4, 5 };
const int32_t kModelCycleLength = sizeof(kModelCycle) / sizeof(kModelCycle[0]);

// The next (direction 1) or previous (-1) model in kModelCycle; a model
// outside the cycle steps to the first.
inline int32_t StepModel(int32_t model, int32_t direction) {
  int32_t i = 0;
  while (i < kModelCycleLength && kModelCycle[i] != model) ++i;
  if (i == kModelCycleLength) return kModelCycle[0];
  i = (i + direction + kModelCycleLength) % kModelCycleLength;
  return kModelCycle[i];
}

// Polyphony cycles all -> 1 -> 2 -> 4 -> all (and back with -1).
inline int32_t StepPoly(int32_t poly, int32_t direction) {
  return (poly + direction + kNumPolyStates) % kNumPolyStates;
}

// The next (direction 1) or previous (-1) engine.
inline int32_t StepEngine(int32_t engine, int32_t direction) {
  return (engine + direction + kNumEngines) % kNumEngines;
}

inline Engine EngineSetting(const int16_t* knobs) {
  return static_cast<Engine>(DecodeSetting(knobs[kEngineKnob], kNumEngines));
}
inline int32_t ModelSetting(const int16_t* knobs) { return DecodeSetting(knobs[kModelKnob], kNumModels); }
inline PolyState PolySetting(const int16_t* knobs) {
  return static_cast<PolyState>(DecodeSetting(knobs[kPolyKnob], kNumPolyStates));
}

}  // namespace op1

#endif  // OP1RINGS_OP1_SETTINGS_H_
