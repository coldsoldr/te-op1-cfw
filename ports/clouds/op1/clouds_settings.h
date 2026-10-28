// Clouds' settings in an effect position's 8 values (fx_params[8]: saved
// with the preset, int16 each), as an engine of the reverb category
// (../fx/op1/fx_host.h: the host owns values[4] bits 8-15, the marker).
//
// values[0..3] are the four encoders, as the OP-1's knob handler writes them
// (each within spring's range, the host's): they are the page being shown.
// Stock code never writes values[4..7] (shift + turn is a fine adjustment
// of 0..3), so they hold the rest, every word within 0..32767 (the patch
// check's range for values[4..7]):
//
//   values[4]   bits 0-1   mode, in the user's order (kModeOrder)
//               bit 2      page: 0 = page 1, 1 = page 2
//               bits 3-7   q3 bits 6-10
//   values[5]   bits 0-10  q0;  bits 11-14  q3 bits 2-5
//   values[6]   bits 0-13  q1;  bit 14      q3 bit 1
//   values[7]   bits 0-13  q2;  bit 14      q3 bit 0
//
// q0..q3 are the hidden page's four values, as offsets from kDefaults (mod
// 2^15) cut to 11, 14, 14 and 11 bits (q0's rounding as fx1's, so sim-fx's
// checksums stay). So all-zero engine bits are looping delay, page 1, the
// hidden page at the encoding base. Entering Clouds from another engine
// starts it at kEnterDefaults, both pages and the mode (EnterSettings).
// fx1's layout (values[4] bits 0-10 the first value, [5..7]
// bits 0-14 the others, bit 15 of [5]/[6] the mode and of [7] the page)
// converts with MigrateSettings.
//
// Parameter values here are 0..32767 ("v15"), page-major:
//   page 1: position, size, pitch (bipolar), density (bipolar)
//   page 2: texture, dry/wet, feedback, reverb

#ifndef OP1CLOUDS_OP1_CLOUDS_SETTINGS_H_
#define OP1CLOUDS_OP1_CLOUDS_SETTINGS_H_

#include <cstdint>

#include "fx/op1/fx_host.h"
#include "src/core/params.h"

