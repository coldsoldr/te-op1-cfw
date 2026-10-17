// Clouds' settings in an effect position's 8 values (fx_params[8]: saved
// with the preset, int16 each, 0..32767 for phone).
//
// values[0..3] are the four encoders, as the OP-1's knob handler writes them
// (each within phone's range, op1/clouds_fx.h kPhoneRanges): they are the
// page being shown. Stock code never writes values[4..7] for phone (shift +
// turn is a fine adjustment of 0..3), so they hold the rest:
//
//   values[4]   bit 0      engine: 0 = Clouds, 1 = stock phone
//               bits 1-2   mode, in the user's order (kModeOrder)
//               bit 3      page: 0 = page 1, 1 = page 2
//               bits 4-14  the hidden page's first value (11 bits)
//   values[5..7]           the hidden page's other three values (15 bits)
//
// So 0 everywhere (every stock preset) is Clouds, looping delay, page 1, and
// the hidden page's values at their defaults: the stored values are offsets
// from kDefaults (mod 2^15).
//
// Parameter values here are 0..32767 ("v15"), page-major:
//   page 1: position, size, pitch (bipolar), density (bipolar)
//   page 2: texture, dry/wet, feedback, reverb

#ifndef OP1CLOUDS_OP1_CLOUDS_SETTINGS_H_
#define OP1CLOUDS_OP1_CLOUDS_SETTINGS_H_

#include <cstdint>

#include "src/core/params.h"

namespace op1clouds {

enum FxEngine { FX_CLOUDS = 0, FX_PHONE = 1 };

const int32_t kNumParams = 8;
const int32_t kParamsPerPage = 4;
const int32_t kValueMax = 32767;

// Modes in the user's order (HANDOVER §3, decision 3). Only the first
// kNumPlayableModes are implemented; the clicks cycle through those.
const int32_t kNumModes = 4;
const int32_t kModeOrder[kNumModes] = { MODE_LOOPING_DELAY, MODE_GRANULAR, MODE_STRETCH,
                                        MODE_SPECTRAL };
const int32_t kNumPlayableModes = 2;

// The hidden page's values are stored as offsets from these.
const int32_t kDefaults[kNumParams] = {
  0, 16384, 16384, 16384,     // position, size, pitch 0, density: no grains
  16384, 16384, 0, 0,         // texture, dry/wet 50%, feedback, reverb
};

// Stereo spread is fixed for now (decision 4).
const int32_t kStereoSpreadQ16 = 32768;

// Pitch: -24..+24 semitones, with a dead zone around the centre.
const int32_t kPitchDeadZone = 160;

// An encoder's range ({min, max} from the FX Ranges table) and the
// reciprocal that maps it onto 0..32767 without a division.
struct EncoderRange {
  int32_t min;
  int32_t max;
  uint32_t recip;   // (32767 << 15) / (max - min)
};

inline void InitRange(EncoderRange* r, int32_t min, int32_t max) {
  if (max <= min) {
    min = 0;
    max = kValueMax;
  }
  r->min = min;
  r->max = max;
  r->recip = (static_cast<uint32_t>(kValueMax) << 15) / static_cast<uint32_t>(max - min);
}

inline int32_t EncoderToValue(const EncoderRange& r, int32_t e) {
  if (e <= r.min) return 0;
  if (e >= r.max) return kValueMax;
  // (e - min) < 2^15 and recip < 2^17 (ranges are wider than 8192): the
  // product fits 32 bits.
  const int32_t v = static_cast<int32_t>((static_cast<uint32_t>(e - r.min) * r.recip + 16384) >> 15);
  return v > kValueMax ? kValueMax : v;
}

// The inverse, for a page swap (not on the audio path: it divides).
inline int16_t ValueToEncoder(const EncoderRange& r, int32_t v) {
  return static_cast<int16_t>(r.min + (v * (r.max - r.min) + kValueMax / 2) / kValueMax);
}

struct FxSettings {
  int32_t engine;
  int32_t mode_index;   // into kModeOrder
  int32_t page;         // 0 or 1
  int32_t hidden[kParamsPerPage];   // the hidden page's values, v15
};

inline FxSettings DecodeSettings(const int16_t* values) {
  FxSettings s;
  const int32_t w = static_cast<uint16_t>(values[4]) & 0x7fff;
  s.engine = w & 1;
  s.mode_index = (w >> 1) & 3;
  s.page = (w >> 3) & 1;
  const int32_t base = (1 - s.page) * kParamsPerPage;   // the hidden page
  s.hidden[0] = (((w >> 4) << 4 | 8) + kDefaults[base]) & 0x7fff;
  for (int32_t k = 1; k < kParamsPerPage; ++k) {
    s.hidden[k] = ((static_cast<uint16_t>(values[4 + k]) & 0x7fff) + kDefaults[base + k]) & 0x7fff;
  }
  return s;
}

inline void EncodeSettings(const FxSettings& s, int16_t* values) {
  const int32_t base = (1 - s.page) * kParamsPerPage;
  const int32_t first = ((s.hidden[0] - kDefaults[base]) & 0x7fff) >> 4;
  values[4] = static_cast<int16_t>((s.engine & 1) | (s.mode_index & 3) << 1 | (s.page & 1) << 3 |
                                   first << 4);
  for (int32_t k = 1; k < kParamsPerPage; ++k) {
    values[4 + k] = static_cast<int16_t>((s.hidden[k] - kDefaults[base + k]) & 0x7fff);
  }
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

inline void ToggleEngine(int16_t* values) {
  FxSettings s = DecodeSettings(values);
  s.engine ^= 1;
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
