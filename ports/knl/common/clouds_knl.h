// Clouds as kennel effect plugins (common/clouds_knl.cc, one source for
// two plugins, the mode fixed per build): cs_clouds (granular) and
// cs_clouds_delay (looping delay). docs/kennel-plugins.md.
//
// Build defines (plugin.json cxxflags):
//   CS_CLOUDS_MODE      MODE_GRANULAR or MODE_LOOPING_DELAY (src/core/params.h)
//   CS_FX_SYMBOL        the op1_fx_stereo_t's name (FX_clouds, FX_clouds_delay)
//   CS_SCREEN_SYMBOL    the op1_screen_t's name
//
// Knobs (page 1): blue position, green size, white pitch (centre = 0),
// orange density (centre = none). Presses: blue, green and white -> page 2
// (words 5..7), orange latches freeze (not saved). Page 2 differs by mode,
// since kennel has three page-2 words and Clouds five more settings:
//   granular:       blue texture, green dry/wet, white reverb; feedback 0
//   looping delay:  blue dry/wet, green feedback, white reverb; texture 50 %
// Each position's Clouds core (~360 KB) lives in a fixed SDRAM slot outside
// kennel's 2 MB arena (common/cs_sdram.h), shared by both Clouds plugins.

#ifndef OP1KNL_COMMON_CLOUDS_KNL_H_
#define OP1KNL_COMMON_CLOUDS_KNL_H_

#include <cstdint>

namespace csclouds {

// What the screen shows for the shown position.
struct ScreenInfo {
  int32_t page;           // 0 or 1
  int32_t mode;           // MODE_* (the SVG's mode dot)
  bool freeze;
  int32_t values[8];      // 0..32767: position, size, pitch (16384 = centre),
                          // density, texture, dry/wet, feedback, reverb
  int32_t pitch_q16;      // semitones
  int32_t load_percent;   // Clouds' share of the call period, smoothed
};

void GetScreenInfo(ScreenInfo* info);

}  // namespace csclouds

#endif  // OP1KNL_COMMON_CLOUDS_KNL_H_
