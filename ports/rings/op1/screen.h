// Rings screen for the OP-1 (op1/screen.cc): the patches that put it in
// place of the phase engine's screen, for the image header's patch table.

#ifndef OP1RINGS_OP1_SCREEN_H_
#define OP1RINGS_OP1_SCREEN_H_

#include <cstdint>

#include "op1/op1.h"

namespace op1 {

const uint32_t kNumScreenPatches = 4;
extern const Patch kScreenPatches[kNumScreenPatches];

}  // namespace op1

#endif  // OP1RINGS_OP1_SCREEN_H_
