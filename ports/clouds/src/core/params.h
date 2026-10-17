// Clouds' parameters (clouds/dsp/parameters.h) in fixed point.

#ifndef OP1CLOUDS_CORE_PARAMS_H_
#define OP1CLOUDS_CORE_PARAMS_H_

#include <cstdint>

namespace op1clouds {

enum PlaybackMode {
  MODE_GRANULAR = 0,
  MODE_STRETCH = 1,
  MODE_LOOPING_DELAY = 2,
  MODE_SPECTRAL = 3,
};

// Q16 (1.0 = 65536) unless noted.
struct CloudsParams {
  int32_t position;
  int32_t size;
  int32_t pitch;           // semitones, Q16
  int32_t density;
  int32_t texture;
  int32_t dry_wet;
  int32_t stereo_spread;
  int32_t feedback;
  int32_t reverb;
  bool freeze;
  bool trigger;
};

}  // namespace op1clouds

#endif  // OP1CLOUDS_CORE_PARAMS_H_
