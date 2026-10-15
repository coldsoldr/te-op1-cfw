// The image's patch table (op1/patches.cc), applied to the app by
// tools/inject_app.py after checking each stock value.

#ifndef OP1RINGS_OP1_PATCHES_H_
#define OP1RINGS_OP1_PATCHES_H_

#include <cstdint>

#include "op1/op1.h"

namespace op1 {

const uint32_t kNumPatches = 7;
extern const Patch kPatches[kNumPatches];

}  // namespace op1

#endif  // OP1RINGS_OP1_PATCHES_H_
