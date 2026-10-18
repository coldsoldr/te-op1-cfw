// Clouds in place of the OP-1's phone effect (HANDOVER §3): the master
// effect (stereo) and the live instrument's effect (mono: a synth slot or
// the drum kit; only the live one exists), one CloudsCore each.
//
// Each position plays Clouds or stock phone, by its values[4] (op1/
// clouds_settings.h; 0, every stock preset, is Clouds). Stock phone is
// called as the vtable would; its object is left alone while Clouds plays.
//
// A position (re)starts after a pause (an effect switched off isn't called:
// a gap over 5 ms), after playing phone, and for the synth when the slot
// or the drum kit changes. A restart zeroes the CloudsCore (~360 KB) kClearBytes per call,
// passing the input through, then initialises it: one call doing it all
// takes longer than the 5 ms gap on the device (SDRAM), and the next call
// would restart again, starving the app (clouds1 hung at boot).
//
// The core runs at half the effect rate (OP1CLOUDS_SAMPLE_RATE 22050, to
// halve its CPU): the input is decimated 2:1, the core's wet signal is
// interpolated back and mixed with the full-rate dry input here
// (src/core/halfband.h; the core's own dry/wet stage is skipped).
//
// Per call, the CPU governor (src/core/governor.h) sets the granular mode's
// grain cap from the call interval, Clouds' own cycles and the cycles of
// the rest of the image (measured_cycles: the synth engines and the other
// position), skipping the first calls after a restart. (Test 29 tried a
// share of 0.40 so the screen keeps its frame rate next to Rings: Clouds
// sounded thin. The user prefers the sound; the share stays at its default.)
//
// Simulator builds (-DOP1_SIM, tests/sim_fx.cc): no firmware. Both entries
// play Clouds on the values the call passes, with phone's measured ranges;
// the synth FX page clicks work on sim_synth_values.

#include "op1/clouds_fx.h"

#include "op1/clouds_settings.h"
#include "op1/op1.h"
#include "src/core/clouds_core.h"
#include "src/core/governor.h"
#include "src/core/profile.h"
#include "src/core/halfband.h"

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
using op1clouds::HalfbandDecimator;
using op1clouds::HalfbandInterpolator;

const uint32_t kGapCycles = 2000000;          // 5 ms at 400 MHz
const uint32_t kNominalPeriod = 566 * 1024;   // Test 16: 64 samples at 44.1 kHz
const int32_t kSettleCalls = 16;              // no governor updates after a restart
const uint32_t kClearBytes = 8192;            // of the core zeroed per call (~45 calls)

// Firmware (docs/clouds-port.md §5, docs/next-projects.md §1).
const uint32_t kFxRanges = 0x01de5c50;        // {min, max, x} int16, 48 bytes per type
const int32_t kPhoneType = 3;
const uint32_t kFxProducers = 0x01aaad60;     // effect object per position (synth, drum, master)
const uint32_t kLiveMode = 0xff801038;        // live instrument: 0 synth, 1 drum
const uint32_t kCoreSlot = 0x01aaac0c;        // the synth slot the core plays
const uint32_t kCoreDrum = 0x01aaac10;        // the drum kit the core plays
const uint32_t kEditedDrum = 0xff8016d8;      // the drum kit being edited
const uint32_t kDrumPage = 0x01aab9f4;        // 2 = FX page
const uint32_t kDrumFxTypes = 0xff801754;     // effect type per drum kit
const int32_t kNumDrums = 8;
const uint32_t kCurrentSlot = 0xff8016d4;     // the synth slot being edited
const uint32_t kSynthPage = 0x01b4ee88;       // 2 = FX page
const int32_t kFxPage = 2;
const uint32_t kSlotFxTypes = 0xff801734;     // effect type per synth slot
const uint32_t kSynthFxValues = 0xff801444;   // + slot x 16
const uint32_t kDrumFxValues = 0xff8015c4;    // + instrument x 16
const int32_t kNumSlots = 8;
const uint32_t kPatches = 0x01ddd7e8;         // Patch objects: 8 synth slots, then the drum kits
const uint32_t kPatchSize = 0x38;
const uint32_t kSetFxValues = 0x019ff664;     // Patch::SetFx...(patch, values)
const uint32_t kMasterFxValues = 0xff8016c4;
const uint32_t kMasterSubscreen = 0x01aad5f0; // MasterController's subscreen_
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
  uint32_t cleared; // bytes of core zeroed so far, while restarting
  bool freeze;
  int32_t settle;
  int32_t slot;
  uint32_t last_call;
  uint32_t own;     // Clouds' cycles in the previous call
  uint32_t own_avg; // smoothed (1/16 per call), for the screen's meter
  uint32_t mark;    // measured_cycles after the previous call
  int32_t dry_wet;  // Q16, the mix's smoothed value
  HalfbandDecimator decimator[2];
  HalfbandInterpolator interpolator[2];
  int16_t half[2][op1fx::kFxBlockSize / 2];
  int16_t wet[2][op1fx::kFxBlockSize];   // half scale (+-2 of headroom)
};

