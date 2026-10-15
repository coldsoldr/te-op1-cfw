// Rings screen for the OP-1 (op1/screen.cc): the entry points and the file
// name that op1/patches.cc puts in place of the phase engine screen's.

#ifndef OP1RINGS_OP1_SCREEN_H_
#define OP1RINGS_OP1_SCREEN_H_

#include <cstdint>

extern "C" void RingsScreenInit(void* self);
extern "C" void RingsScreenUpdate(void* self, int32_t ticks);

namespace op1 {

extern const char kRingsSvgName[];

}  // namespace op1

#endif  // OP1RINGS_OP1_SCREEN_H_
