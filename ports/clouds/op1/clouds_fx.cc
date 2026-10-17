// Clouds in place of the OP-1's phone effect, in two positions (HANDOVER
// §3): the master effect (stereo) and the synth effect (mono), one
// CloudsCore each. The drum effect, which shares the mono process with the
// synth one, stays stock phone.
//
// Each position plays Clouds or stock phone, by its values[4] (op1/
// clouds_settings.h; 0, every stock preset, is Clouds). Stock phone is
// called as the vtable would; its object is left alone while Clouds plays.
//
// A position (re)starts after a pause (an effect switched off isn't called:
// a gap over 5 ms), after playing phone, and for the synth when the slot
// changes: the call that restarts clears Clouds' buffers and passes the
// input through.
//
// Per call, the CPU governor (src/core/governor.h) sets the granular mode's
// grain cap from the call interval, Clouds' own cycles and the cycles of
// the rest of the image (measured_cycles: the synth engines and the other
// position), skipping the first calls after a restart.
//
// Simulator builds (-DOP1_SIM, tests/sim_fx.cc): no firmware. Both entries
// play Clouds on the values the call passes, with phone's measured ranges;
// the synth FX page clicks work on sim_synth_values.

#include "op1/clouds_fx.h"

#include "op1/clouds_settings.h"
#include "op1/op1.h"
#include "src/core/clouds_core.h"
#include "src/core/governor.h"

namespace op1fx {

uint32_t measured_cycles;

#ifdef OP1_SIM
int16_t sim_synth_values[8];
#endif

}  // namespace op1fx

