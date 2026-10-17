// The image's patch table (op1/patches.cc), applied to the app by
// tools/inject_app.py after checking each stock value.

#ifndef OP1RINGS_OP1_PATCHES_H_
#define OP1RINGS_OP1_PATCHES_H_

#include <cstdint>

#include "op1/op1.h"

namespace op1 {

#if defined(OP1_FX_PROBE)
const uint32_t kNumPatches = 9;   // + the phone effect's process (op1/fx_probe.cc)
#elif defined(OP1_CLOUDS)
const uint32_t kNumPatches = 17;  // + the phone effect's process and screen (Clouds,
                                  //   ../clouds/op1/) and the master and drum pages' key handlers
#else
const uint32_t kNumPatches = 7;
#endif
extern const Patch kPatches[kNumPatches];

}  // namespace op1

#endif  // OP1RINGS_OP1_PATCHES_H_
