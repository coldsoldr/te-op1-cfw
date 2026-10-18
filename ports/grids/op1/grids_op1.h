// Grids on the OP-1's pattern sequencer, for the drum instrument
// (op1/grids_op1.cc): the hook words (docs/grids-port.md §6.1,
// docs/sequencer-interface.md §12) and the entry points the image's patch
// table points them at (ports/rings/op1/patches.cc, -DOP1_GRIDS).
// Part of the Grids port; Grids (c) 2011-2012 Emilie Gillet, GPL v3.

#ifndef OP1GRIDS_OP1_GRIDS_OP1_H_
#define OP1GRIDS_OP1_GRIDS_OP1_H_

#include <stdint.h>

namespace op1grids {

// Pattern's player vtable 0x19110c4 (entries from +0x0c, 8 bytes each).
const uint32_t kKeyDownSlot = 0x019110d8;
const uint32_t kKeyUpSlot = 0x019110e0;
const uint32_t kTickSlot = 0x019110f0;
const uint32_t kUiFrameSlot = 0x019110f8;
const uint32_t kAllNotesOffSlot = 0x01911100;
const uint32_t kStockKeyDown = 0x01a08c84;
const uint32_t kStockKeyUp = 0x01a087b4;
const uint32_t kStockTick = 0x01a08ffc;
const uint32_t kStockUiFrame = 0x01a086ee;
const uint32_t kStockAllNotesOff = 0x01a08bc0;
// Pattern's controller vtable 0x191101c: key down (+0x14), shifted key down
// (+0x1c), key up (+0x24), knob (+0x2c). Stock's key entries move pattern's
// step cursor and look up pattern.svg's nodes ("h1", "h14", asserting) in
// the shown document: never call them while grids.svg is shown.
const uint32_t kCtlKeyDownSlot = 0x01911030;
const uint32_t kCtlShiftKeyDownSlot = 0x01911038;
const uint32_t kCtlKeyUpSlot = 0x01911040;
const uint32_t kCtlKnobSlot = 0x01911048;
const uint32_t kStockCtlKeyDown = 0x01a07a58;
const uint32_t kStockCtlShiftKeyDown = 0x01a07ad0;
const uint32_t kStockCtlKeyUp = 0x01a07980;
const uint32_t kStockCtlKnob = 0x01a07bd0;
// Pattern's screen vtable 0x1911104: Update (+0x1c), Init (+0x24).
const uint32_t kScreenUpdateSlot = 0x01911120;
const uint32_t kScreenInitSlot = 0x01911128;
const uint32_t kStockScreenUpdate = 0x01a0967c;
const uint32_t kStockScreenInit = 0x01a092ac;
// The sequencer chooser's index -> name call (0x1970ba0's CALL at
// 0x1970bea), so pattern shows as "multi" there too (§12: the chooser finds
// the current type by its shown name).
const uint32_t kChooserNameCall = 0x01970bea;
const uint32_t kStockSeqName = 0x01a422ec;

}  // namespace op1grids

extern "C" int32_t GridsKeyDown(void* player, int32_t key, int32_t flag);
extern "C" void GridsKeyUp(void* player, int32_t key);
extern "C" void GridsTick(void* player, int32_t t);
extern "C" void GridsUiFrame(void* player, int32_t ticks);
extern "C" void GridsAllNotesOff(void* player);
extern "C" int32_t GridsCtlKeyDown(void* controller, int32_t key);
extern "C" int32_t GridsCtlShiftKeyDown(void* controller, int32_t key);
extern "C" int32_t GridsCtlKeyUp(void* controller, int32_t key);
extern "C" void GridsCtlKnob(void* controller, int32_t k, int16_t* delta);
extern "C" void GridsScreenInit(void* self);
extern "C" void GridsScreenUpdate(void* self, int32_t ticks);
extern "C" const char* GridsSeqName(int32_t index);

#endif  // OP1GRIDS_OP1_GRIDS_OP1_H_