Instance synth_fx;
Instance master_fx;

op1clouds::SoftLimitTable soft_limit;

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

// One step of a restart: zeroes the next kClearBytes of the core, and
// after the last one initialises it. Returns whether it's running.
bool RestartStep(Instance* in, int32_t channels) {
  const uint32_t size = sizeof(CloudsCore) & ~3u;
  uint32_t end = in->cleared + kClearBytes;
  if (end > size) end = size;
  volatile uint32_t* words = reinterpret_cast<volatile uint32_t*>(&in->core);
  for (uint32_t i = in->cleared >> 2; i < end >> 2; ++i) words[i] = 0;
  in->cleared = end;
  if (end < size) return false;
  in->core.Init(channels, false);
  in->core.set_external_mix(true);
  for (int32_t c = 0; c < 2; ++c) {
    in->decimator[c].Init();
    in->interpolator[c].Init();
  }
  in->dry_wet = 0;
  op1clouds::InitSoftLimitTable(&soft_limit);
  in->governor.Init(channels == 1 ? 40 : 32, kNominalPeriod);
  in->core.granular()->set_max_grains(in->governor.cap());
  in->core.granular()->set_clock(op1::Cycles);
  in->running = true;
  in->settle = kSettleCalls;
  in->own = 0;
  return true;
}

// The crossfade (Q16 dry/wet) in Q15: the dry side, and the wet side x 0.6
// (upstream's x 1.2 of half the sum).
inline int16_t FadeOut(int32_t dry_wet) {
  return static_cast<int16_t>(op1clouds::InterpolateQ16(op1clouds::kXfadeOutQ31, dry_wet, 4) >> 16);
}

inline int16_t WetGain(int32_t dry_wet) {
  using namespace op1clouds;
  return static_cast<int16_t>(MulQ31(InterpolateQ16(kXfadeInQ31, dry_wet, 4), Q(0.6, 31)) >> 16);
}

