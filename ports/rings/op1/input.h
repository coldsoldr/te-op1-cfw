// Encoder push-clicks for the Rings engine (op1/input.cc): the synth
// controller key handlers that op1/patches.cc puts in place of the stock ones.

#ifndef OP1RINGS_OP1_INPUT_H_
#define OP1RINGS_OP1_INPUT_H_

#include <cstdint>

extern "C" int32_t RingsKeyDown(void* controller, int32_t key);
extern "C" int32_t RingsShiftKeyDown(void* controller, int32_t key);
extern "C" int32_t RingsKeyUp(void* controller, int32_t key);

#ifdef OP1_CLOUDS
// The master controller's (the effect categories on the master FX page).
extern "C" int32_t FxMasterKeyDown(void* controller, int32_t key);
extern "C" int32_t FxMasterShiftKeyDown(void* controller, int32_t key);
extern "C" int32_t FxMasterKeyUp(void* controller, int32_t key);
// The drum controller's (the effect categories on the drum FX page).
extern "C" int32_t FxDrumKeyDown(void* controller, int32_t key);
extern "C" int32_t FxDrumShiftKeyDown(void* controller, int32_t key);
extern "C" int32_t FxDrumKeyUp(void* controller, int32_t key);
#endif

#endif  // OP1RINGS_OP1_INPUT_H_