namespace {

using op1clouds::CloudsCore;
using op1clouds::EncoderRange;
using op1clouds::FxSettings;
using op1clouds::Governor;

const uint32_t kGapCycles = 2000000;          // 5 ms at 400 MHz
const uint32_t kNominalPeriod = 566 * 1024;   // Test 16: 64 samples at 44.1 kHz
const int32_t kSettleCalls = 16;              // no governor updates after a restart

// Firmware (docs/clouds-port.md §5, docs/next-projects.md §1).
const uint32_t kFxRanges = 0x01de5c50;        // {min, max, x} int16, 48 bytes per type
const int32_t kPhoneType = 3;
const uint32_t kFxProducers = 0x01aaad60;     // effect object per position (synth, drum, master)
const uint32_t kCoreSlot = 0x01aaac0c;        // the synth slot the core plays
const uint32_t kCurrentSlot = 0xff8016d4;     // the synth slot being edited
const uint32_t kSynthPage = 0x01b4ee88;       // 2 = FX page
const int32_t kFxPage = 2;
const uint32_t kSlotFxTypes = 0xff801734;     // effect type per synth slot
const uint32_t kSynthFxValues = 0xff801444;   // + slot x 16
const uint32_t kDrumFxValues = 0xff8015c4;    // + instrument x 16
const int32_t kNumSlots = 8;
const uint32_t kPatches = 0x01ddd7e8;         // Patch objects, one per slot
const uint32_t kPatchSize = 0x38;
const uint32_t kSetFxValues = 0x019ff664;     // Patch::SetFx...(patch, values)
const uint32_t kMasterFxValues = 0xff8016c4;
const uint32_t kMasterSubscreen = 0x01aad5d8; // MasterController's subscreen_
const int32_t kMasterFxSubscreen = 2;         // its knob case copies 0xff8016c4
const uint32_t kPhoneStereoVtable = 0x01911314;

typedef void (*SetValuesFn)(void* patch, const int16_t* values);

// Phone's encoder ranges (FX Ranges table, read with bfin-elf-gdb), used
// when the table doesn't look valid and in the simulator.
const int16_t kPhoneRanges[4][2] = { { 204, 20480 }, { 3072, 17408 }, { 1536, 16896 }, { 0, 32767 } };

template <typename T>
T Read(uint32_t address) {
  return *reinterpret_cast<const volatile T*>(address);
}

struct Instance {
  CloudsCore core;
  Governor governor;
  bool running;
  bool freeze;
  int32_t settle;
  int32_t slot;
  uint32_t last_call;
  uint32_t own;     // Clouds' cycles in the previous call
  uint32_t mark;    // measured_cycles after the previous call
};

Instance synth_fx;
Instance master_fx;

EncoderRange ranges[4];
bool ranges_ready;

void InitRanges() {
  for (int32_t k = 0; k < 4; ++k) {
    int32_t min = kPhoneRanges[k][0];
    int32_t max = kPhoneRanges[k][1];
#ifndef OP1_SIM
    const uint32_t entry = kFxRanges + kPhoneType * 48 + k * 6;
    const int32_t table_min = Read<int16_t>(entry);
    const int32_t table_max = Read<int16_t>(entry + 2);
    if (table_max > table_min && table_max - table_min >= 8192) {
      min = table_min;
      max = table_max;
    }
#endif
    op1clouds::InitRange(&ranges[k], min, max);
  }
  ranges_ready = true;
}

const EncoderRange* Ranges() {
  if (!ranges_ready) InitRanges();
  return ranges;
}

void Restart(Instance* in, int32_t channels, int32_t slot, uint32_t now) {
  in->core.Init(channels);
  in->governor.Init(channels == 1 ? 40 : 32, kNominalPeriod);
  in->core.granular()->set_max_grains(in->governor.cap());
  in->running = true;
  in->freeze = false;
  in->settle = kSettleCalls;
  in->slot = slot;
  in->own = 0;
  in->last_call = now;
  in->mark = op1fx::measured_cycles;
}

// Clouds on one call. `values`: the position's 8 values, `s` their
// settings. A call that restarts leaves the input as it is.
void Process(Instance* in, int32_t channels, int32_t slot, int16_t* l, int16_t* r,
             const int16_t* values, const FxSettings& s) {
  const uint32_t start = op1::Cycles();
  const uint32_t interval = start - in->last_call;
  if (!in->running || interval > kGapCycles || slot != in->slot) {
    Restart(in, channels, slot, start);
    op1fx::measured_cycles += op1::Cycles() - start;
    in->mark = op1fx::measured_cycles;
    return;
  }
  in->last_call = start;

  // The grain cap: the rest of the image's cycles since our previous call.
  const uint32_t others = op1fx::measured_cycles - in->mark;
  if (in->settle) {
    --in->settle;
  } else {
    in->core.granular()->set_max_grains(in->governor.Update(interval, in->own, others));
  }

  int32_t v[op1clouds::kNumParams];
  op1clouds::ParamValues(Ranges(), values, s, v);
  op1clouds::CloudsParams* p = in->core.mutable_parameters();
  op1clouds::ApplyParams(v, p);
  p->freeze = in->freeze;
  in->core.set_playback_mode(op1clouds::kModeOrder[s.mode_index]);

  const uint32_t t1 = op1::Cycles();
  if (r) {
    in->core.ProcessStereo(l, r, op1fx::kFxBlockSize);
  } else {
    in->core.ProcessMono(l, op1fx::kFxBlockSize);
  }
  const uint32_t end = op1::Cycles();
  in->own = end - t1;
  op1fx::measured_cycles += end - start;
  in->mark = op1fx::measured_cycles;
}

#ifndef OP1_SIM
// Whether `self` is (inside) the effect object at position `pos`.
bool IsPosition(const void* self, int32_t pos) {
  const uint32_t object = Read<uint32_t>(kFxProducers + pos * 4);
  const uint32_t p = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self));
  return object && p - object < 0x100;
}

// The synth slot whose effect the mono process is running, or -1 for the
// drum effect (or an unknown caller): both share one object, so the
// position comes from the producers (and the drum values, if the call
// passes those).
int32_t SynthSlot(const void* self, const int16_t* params) {
  const uint32_t a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(params));
  if (a - kDrumFxValues < 8 * 16) return -1;
  const bool synth = IsPosition(self, 0);
  const bool drum = IsPosition(self, 1);
  if (!synth && !drum) return -1;
  if (drum && !synth) return -1;
  const int32_t slot = Read<int32_t>(kCoreSlot);
  return slot >= 0 && slot < kNumSlots ? slot : -1;
}

int16_t* SynthValues(int32_t slot) {
  return reinterpret_cast<int16_t*>(kSynthFxValues + slot * 16);
}
#endif

}  // namespace

extern "C" void CloudsFxMono(void* self, int16_t* buf, const int16_t* params) {
#ifdef OP1_SIM
  const int32_t slot = 0;
  const int16_t* values = params;
#else
  const int32_t slot = SynthSlot(self, params);
  const int16_t* values = slot >= 0 ? SynthValues(slot) : params;
#endif
  const FxSettings s = op1clouds::DecodeSettings(values);
  if (slot < 0 || s.engine != op1clouds::FX_CLOUDS) {
    synth_fx.running = false;
#ifndef OP1_SIM
    reinterpret_cast<op1fx::FxMonoFn>(op1fx::kPhoneMonoProcess)(self, buf, params);
#endif
    return;
  }
  Process(&synth_fx, 1, slot, buf, nullptr, values, s);
}

