// Clouds as an engine of the OP-1's reverb category (op1/clouds_fx.cc; the
// host, ../fx/op1/fx_host.h, owns spring's process and screen words and the
// FX page clicks): the master (stereo) and the live instrument's effect
// (mono), one CloudsCore each.

#ifndef OP1CLOUDS_OP1_CLOUDS_FX_H_
#define OP1CLOUDS_OP1_CLOUDS_FX_H_

#include <cstdint>

#include "fx/op1/fx_host.h"

namespace op1clouds {

// Its id in the reverb host (spring 0, plate 1, hall 2, clouds 3).
const int32_t kCloudsEngineId = 3;

extern const op1fx::Engine kCloudsEngine;

// The Clouds state of a position, for the screen (op1/clouds_screen.cc).
struct ScreenInfo {
  int32_t page;           // 0 or 1
  int32_t mode;           // MODE_GRANULAR .. MODE_SPECTRAL (the SVG's mode index)
  bool freeze;
  int32_t values[8];      // 0..32767: position, size, pitch (16384 = centre),
                          // density, texture, dry/wet, feedback, reverb
  int32_t pitch_q16;      // semitones
  int32_t load_percent;   // Clouds' share of the call period, smoothed
};

// A position's (op1fx::kScreen*) state from its encoders (as played) and
// stored values.
void GetScreenInfo(int32_t screen_position, const int16_t* encoders, const int16_t* values,
                   const op1fx::EncoderRange* ranges, ScreenInfo* info);

// The screen (op1/clouds_screen.cc), as the host calls it.
void ScreenInit(void* screen);
void ScreenUpdate(void* screen, int32_t screen_position, const int16_t* encoders,
                  const int16_t* values, const op1fx::EncoderRange* ranges);

}  // namespace op1clouds

#endif  // OP1CLOUDS_OP1_CLOUDS_FX_H_
