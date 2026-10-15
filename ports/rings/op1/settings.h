// Model and polyphony settings, stored in the phase engine's spare preset
// values knobs[4] and knobs[5] (docs/screen-interface.md §5: stock code never
// writes them for phase, every preset stores 0, and they're saved and
// restored with presets). Values are setting x 4096, so 0 (every existing
// preset) decodes as the modal model and polyphony "all", the engine's
// original behaviour.

#ifndef OP1RINGS_OP1_SETTINGS_H_
#define OP1RINGS_OP1_SETTINGS_H_

#include <cstdint>

namespace op1 {

const int32_t kModelKnob = 4;
const int32_t kPolyKnob = 5;
const int32_t kSettingStep = 4096;

const int32_t kNumModels = 6;   // Rings' models, as op1rings::ResonatorModel

// Polyphony states: "all" (6 voices) first so that 0 keeps the original
// behaviour; then Rings' 1, 2 and 4 voices.
enum PolyState { POLY_ALL = 0, POLY_1 = 1, POLY_2 = 2, POLY_4 = 3 };
const int32_t kNumPolyStates = 4;

// Rings voices and resonances per voice (Rings: 64 / polyphony - 4 modes;
// "all" keeps the 16 modes sized from the measured free CPU).
const int32_t kPolyVoices[kNumPolyStates] = { 6, 1, 2, 4 };
const int32_t kPolyModes[kNumPolyStates] = { 16, 60, 28, 12 };

inline int32_t DecodeSetting(int16_t k, int32_t count) {
  int32_t s = (static_cast<int32_t>(k) + kSettingStep / 2) / kSettingStep;
  if (s < 0) s = 0;
  if (s >= count) s = count - 1;
  return s;
}

inline int16_t EncodeSetting(int32_t s) { return static_cast<int16_t>(s * kSettingStep); }

// The models the engine implements, in click order (the others would play
// modal; they join the cycle once the string model exists).
const int32_t kModelCycle[] = { 0, 3 };   // modal, FM voice
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

inline int32_t ModelSetting(const int16_t* knobs) { return DecodeSetting(knobs[kModelKnob], kNumModels); }
inline PolyState PolySetting(const int16_t* knobs) {
  return static_cast<PolyState>(DecodeSetting(knobs[kPolyKnob], kNumPolyStates));
}

}  // namespace op1

#endif  // OP1RINGS_OP1_SETTINGS_H_