// The core at half rate on one call: the decimated input in, the wet
// signal interpolated back and mixed with the dry input as the core would
// (upstream's equal-power crossfade, the wet side x 1.2, soft limit of
// half the sum), then x 2: unity gain on the OP-1 (src/core/halfband.h). 16 x 16 products throughout: the wet signal is kept at
// half scale, the fades in Q15.
void Render(Instance* in, int16_t* l, int16_t* r) {
  using namespace op1clouds;
  const int32_t n = op1fx::kFxBlockSize;
  const int32_t channels = r ? 2 : 1;
  OP1CLOUDS_PROFILE_MARK(profile_t);
  in->decimator[0].Process(l, in->half[0], n / 2);
  if (r) in->decimator[1].Process(r, in->half[1], n / 2);
  OP1CLOUDS_PROFILE_ADD(PROFILE_RESAMPLE_MIX, profile_t);
  if (r) {
    in->core.ProcessStereo(in->half[0], in->half[1], n / 2);
  } else {
    in->core.ProcessMono(in->half[0], n / 2);
  }
  OP1CLOUDS_PROFILE_RESET(profile_t);
  for (int32_t c = 0; c < channels; ++c) {
    // The half-rate buffer is free again: the wet signal, half scale.
    int16_t* w = in->half[c];
    const int32_t* wet = in->core.wet(c);
    for (int32_t i = 0; i < n / 2; ++i) w[i] = static_cast<int16_t>(Sat16(wet[i] >> 13));
    in->interpolator[c].Process(w, in->wet[c], n / 2);
  }

  const int32_t target = in->core.mutable_parameters()->dry_wet;
  const int32_t increment = (target - in->dry_wet) / n;
  if (increment == 0) {
    // The knob is still (the usual case): constant fades.
    in->dry_wet = target;
    const int16_t fade_out = FadeOut(target);
    const int16_t wet_gain = WetGain(target);
    MixSoftLimit(l, in->wet[0], n, fade_out, wet_gain, &soft_limit);
    if (r) MixSoftLimit(r, in->wet[1], n, fade_out, wet_gain, &soft_limit);
    OP1CLOUDS_PROFILE_ADD(PROFILE_RESAMPLE_MIX, profile_t);
    return;
  }
  int32_t dry_wet = in->dry_wet;
  for (int32_t i = 0; i < n; ++i) {
    dry_wet += increment;
    const int16_t fade_out = FadeOut(dry_wet);
    const int16_t wet_gain = WetGain(dry_wet);
    MixSoftLimitC(&l[i], &in->wet[0][i], 1, fade_out, wet_gain, &soft_limit);
    if (r) MixSoftLimitC(&r[i], &in->wet[1][i], 1, fade_out, wet_gain, &soft_limit);
  }
  in->dry_wet = dry_wet;
  OP1CLOUDS_PROFILE_ADD(PROFILE_RESAMPLE_MIX, profile_t);
}

// Clouds on one call. `values`: the position's 8 values, `s` their
// settings. A call that restarts leaves the input as it is.
void Process(Instance* in, int32_t channels, int32_t slot, int16_t* l, int16_t* r,
             const int16_t* values, const FxSettings& s) {
  const uint32_t start = op1::Cycles();
  const uint32_t interval = start - in->last_call;
  if (in->running && (interval > kGapCycles || slot != in->slot)) in->running = false;
  if (!in->running) {
    if (in->cleared == 0 || slot != in->slot) {
      in->cleared = 0;
      in->freeze = false;
      in->slot = slot;
    }
    if (RestartStep(in, channels)) in->cleared = 0;
    const uint32_t end = op1::Cycles();
    in->last_call = end;
    op1fx::measured_cycles += end - start;
    in->mark = op1fx::measured_cycles;
    return;
  }
  in->last_call = start;

  // The grain cap: the rest of the image's cycles since our previous call.
  const uint32_t others = op1fx::measured_cycles - in->mark;
  if (in->settle) {
    --in->settle;
  } else {
    op1clouds::GranularPlayer* granular = in->core.granular();
    in->core.granular()->set_max_grains(in->governor.Update(
        interval, in->own, granular->TakeOverlapCycles(), granular->active_grains(), others));
  }

  int32_t v[op1clouds::kNumParams];
  op1clouds::ParamValues(Ranges(), values, s, v);
  op1clouds::CloudsParams* p = in->core.mutable_parameters();
  op1clouds::ApplyParams(v, p);
  p->freeze = in->freeze;
  in->core.set_playback_mode(op1clouds::kModeOrder[s.mode_index]);

  const uint32_t t1 = op1::Cycles();
  Render(in, l, r);
  const uint32_t end = op1::Cycles();
  in->own = end - t1;
  in->own_avg += static_cast<int32_t>(in->own - in->own_avg) >> 4;
#ifdef OP1CLOUDS_PROFILE
  op1clouds::profile_instance_cycles[channels - 1] += in->own;
  op1clouds::profile_grains[channels - 1] = in->core.granular()->active_grains();
  op1clouds::profile_cap[channels - 1] = in->governor.cap();
  op1clouds::profile_late[channels - 1] = in->governor.late_calls();
#endif
  op1fx::measured_cycles += end - start;
  in->mark = op1fx::measured_cycles;
}

