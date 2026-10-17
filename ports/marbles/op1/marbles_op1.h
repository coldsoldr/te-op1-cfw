// Marbles on the OP-1's tombola sequencer (op1/marbles_op1.cc): the hook
// words (docs/sequencer-interface.md §9) and the entry points the image's
// patch table points them at (ports/rings/op1/patches.cc, -DOP1_MARBLES).

#ifndef OP1MARBLES_OP1_MARBLES_OP1_H_
#define OP1MARBLES_OP1_MARBLES_OP1_H_

#include <stdint.h>

namespace op1marbles {

// Tombola's player vtable 0x1911e04 (entries from +0x0c, 8 bytes each).
const uint32_t kKeyDownSlot = 0x01911e18;
const uint32_t kKeyUpSlot = 0x01911e20;
const uint32_t kTickSlot = 0x01911e30;
const uint32_t kUiFrameSlot = 0x01911e38;
const uint32_t kAllNotesOffSlot = 0x01911e40;
const uint32_t kStockKeyDown = 0x01a658b8;
const uint32_t kStockKeyUp = 0x01a65854;
const uint32_t kStockTick = 0x01a659ec;
const uint32_t kStockUiFrame = 0x01a65bbc;
const uint32_t kStockAllNotesOff = 0x01a65948;
// Tombola's controller vtable 0x1911ce4: key down (+0x14), shifted key down
// (+0x1c), knob (+0x2c). The key words point at shared do-nothing
// functions: only these vtable words are patched.
const uint32_t kCtlKeyDownSlot = 0x01911cf8;
const uint32_t kCtlShiftKeyDownSlot = 0x01911d00;
const uint32_t kCtlKnobSlot = 0x01911d10;
const uint32_t kStockCtlKeyDown = 0x01966cda;
const uint32_t kStockCtlShiftKeyDown = 0x01966cd6;
const uint32_t kStockCtlKnob = 0x01a63e50;
// Tombola's screen vtable 0x1911ec4: Update (+0x1c), Init (+0x24).
const uint32_t kScreenUpdateSlot = 0x01911ee0;
const uint32_t kScreenInitSlot = 0x01911ee8;
const uint32_t kStockScreenUpdate = 0x01a65ebc;
const uint32_t kStockScreenInit = 0x01a66648;

}  // namespace op1marbles

extern "C" int32_t MarblesKeyDown(void* player, int32_t key, int32_t flag);
extern "C" void MarblesKeyUp(void* player, int32_t key);
extern "C" void MarblesTick(void* player, int32_t t);
extern "C" void MarblesUiFrame(void* player, int32_t ticks);
extern "C" void MarblesAllNotesOff(void* player);
extern "C" int32_t MarblesCtlKeyDown(void* controller, int32_t key);
extern "C" int32_t MarblesCtlShiftKeyDown(void* controller, int32_t key);
extern "C" void MarblesCtlKnob(void* controller, int32_t k, int16_t* delta);
extern "C" void MarblesScreenInit(void* self);
extern "C" void MarblesScreenUpdate(void* self, int32_t ticks);

#endif  // OP1MARBLES_OP1_MARBLES_OP1_H_
