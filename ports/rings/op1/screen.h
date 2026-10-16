// The MULTI engine's screen (op1/screen.cc): the phase screen's Init and
// Update, which op1/patches.cc puts in its vtable, and our drawing's name.

#ifndef OP1RINGS_OP1_SCREEN_H_
#define OP1RINGS_OP1_SCREEN_H_

#include <cstdint>

extern "C" void MultiScreenInit(void* self);
extern "C" void MultiScreenUpdate(void* self, int32_t ticks);

namespace op1 {

extern const char kRingsSvgName[];

}  // namespace op1

#endif  // OP1RINGS_OP1_SCREEN_H_
