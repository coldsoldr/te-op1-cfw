// Sequencer probe (op1/seq_probe.cc): wrappers around the tombola
// sequencer's player, controller and screen words (docs/sequencer-interface.md
// §9), for the device questions of its §10.

#ifndef OP1RINGS_OP1_SEQ_PROBE_H_
#define OP1RINGS_OP1_SEQ_PROBE_H_

#include <cstdint>

namespace op1seq {

// Tombola's player vtable 0x1911e04 (entries from +0x0c, 8 bytes each).
const uint32_t kKeyDownSlot = 0x01911e18;
const uint32_t kKeyUpSlot = 0x01911e20;
const uint32_t kOnLeaveSlot = 0x01911e28;
const uint32_t kTickSlot = 0x01911e30;
const uint32_t kUiFrameSlot = 0x01911e38;
const uint32_t kAllNotesOffSlot = 0x01911e40;
const uint32_t kStockKeyDown = 0x01a658b8;
const uint32_t kStockKeyUp = 0x01a65854;
const uint32_t kStockOnLeave = 0x01a65872;
const uint32_t kStockTick = 0x01a659ec;
const uint32_t kStockUiFrame = 0x01a65bbc;
const uint32_t kStockAllNotesOff = 0x01a65948;
// Tombola's controller vtable 0x1911ce4: key down (+0x14). The stock word
// points at a shared do-nothing function: only this vtable word is patched.
const uint32_t kCtlKeyDownSlot = 0x01911cf8;
const uint32_t kStockCtlKeyDown = 0x01966cda;
// Tombola's screen vtable 0x1911ec4: Update (+0x1c), Init (+0x24).
const uint32_t kScreenUpdateSlot = 0x01911ee0;
const uint32_t kScreenInitSlot = 0x01911ee8;
const uint32_t kStockScreenUpdate = 0x01a65ebc;
const uint32_t kStockScreenInit = 0x01a66648;

}  // namespace op1seq

extern "C" int32_t SeqProbeKeyDown(void* player, int32_t key, int32_t flag);
extern "C" void SeqProbeKeyUp(void* player, int32_t key);
extern "C" void SeqProbeOnLeave(void* player);
extern "C" void SeqProbeTick(void* player, int32_t t);
extern "C" void SeqProbeUiFrame(void* player, int32_t ticks);
extern "C" void SeqProbeAllNotesOff(void* player);
extern "C" int32_t SeqProbeCtlKeyDown(void* controller, int32_t key);
extern "C" void SeqProbeScreenInit(void* self);
extern "C" void SeqProbeScreenUpdate(void* self, int32_t ticks);

#endif  // OP1RINGS_OP1_SEQ_PROBE_H_
