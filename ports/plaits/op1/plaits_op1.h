// The Plaits voice for the OP-1's MULTI engine (phase = 0, Rings = 1,
// Braids = 2, Plaits = 3 in knobs[6]): the render and note-on entry points
// the MULTI dispatcher (ports/rings/op1/engine.cc) would call for a voice
// whose latched engine is Plaits. Self-contained here; not linked into the
// device image yet (docs/plaits-port.md, "OP-1 glue").
//
// Conventions as Rings' (docs/rings-op1-reference.md):
//   - render(self, out, params): 128 int16 samples at 88.2 kHz; Plaits runs
//     at 44.1 kHz in 4 blocks of 16 (64 samples), upsampled 2x;
//   - pitch: the phase increment at 0xff801950[0] (f / 88200 * 2^32),
//     latched per call;
//   - params[0..3]: the four encoders (0..32767): blue = harmonics, green =
//     timbre, white = morph, orange = LPG decay; smoothed ~50 ms;
//   - note-on(self, knobs): knobs[4] = model (0..15, setting * 2048),
//     knobs[5] = options (bit field as setting * 2048: LPG off, AUX mix),
//     knobs[6] = engine (3 = Plaits).
//   - per-voice state in SDRAM (static arrays in the image), indexed by the
//     voice object's position; nothing is stored in the 28-byte L1 object.

#ifndef OP1PLAITS_OP1_PLAITS_OP1_H_
#define OP1PLAITS_OP1_PLAITS_OP1_H_

#include <cstdint>

namespace op1plaits {

const int kModelKnob = 4;
const int kOptionsKnob = 5;
const int kPlaitsSettingStep = 2048;   // 16 models in 0..30720
const int kNumModels = 16;

// knobs[5] options (decoded value 0..15).
const int kOptionLpgOff = 1;      // the OP-1's ADSR alone shapes the level
const int kOptionAuxOut = 2;      // play AUX instead of OUT
const int kOptionMix = 4;         // play (OUT + AUX) / 2 (overrides kOptionAuxOut)

inline int DecodePlaitsSetting(int16_t k) {
  int s = (static_cast<int>(k) + kPlaitsSettingStep / 2) / kPlaitsSettingStep;
  return s < 0 ? 0 : (s > 15 ? 15 : s);
}
inline int16_t EncodePlaitsSetting(int s) { return static_cast<int16_t>(s * kPlaitsSettingStep); }

// The increment (f / 88200 * 2^32) as a note (Q16 semitones) for the
// fixed-point engines at 44.1 kHz.
int32_t IncrementToNote(uint32_t increment);

}  // namespace op1plaits

extern "C" void PlaitsRender(void* self, int16_t* out, const int16_t* params);
extern "C" void PlaitsNoteOn(void* self, const int16_t* knobs);

#endif  // OP1PLAITS_OP1_PLAITS_OP1_H_
