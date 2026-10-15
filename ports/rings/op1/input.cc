// Encoder push-clicks for the Rings engine (docs/screen-interface.md §7):
// the synth controller's key handlers (vtable 0x1911994: key down +0x14,
// shifted key down +0x1c, key up +0x24) are ours. On the synth engine page
// of a phase (= Rings) slot:
//   click blue           next model (op1/settings.h kModelCycle)
//   click green          next polyphony (all -> 1 -> 2 -> 4)
//   shift + click        the previous one
//   click white          show or hide the CPU meter (op1/cpu_meter.h)
// The value goes into the slot's knobs[4] / knobs[5] and is saved with the
// preset through Patch::SetKnobs, as the stock knob handler does for
// knobs[0..3] in the same thread. Clicks are consumed (return 1: no global
// handler) on that page; every other key, page and engine goes to the stock
// handler. Orange clicks pass through (stock ignores them).

#include "op1/input.h"

#include <cstdint>

#include "op1/cpu_meter.h"
#include "op1/op1.h"
#include "op1/settings.h"

namespace {

// Key codes of the encoder clicks (inferred, strong: §7b).
const int32_t kClickBlue = 0x3b;
const int32_t kClickGreen = 0x3c;
const int32_t kClickWhite = 0x3d;   // inferred from blue and green

// Firmware (§4, §7).
const uint32_t kStockKeyDown = 0x01a51058;
const uint32_t kStockShiftKeyDown = 0x01a50904;
const uint32_t kStockKeyUp = 0x01a5056c;
const uint32_t kCurrentSlot = 0xff8016d4;     // synth slot being edited
const uint32_t kSlotTypes = 0xff8016e4;       // engine type per slot
const uint32_t kSynthPage = 0x01b4ee88;       // 0 = engine page (T1)
const int32_t kPhaseType = 7;
const uint32_t kPatches = 0x01ddd7e8;         // Patch objects, one per slot
const uint32_t kPatchSize = 0x38;
const uint32_t kSetKnobs = 0x01a00d14;        // Patch::SetKnobs(patch, knobs)

typedef int32_t (*KeyFn)(void* controller, int32_t key);
typedef void (*SetKnobsFn)(void* patch, const int16_t* knobs);

template <typename T>
T Read(uint32_t address) {
  return *reinterpret_cast<const volatile T*>(address);
}

// The slot being edited if it's a Rings (phase) slot on the engine page,
// else -1.
int32_t RingsSlot() {
  const int32_t slot = Read<int32_t>(kCurrentSlot);
  if (slot < 0 || slot >= op1::kNumSlots) return -1;
  if (Read<int32_t>(kSynthPage) != 0) return -1;
  if (Read<int32_t>(kSlotTypes + slot * 4) != kPhaseType) return -1;
  return slot;
}

bool IsOurClick(int32_t key) {
  return key == kClickBlue || key == kClickGreen || key == kClickWhite;
}

void Step(int32_t slot, int32_t key, int32_t direction) {
  if (key == kClickWhite) {
    op1::cpu_meter_shown = !op1::cpu_meter_shown;
    return;
  }
  int16_t* knobs = reinterpret_cast<int16_t*>(op1::kKnobs + slot * 16);
  if (key == kClickBlue) {
    knobs[op1::kModelKnob] = op1::EncodeSetting(op1::StepModel(op1::ModelSetting(knobs), direction));
  } else {
    knobs[op1::kPolyKnob] = op1::EncodeSetting(op1::StepPoly(op1::PolySetting(knobs), direction));
  }
  reinterpret_cast<SetKnobsFn>(kSetKnobs)(reinterpret_cast<void*>(kPatches + slot * kPatchSize),
                                          knobs);
}

}  // namespace

// KeyDispatch passes the key as a byte; stock handlers get it as received.
extern "C" int32_t RingsKeyDown(void* controller, int32_t key) {
  const int32_t code = key & 0xff;
  if (IsOurClick(code)) {
    const int32_t slot = RingsSlot();
    if (slot >= 0) {
      Step(slot, code, 1);
      return 1;
    }
  }
  return reinterpret_cast<KeyFn>(kStockKeyDown)(controller, key);
}

extern "C" int32_t RingsShiftKeyDown(void* controller, int32_t key) {
  const int32_t code = key & 0xff;
  if (IsOurClick(code)) {
    const int32_t slot = RingsSlot();
    if (slot >= 0) {
      Step(slot, code, -1);
      return 1;
    }
  }
  return reinterpret_cast<KeyFn>(kStockShiftKeyDown)(controller, key);
}

extern "C" int32_t RingsKeyUp(void* controller, int32_t key) {
  if (IsOurClick(key & 0xff) && RingsSlot() >= 0) return 1;
  return reinterpret_cast<KeyFn>(kStockKeyUp)(controller, key);
}