#ifndef OP1_SIM
// The live instrument, whose effect the mono process is running: a synth
// slot 0..7, or kNumSlots + the drum kit; -1 if neither (the firmware's
// mode word, as its own "drum mode" test 0x1a27e0c reads it).
int32_t LiveSlot() {
  const int32_t mode = Read<int32_t>(kLiveMode);
  if (mode == 0) {
    const int32_t slot = Read<int32_t>(kCoreSlot);
    return slot >= 0 && slot < kNumSlots ? slot : -1;
  }
  if (mode == 1) {
    const int32_t drum = Read<int32_t>(kCoreDrum);
    return drum >= 0 && drum < kNumDrums ? kNumSlots + drum : -1;
  }
  return -1;
}

// A slot's effect values (a synth slot, or kNumSlots + a drum kit).
int16_t* SlotValues(int32_t slot) {
  const uint32_t a = slot < kNumSlots ? kSynthFxValues + slot * 16 : kDrumFxValues + (slot - kNumSlots) * 16;
  return reinterpret_cast<int16_t*>(a);
}
#endif

}  // namespace

extern "C" void CloudsFxMono(void* self, int16_t* buf, const int16_t* params) {
#ifdef OP1_SIM
  const int32_t slot = 0;
  const int16_t* values = params;
#else
  const int32_t slot = LiveSlot();
  const int16_t* values = slot >= 0 ? SlotValues(slot) : params;
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
bool DrumFxPageIsPhone() { return false; }
bool MasterFxPageIsPhone() { return true; }

namespace {
int16_t* EditedValues(int32_t* slot) {
  *slot = 0;
  return sim_synth_values;
}
int16_t* EditedDrumValues(int32_t* slot) {
  *slot = -1;
  return nullptr;
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
  return *slot >= 0 ? SlotValues(*slot) : nullptr;
}

// The drum kit being edited, as kNumSlots + its index, if its FX page is
// shown and its effect is phone; else -1.
int32_t EditedDrum() {
  const int32_t drum = Read<int32_t>(kEditedDrum);
  if (drum < 0 || drum >= kNumDrums) return -1;
  if (Read<int32_t>(kDrumPage) != kFxPage) return -1;
  if (Read<int32_t>(kDrumFxTypes + drum * 4) != kPhoneType) return -1;
  return kNumSlots + drum;
}

int16_t* EditedDrumValues(int32_t* slot) {
  *slot = EditedDrum();
  return *slot >= 0 ? SlotValues(*slot) : nullptr;
}

// Saved with the preset as the stock knob handlers save the effect values
// (synth 0x1a50690, drum 0x19775b4: the drum kits' Patch objects follow
// the synth slots').
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
bool DrumFxPageIsPhone() { return EditedDrum() >= 0; }
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

bool DrumFxClick(Click click, int32_t direction) {
  int32_t slot;
  int16_t* values = EditedDrumValues(&slot);
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

bool GetScreenInfo(int32_t position, int32_t index, ScreenInfo* info) {
  const int16_t* values;
#ifdef OP1_SIM
  values = position == 3 ? sim_master_values : sim_synth_values;
  (void)index;
#else
  if (position == 3) {
    values = reinterpret_cast<const int16_t*>(kMasterFxValues);
  } else if (position == 0 && index >= 0 && index < kNumSlots) {
    values = SlotValues(index);
  } else if (position == 1 && index >= 0 && index < kNumDrums) {
    values = SlotValues(kNumSlots + index);
  } else {
    return false;
  }
#endif
  const FxSettings s = op1clouds::DecodeSettings(values);
  if (s.engine != op1clouds::FX_CLOUDS) return false;
  const Instance* in = position == 3 ? &master_fx : &synth_fx;
  info->page = s.page;
  info->mode = op1clouds::kModeOrder[s.mode_index];
  info->freeze = in->freeze;
  op1clouds::ParamValues(Ranges(), values, s, info->values);
  info->pitch_q16 = op1clouds::PitchQ16(info->values[2]);
  info->load_percent = in->running ? static_cast<int32_t>(in->own_avg / (kNominalPeriod / 100)) : 0;
  return true;
}

}  // namespace op1fx
