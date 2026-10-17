// Encoder push-clicks for the Rings engine (op1/input.cc): the synth
// controller key handlers that op1/patches.cc puts in place of the stock ones.

#ifndef OP1RINGS_OP1_INPUT_H_
#define OP1RINGS_OP1_INPUT_H_

#include <cstdint>

extern "C" int32_t RingsKeyDown(void* controller, int32_t key);
extern "C" int32_t RingsShiftKeyDown(void* controller, int32_t key);
extern "C" int32_t RingsKeyUp(void* controller, int32_t key);

#ifdef OP1_CLOUDS
// The master controller's (Clouds on the master FX page).
extern "C" int32_t CloudsMasterKeyDown(void* controller, int32_t key);
extern "C" int32_t CloudsMasterShiftKeyDown(void* controller, int32_t key);
extern "C" int32_t CloudsMasterKeyUp(void* controller, int32_t key);
#endif

#endif  // OP1RINGS_OP1_INPUT_H_