extern "C" void CloudsFxStereo(void* self, int16_t* l, int16_t* r, const int16_t* params) {
  // The master's values are the ones the call passes (0xff8016c4, Test 16).
  const FxSettings s = op1clouds::DecodeSettings(params);
  if (s.engine != op1clouds::FX_CLOUDS) {
    master_fx.running = false;
#ifndef OP1_SIM
    reinterpret_cast<op1fx::FxStereoFn>(op1fx::kPhoneStereoProcess)(self, l, r, params);
#endif
    return;
  }
  Process(&master_fx, 2, 0, l, r, params, s);
}

namespace op1fx {

#ifdef OP1_SIM
int16_t sim_master_values[8];

bool SynthFxPageIsPhone() { return true; }
bool MasterFxPageIsPhone() { return true; }

namespace {
int16_t* EditedValues(int32_t* slot) {
  *slot = 0;
  return sim_synth_values;
}
void Save(int32_t, const int16_t*) {}
int16_t* MasterValues() { return sim_master_values; }
}  // namespace
#else
namespace {

int32_t EditedSlot() {
  const int32_t slot = Read<int32_t>(kCurrentSlot);
  if (slot < 0 || slot >= kNumSlots) return -1;
  if (Read<int32_t>(kSynthPage) != kFxPage) return -1;
  if (Read<int32_t>(kSlotFxTypes + slot * 4) != kPhoneType) return -1;
  return slot;
}

int16_t* EditedValues(int32_t* slot) {
  *slot = EditedSlot();
  return *slot >= 0 ? SynthValues(*slot) : nullptr;
}

// Saved with the preset as the stock knob handler saves the effect values
// (0x1a50690).
void Save(int32_t slot, const int16_t* values) {
  reinterpret_cast<SetValuesFn>(kSetFxValues)(reinterpret_cast<void*>(kPatches + slot * kPatchSize),
                                              values);
}

// The master page shows the effect, and the master effect is phone (by
// its object's vtable: where the master effect's type is kept isn't known
// yet).
int16_t* MasterValues() {
  if (Read<int32_t>(kMasterSubscreen) != kMasterFxSubscreen) return nullptr;
  const uint32_t object = Read<uint32_t>(kFxProducers + 2 * 4);
  if (!object || Read<uint32_t>(object) != kPhoneStereoVtable) return nullptr;
  return reinterpret_cast<int16_t*>(kMasterFxValues);
}

}  // namespace

bool SynthFxPageIsPhone() { return EditedSlot() >= 0; }
bool MasterFxPageIsPhone() { return MasterValues() != nullptr; }
#endif

namespace {

// A click on a position's values: whether it was consumed, and in *changed
// whether the values changed (to be saved).
bool ClickOn(Instance* in, int16_t* values, Click click, int32_t direction, bool* changed) {
  *changed = false;
  if (click == CLICK_ORANGE) {
    op1clouds::ToggleEngine(values);
    *changed = true;
    return true;
  }
  if (op1clouds::DecodeSettings(values).engine != op1clouds::FX_CLOUDS) return false;
  switch (click) {
    case CLICK_BLUE:
      op1clouds::StepMode(values, direction);
      break;
    case CLICK_GREEN:
      // Freeze isn't saved: it holds the live buffer.
      in->freeze = !in->freeze;
      return true;
    case CLICK_WHITE:
      op1clouds::SwapPage(Ranges(), values);
      break;
    default:
      return false;
  }
  *changed = true;
  return true;
}

}  // namespace

bool SynthFxClick(Click click, int32_t direction) {
  int32_t slot;
  int16_t* values = EditedValues(&slot);
  if (!values) return false;
  bool changed;
  const bool consumed = ClickOn(&synth_fx, values, click, direction, &changed);
  if (changed) Save(slot, values);
  return consumed;
}

// The master's values are the live array its state is kept in; its knob
// case writes them the same way, with no save call of its own.
bool MasterFxClick(Click click, int32_t direction) {
  int16_t* values = MasterValues();
  if (!values) return false;
  bool changed;
  return ClickOn(&master_fx, values, click, direction, &changed);
}

}  // namespace op1fx