namespace op1clouds {

using op1fx::EncoderRange;
using op1fx::EncoderToValue;
using op1fx::InitRange;
using op1fx::ValueToEncoder;
using op1fx::kValueMax;

const int32_t kNumParams = 8;
const int32_t kParamsPerPage = 4;

// Modes in the user's order (HANDOVER §3, decision 3). Only the first
// kNumPlayableModes are implemented; the clicks cycle through those.
const int32_t kNumModes = 4;
const int32_t kModeOrder[kNumModes] = { MODE_LOOPING_DELAY, MODE_GRANULAR, MODE_STRETCH,
                                        MODE_SPECTRAL };
const int32_t kNumPlayableModes = 2;

// The encoding base: the hidden page's values are stored as offsets from
// these (fx1's defaults; kept so that saved presets decode unchanged).
const int32_t kDefaults[kNumParams] = {
  0, 16384, 16384, 16384,     // position, size, pitch 0, density: no grains
  16384, 16384, 0, 0,         // texture, dry/wet 50%, feedback, reverb
};

// A fresh start (the host's orange click into Clouds): the user's settings
// of 2026-10-20 (the snapshot synth/snapshot/20261020_2024.aif, rounded to
// the clean values), v15, page-major, and the mode.
const int32_t kEnterMode = 0;   // index into kModeOrder: looping delay
const int32_t kEnterDefaults[kNumParams] = {
  13512, 32767, 16384, 32767,   // position 41%, size 100%, pitch 0, density 100%
  16384, 10239, 9216, 21504,    // texture 50%, dry/wet 31%, feedback 28%, reverb 66%
};

// Stereo spread is fixed for now (decision 4).
const int32_t kStereoSpreadQ16 = 32768;

// Pitch: -24..+24 semitones, with a dead zone around the centre.
const int32_t kPitchDeadZone = 160;

struct FxSettings {
  int32_t mode_index;   // into kModeOrder
  int32_t page;         // 0 or 1
  int32_t hidden[kParamsPerPage];   // the hidden page's values, v15
};

inline int32_t Word(const int16_t* values, int32_t k) { return static_cast<uint16_t>(values[k]); }

inline FxSettings DecodeSettings(const int16_t* values) {
  FxSettings s;
  const int32_t w4 = Word(values, 4), w5 = Word(values, 5), w6 = Word(values, 6),
                w7 = Word(values, 7);
  s.mode_index = w4 & 3;
  s.page = (w4 >> 2) & 1;
  const int32_t q3 = ((w4 >> 3) & 0x1f) << 6 | ((w5 >> 11) & 0xf) << 2 | ((w6 >> 14) & 1) << 1 |
                     ((w7 >> 14) & 1);
  const int32_t base = (1 - s.page) * kParamsPerPage;   // the hidden page
  s.hidden[0] = (((w5 & 0x7ff) << 4 | 8) + kDefaults[base]) & 0x7fff;
  s.hidden[1] = (((w6 & 0x3fff) << 1) + kDefaults[base + 1]) & 0x7fff;
  s.hidden[2] = (((w7 & 0x3fff) << 1) + kDefaults[base + 2]) & 0x7fff;
  s.hidden[3] = ((q3 << 4) + kDefaults[base + 3]) & 0x7fff;
  return s;
}

// Keeps the host's bits of values[4].
inline void EncodeSettings(const FxSettings& s, int16_t* values) {
  const int32_t base = (1 - s.page) * kParamsPerPage;
  const int32_t q0 = ((s.hidden[0] - kDefaults[base]) & 0x7fff) >> 4;
  const int32_t q1 = ((s.hidden[1] - kDefaults[base + 1]) & 0x7fff) >> 1;
  const int32_t q2 = ((s.hidden[2] - kDefaults[base + 2]) & 0x7fff) >> 1;
  const int32_t q3 = ((s.hidden[3] - kDefaults[base + 3]) & 0x7fff) >> 4;
  values[4] = static_cast<int16_t>((Word(values, 4) & ~op1fx::kEngineBits & 0x7fff) |
                                   (s.mode_index & 3) | (s.page & 1) << 2 | (q3 >> 6) << 3);
  values[5] = static_cast<int16_t>(q0 | ((q3 >> 2) & 0xf) << 11);
  values[6] = static_cast<int16_t>(q1 | ((q3 >> 1) & 1) << 14);
  values[7] = static_cast<int16_t>(q2 | (q3 & 1) << 14);
}

// fx1's layout (see above) to this one; the host then sets the marker.
inline void MigrateSettings(int16_t* values) {
  FxSettings s;
  s.mode_index = (Word(values, 5) >> 15) | (Word(values, 6) >> 15) << 1;
  s.page = Word(values, 7) >> 15;
  const int32_t base = (1 - s.page) * kParamsPerPage;
  s.hidden[0] = (((Word(values, 4) & 0x7ff) << 4 | 8) + kDefaults[base]) & 0x7fff;
  for (int32_t k = 1; k < kParamsPerPage; ++k) {
    s.hidden[k] = ((Word(values, 4 + k) & 0x7fff) + kDefaults[base + k]) & 0x7fff;
  }
  values[4] = 0;
  EncodeSettings(s, values);
}

// All eight parameter values (v15, page-major) from a position's values.
inline void ParamValues(const EncoderRange* ranges, const int16_t* values, const FxSettings& s,
                        int32_t out[kNumParams]) {
  const int32_t shown = s.page * kParamsPerPage;
  const int32_t hidden = (1 - s.page) * kParamsPerPage;
  for (int32_t k = 0; k < kParamsPerPage; ++k) {
    out[shown + k] = EncoderToValue(ranges[k], values[k]);
    out[hidden + k] = s.hidden[k];
  }
}

// v15 -> Q16 0..1 (32767 -> 65536).
inline int32_t UnitQ16(int32_t v) { return (v << 1) + (v >> 14); }

inline int32_t PitchQ16(int32_t v) {
  int32_t d = v - 16384;
  if (d > kPitchDeadZone) {
    d -= kPitchDeadZone;
  } else if (d < -kPitchDeadZone) {
    d += kPitchDeadZone;
  } else {
    d = 0;
  }
  // +-(16384 - dead zone) -> +-24 semitones, Q16 (x 96.95, as a Q10
  // multiplier: no division; |d| x kScale < 2^31).
  static constexpr int32_t kScale =
      static_cast<int32_t>((24LL << 26) / (16384 - kPitchDeadZone));
  return (d * kScale) >> 10;
}

inline void ApplyParams(const int32_t v[kNumParams], CloudsParams* p) {
  p->position = UnitQ16(v[0]);
  p->size = UnitQ16(v[1]);
  p->pitch = PitchQ16(v[2]);
  p->density = UnitQ16(v[3]);
  p->texture = UnitQ16(v[4]);
  p->dry_wet = UnitQ16(v[5]);
  p->feedback = UnitQ16(v[6]);
  p->reverb = UnitQ16(v[7]);
  p->stereo_spread = kStereoSpreadQ16;
  p->trigger = false;
}

// Click actions on a position's values (outside the audio thread).

// A fresh start (the host's orange click): kEnterMode, page 1 on the
// encoders and page 2 hidden, both at kEnterDefaults (the encoders are
// written the way SwapPage writes them).
inline void EnterSettings(const EncoderRange* ranges, int16_t* values) {
  FxSettings s = DecodeSettings(values);
  s.mode_index = kEnterMode;
  s.page = 0;
  for (int32_t k = 0; k < kParamsPerPage; ++k) {
    values[k] = ValueToEncoder(ranges[k], kEnterDefaults[k]);
    s.hidden[k] = kEnterDefaults[kParamsPerPage + k];
  }
  EncodeSettings(s, values);
}

inline void StepMode(int16_t* values, int32_t direction) {
  FxSettings s = DecodeSettings(values);
  int32_t m = s.mode_index;
  if (m >= kNumPlayableModes) m = 0;
  s.mode_index = (m + direction + kNumPlayableModes) % kNumPlayableModes;
  EncodeSettings(s, values);
}

// The shown page's encoder values go to the hidden slots and the hidden
// page's values to the encoders.
inline void SwapPage(const EncoderRange* ranges, int16_t* values) {
  FxSettings s = DecodeSettings(values);
  int32_t shown[kParamsPerPage];
  for (int32_t k = 0; k < kParamsPerPage; ++k) shown[k] = EncoderToValue(ranges[k], values[k]);
  for (int32_t k = 0; k < kParamsPerPage; ++k) {
    values[k] = ValueToEncoder(ranges[k], s.hidden[k]);
    s.hidden[k] = shown[k];
  }
  s.page ^= 1;
  EncodeSettings(s, values);
}

}  // namespace op1clouds

#endif  // OP1CLOUDS_OP1_CLOUDS_SETTINGS_H_
