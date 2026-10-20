// Encoder push-clicks for the MULTI engine (docs/screen-interface.md §7):
// the synth controller's key handlers (vtable 0x1911994: key down +0x14,
// shifted key down +0x1c, key up +0x24) are ours. On the synth engine page
// of a phase (= MULTI) slot:
//   click orange         next engine: stock phase <-> Rings (knobs[6])
// and while the slot plays Rings:
//   click blue           next model (op1/settings.h kModelCycle)
//   click green          next polyphony (all -> 1 -> 2 -> 4)
//   shift + click        the previous one
// (the white click is free: stock ignores it)
// The value goes into the slot's knobs[4..6] and is saved with the preset
// through Patch::SetKnobs, as the stock knob handler does for knobs[0..3]
// in the same thread. Our clicks are consumed (return 1: no global
// handler); every other key, page and engine goes to the stock handler
// (stock phase ignores encoder clicks).
//
// In the Clouds image (-DOP1_CLOUDS) the same handlers take the clicks on
// the synth FX page of a slot whose effect is a category with engines
// (../fx/op1/fx_host.cc, op1fx::SynthFxClick: orange = next engine, the
// others the engine's, e.g. Clouds' blue = next mode, green = freeze,
// white = page); and the master and drum controllers' handlers (vtables
// 0x1910aac, 0x190fa64, same offsets) take them on their FX pages. A
// release is swallowed when its press was consumed.

#include "op1/input.h"

#include <cstdint>

#ifdef OP1_CLOUDS
#include "fx/op1/fx_host.h"
#endif
#ifdef OP1_CPU_PROBE
#include "op1/cpu_probe.h"
#endif
#include "op1/op1.h"
#include "op1/settings.h"

namespace {

// Key codes of the encoder clicks (inferred, strong: §7b).
const int32_t kClickBlue = 0x3b;
const int32_t kClickGreen = 0x3c;
const int32_t kClickWhite = 0x3d;   // inferred from blue and green
const int32_t kClickOrange = 0x3e;  // inferred likewise

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
#ifdef OP1_CLOUDS
const uint32_t kStockMasterKeyDown = 0x019db7c4;
const uint32_t kStockMasterShiftKeyDown = 0x019d987e;
const uint32_t kStockMasterKeyUp = 0x019db708;
const uint32_t kStockDrumKeyDown = 0x01978008;
const uint32_t kStockDrumShiftKeyDown = 0x019777dc;
const uint32_t kStockDrumKeyUp = 0x019773e4;
#endif

typedef int32_t (*KeyFn)(void* controller, int32_t key);
typedef void (*SetKnobsFn)(void* patch, const int16_t* knobs);

template <typename T>
T Read(uint32_t address) {
  return *reinterpret_cast<const volatile T*>(address);
}

// The slot being edited if it's a MULTI (phase) slot on the engine page,
// else -1.
int32_t MultiSlot() {
  const int32_t slot = Read<int32_t>(kCurrentSlot);
  if (slot < 0 || slot >= op1::kNumSlots) return -1;
  if (Read<int32_t>(kSynthPage) != 0) return -1;
  if (Read<int32_t>(kSlotTypes + slot * 4) != kPhaseType) return -1;
  return slot;
}

int16_t* SlotKnobs(int32_t slot) {
  return reinterpret_cast<int16_t*>(op1::kKnobs + slot * 16);
}

// Whether a click on this slot is ours: orange always (the engine), blue
// and green while the slot plays Rings.
bool IsOurClick(int32_t slot, int32_t key) {
  if (key == kClickOrange) return true;
#ifdef OP1_CPU_PROBE
  if (key == kClickWhite) return op1::EngineSetting(SlotKnobs(slot)) == op1::ENGINE_RINGS;
#endif
  if (key != kClickBlue && key != kClickGreen) return false;
  return op1::EngineSetting(SlotKnobs(slot)) == op1::ENGINE_RINGS;
}

void Step(int32_t slot, int32_t key, int32_t direction) {
  int16_t* knobs = SlotKnobs(slot);
#ifdef OP1_CPU_PROBE
  if (key == kClickWhite) {   // the CPU probe's readout (op1/cpu_probe.cc)
    op1cpu::Toggle();
    return;
  }
#endif
  if (key == kClickOrange) {
    knobs[op1::kEngineKnob] = op1::EncodeSetting(op1::StepEngine(op1::EngineSetting(knobs), direction));
  } else if (key == kClickBlue) {
    knobs[op1::kModelKnob] = op1::EncodeSetting(op1::StepModel(op1::ModelSetting(knobs), direction));
  } else {
    knobs[op1::kPolyKnob] = op1::EncodeSetting(op1::StepPoly(op1::PolySetting(knobs), direction));
  }
  reinterpret_cast<SetKnobsFn>(kSetKnobs)(reinterpret_cast<void*>(kPatches + slot * kPatchSize),
                                          knobs);
}

#ifdef OP1_CLOUDS
// The FX pages' consumed presses, per controller, until their release.
enum { FX_SYNTH, FX_DRUM, FX_MASTER, kNumFxPages };
bool fx_pressed[kNumFxPages][4];

typedef bool (*FxClickFn)(op1fx::Click click, int32_t direction);

// A press on an FX page: whether it was consumed.
bool FxPress(int32_t page, FxClickFn fn, int32_t key, int32_t direction) {
  if (key < kClickBlue || key > kClickOrange) return false;
  const int32_t k = key - kClickBlue;
  const bool consumed = fn(static_cast<op1fx::Click>(k), direction);
  fx_pressed[page][k] = consumed;
  return consumed;
}

// A release: whether its press was ours.
bool FxRelease(int32_t page, int32_t key) {
  if (key < kClickBlue || key > kClickOrange) return false;
  const int32_t k = key - kClickBlue;
  const bool ours = fx_pressed[page][k];
  fx_pressed[page][k] = false;
  return ours;
}

bool FxClick(int32_t key, int32_t direction) {
  return FxPress(FX_SYNTH, op1fx::SynthFxClick, key, direction);
}
#else
bool FxClick(int32_t, int32_t) { return false; }
#endif

}  // namespace

// KeyDispatch passes the key as a byte; stock handlers get it as received.
extern "C" int32_t RingsKeyDown(void* controller, int32_t key) {
  const int32_t code = key & 0xff;
  const int32_t slot = MultiSlot();
  if (slot >= 0 && IsOurClick(slot, code)) {
    Step(slot, code, 1);
    return 1;
  }
  if (FxClick(code, 1)) return 1;
  return reinterpret_cast<KeyFn>(kStockKeyDown)(controller, key);
}

extern "C" int32_t RingsShiftKeyDown(void* controller, int32_t key) {
  const int32_t code = key & 0xff;
  const int32_t slot = MultiSlot();
  if (slot >= 0 && IsOurClick(slot, code)) {
    Step(slot, code, -1);
    return 1;
  }
  if (FxClick(code, -1)) return 1;
  return reinterpret_cast<KeyFn>(kStockShiftKeyDown)(controller, key);
}

// Releases of the encoder clicks are swallowed on a MULTI slot (stock does
// nothing with them; the engine may have changed since the press).
extern "C" int32_t RingsKeyUp(void* controller, int32_t key) {
  const int32_t code = key & 0xff;
  if (code >= kClickBlue && code <= kClickOrange && MultiSlot() >= 0) return 1;
#ifdef OP1_CLOUDS
  // Likewise a click on an FX page that we took.
  if (FxRelease(FX_SYNTH, code)) return 1;
#endif
  return reinterpret_cast<KeyFn>(kStockKeyUp)(controller, key);
}

#ifdef OP1_CLOUDS
// The master FX page. Stock handles no encoder click here (its key cases
// are 0x05..0x14 and 0x2d..0x2f): ours are consumed, the rest go to stock.
extern "C" int32_t FxMasterKeyDown(void* controller, int32_t key) {
  if (FxPress(FX_MASTER, op1fx::MasterFxClick, key & 0xff, 1)) return 1;
  return reinterpret_cast<KeyFn>(kStockMasterKeyDown)(controller, key);
}

extern "C" int32_t FxMasterShiftKeyDown(void* controller, int32_t key) {
  if (FxPress(FX_MASTER, op1fx::MasterFxClick, key & 0xff, -1)) return 1;
  return reinterpret_cast<KeyFn>(kStockMasterShiftKeyDown)(controller, key);
}

extern "C" int32_t FxMasterKeyUp(void* controller, int32_t key) {
  if (FxRelease(FX_MASTER, key & 0xff)) return 1;
  return reinterpret_cast<KeyFn>(kStockMasterKeyUp)(controller, key);
}

// The drum FX page, likewise (drum controller vtable 0x190fa64).
extern "C" int32_t FxDrumKeyDown(void* controller, int32_t key) {
  if (FxPress(FX_DRUM, op1fx::DrumFxClick, key & 0xff, 1)) return 1;
  return reinterpret_cast<KeyFn>(kStockDrumKeyDown)(controller, key);
}

extern "C" int32_t FxDrumShiftKeyDown(void* controller, int32_t key) {
  if (FxPress(FX_DRUM, op1fx::DrumFxClick, key & 0xff, -1)) return 1;
  return reinterpret_cast<KeyFn>(kStockDrumShiftKeyDown)(controller, key);
}

extern "C" int32_t FxDrumKeyUp(void* controller, int32_t key) {
  if (FxRelease(FX_DRUM, key & 0xff)) return 1;
  return reinterpret_cast<KeyFn>(kStockDrumKeyUp)(controller, key);
}
#endif
