// The effect category hosts (op1/fx_host.h, docs/fx-categories.md).
//
// Process: the mono entry plays the live instrument's effect (a synth slot
// or the drum kit; only the live one exists). The engine and its settings
// come from that slot's stored values; the encoders from the passed copy
// (0xff801724), which the effect LFO modulates, but on spring its range
// reaches values[4..7] too, the marker. The stereo entry plays the master
// from the values it's passed (0xff8016c4, Test 16). A position whose engine
// changes stops the previous one (engines may share memory later: each must
// restart). fx1's values (Test 37) are migrated wherever the host meets them.
//
// Clicks: the synth, drum and master FX pages of a hosted type. Orange steps
// through stock and the host's engines (shift: back). Each synth slot, drum
// kit and the master remembers every engine's values (stock's too) while
// the OP-1 is on, and a step back to one restores them; a step to one not
// played yet since power-up starts it afresh (an engine at its defaults;
// stock with the type's factory values[4..7], which stock may read). Only
// the shown engine's values are saved with the preset. The other clicks go
// to the engine.
//
// Screen: the host type's screen shows the stock drawing or the shown
// position's engine's own document (fx_screen.h), loaded at boot.
//
// Simulator builds (-DOP1_SIM): no firmware. The mono entry plays slot 0
// from the passed values, the clicks act on sim_synth_values and
// sim_master_values, the reverb host's.

#include "fx/op1/fx_host.h"

#include "fx/op1/fx_screen.h"
#include "fx/op1/modules.h"
#include "fx/op1/modules.h"

#ifdef OP1_CLOUDS
#include "op1/clouds_fx.h"
#endif
#include "fx/op1/crush_fx.h"
#ifdef OP1FX_PLATE
#include "fx/op1/plate_fx.h"
#endif
#ifdef OP1FX_HALL
#include "fx/op1/hall_fx.h"
#endif
#ifdef OP1FX_DRIVE
#include "fx/op1/drive_fx.h"
#endif
#ifdef OP1FX_TAPE
#include "fx/op1/tape_fx.h"
#endif
#ifdef OP1FX_FOLD
#include "fx/op1/fold_fx.h"
#endif
#ifdef OP1FX_ENSEMBLE
#include "fx/op1/ensemble_fx.h"
#endif
#ifdef OP1FX_SHIFT
#include "fx/op1/shift_fx.h"
#endif
#ifdef OP1FX_TAPE_ECHO
#include "fx/op1/tape_echo_fx.h"
#endif

namespace op1fx {

uint32_t measured_cycles;

#ifdef OP1_SIM
int16_t sim_synth_values[8];
int16_t sim_master_values[8];
const int16_t (*sim_ranges)[2];
#endif

}  // namespace op1fx

namespace {

using op1fx::Click;
using op1fx::EncoderRange;
using op1fx::Engine;

template <typename T>
T Read(uint32_t address) {
  return *reinterpret_cast<const volatile T*>(address);
}

// Firmware (docs/clouds-port.md §5, docs/fx-categories.md).
const uint32_t kFxRanges = 0x01de5c50;        // {min, max, x} int16, 48 bytes per type
const uint32_t kFxProducers = 0x01aaad60;     // effect object per position (synth, drum, master)
const uint32_t kLiveMode = 0xff801038;        // live instrument: 0 synth, 1 drum
const uint32_t kCoreSlot = 0x01aaac0c;        // the synth slot the mono effect plays
const uint32_t kCoreDrum = 0x01aaac10;        // the drum kit it plays
const uint32_t kEditedDrum = 0xff8016d8;      // the drum kit being edited
const uint32_t kDrumPage = 0x01aab9f4;        // 2 = FX page
const uint32_t kDrumFxTypes = 0xff801754;     // effect type per drum kit
const int32_t kNumDrums = 8;
const uint32_t kCurrentSlot = 0xff8016d4;     // the synth slot being edited
const uint32_t kSynthPage = 0x01b4ee88;       // 2 = FX page
const int32_t kFxPage = 2;
const uint32_t kSlotFxTypes = 0xff801734;     // effect type per synth slot
const uint32_t kSynthFxValues = 0xff801444;   // + slot x 16
const uint32_t kDrumFxValues = 0xff8015c4;    // + kit x 16
const int32_t kNumSlots = 8;
const uint32_t kPatches = 0x01ddd7e8;         // Patch objects: 8 synth slots, then the drum kits
const uint32_t kPatchSize = 0x38;
const uint32_t kSetFxValues = 0x019ff664;     // Patch::SetFx...(patch, values)
const uint32_t kMasterFxValues = 0xff8016c4;
const uint32_t kMasterSubscreen = 0x01aad5f0; // MasterController's subscreen_
const int32_t kMasterFxSubscreen = 2;         // its knob case copies 0xff8016c4

typedef void (*SetValuesFn)(void* patch, const int16_t* values);

const int32_t kMaxEngines = 8;

// An engine and the module bit that switches it on (op1/modules.h).
struct Hosted {
  const Engine* engine;
  op1::Module module;
};

// A category: its stock type and the words its entries replace.
struct Host {
  int32_t type;              // fx_types.cpp index (FX Ranges, slot FX type words)
  uint32_t mono_process;     // stock process functions
  uint32_t stereo_process;
  uint32_t stereo_vtable;    // the master object's vtable when it's this type
  uint32_t screen_init;      // stock screen Init and Update
  uint32_t screen_update;
  int16_t stock_tail[4];     // the type's factory default_params[4..7]
  int16_t ranges[4][2];      // the FX Ranges table's entries (used if it's not valid)
  // In orange-click order after stock, null after the last (each engine is
  // in the image if its OP1FX_ flag is set, and switched on if its module
  // bit is: an engine switched off is skipped everywhere, and a slot saved
  // on it plays stock).
  Hosted engines[kMaxEngines + 1];
};

enum { HOST_REVERB, HOST_MODULATION, HOST_DIRT, HOST_DELAY, kNumHosts };

const Host kHosts[kNumHosts] = {
  // reverb: spring (Rymd.cpp)
  { 4, op1fx::kSpringMonoProcess, op1fx::kSpringStereoProcess, 0x019113dc, op1fx::kSpringScreenInit,
    op1fx::kSpringScreenUpdate, { 8000, 8000, 8000, 8000 },
    { { 1344, 16448 }, { 7744, 16448 }, { 0, 16384 }, { 0, 32767 } },
    {
#ifdef OP1FX_PLATE
      { &op1fx::kPlateEngine, op1::MODULE_PLATE },
#endif
#ifdef OP1FX_HALL
      { &op1fx::kHallEngine, op1::MODULE_HALL },
#endif
#ifdef OP1_CLOUDS
      { &op1clouds::kCloudsEngine, op1::MODULE_CLOUDS },
#endif
    } },
  // modulation: cwo (BodeAudioEngine.cpp)
  { 7, op1fx::kCwoMonoProcess, op1fx::kCwoStereoProcess, 0x0190f594, op1fx::kCwoScreenInit,
    op1fx::kCwoScreenUpdate, { 0, 0, 0, 0 },
    { { 0, 32767 }, { 0, 32767 }, { 0, 32767 }, { 0, 32767 } },
    {
#ifdef OP1FX_ENSEMBLE
      { &op1fx::kEnsembleEngine, op1::MODULE_ENSEMBLE },
#endif
#ifdef OP1FX_SHIFT
      { &op1fx::kShiftEngine, op1::MODULE_SHIFT },
#endif
    } },
  // dirt: punch (Mllp.cpp)
  { 2, op1fx::kPunchMonoProcess, op1fx::kPunchStereoProcess, 0x01910c3c, op1fx::kPunchScreenInit,
    op1fx::kPunchScreenUpdate, { 8000, 8000, 8000, 8000 },
    { { 1344, 12480 }, { 0, 32767 }, { 1536, 25088 }, { 0, 32767 } },
    {
#ifdef OP1FX_DRIVE
      { &op1fx::kDriveEngine, op1::MODULE_DRIVE },
#endif
#ifdef OP1FX_TAPE
      { &op1fx::kTapeEngine, op1::MODULE_TAPE },
#endif
#ifdef OP1FX_FOLD
      { &op1fx::kFoldEngine, op1::MODULE_FOLD },
#endif
      { &op1fx::kCrushEngine, op1::MODULE_CRUSH },
    } },
  // delay
  { 0, op1fx::kDelayMonoProcess, op1fx::kDelayStereoProcess, 0x0190fb8c, op1fx::kDelayScreenInit,
    op1fx::kDelayScreenUpdate, { 0, 0, 0, 0 },
    { { 1024, 11264 }, { 3276, 32767 }, { 0, 16384 }, { 0, 32767 } },
    {
#ifdef OP1FX_TAPE_ECHO
      { &op1fx::kTapeEchoEngine, op1::MODULE_TAPE_ECHO },
#endif
    } },
};

int32_t NumEngines(int32_t h) {
  int32_t n = 0;
  while (n < kMaxEngines && kHosts[h].engines[n].engine) ++n;
  return n;
}

// Whether host h's engine i (0-based in its list) is switched on.
bool Enabled(int32_t h, int32_t i) { return op1::ModuleEnabled(kHosts[h].engines[i].module); }

// The engines switched on.
int32_t NumEnabled(int32_t h) {
  int32_t n = 0;
  for (int32_t i = 0; i < NumEngines(h); ++i) n += Enabled(h, i) ? 1 : 0;
  return n;
}

EncoderRange ranges[kNumHosts][4];
bool ranges_ready[kNumHosts];

const EncoderRange* Ranges(int32_t h) {
  if (!ranges_ready[h]) {
    for (int32_t k = 0; k < 4; ++k) {
      int32_t min = kHosts[h].ranges[k][0];
      int32_t max = kHosts[h].ranges[k][1];
#ifdef OP1_SIM
      if (op1fx::sim_ranges) {
        min = op1fx::sim_ranges[k][0];
        max = op1fx::sim_ranges[k][1];
      }
#else
      const uint32_t entry = kFxRanges + kHosts[h].type * 48 + k * 6;
      const int32_t table_min = Read<int16_t>(entry);
      const int32_t table_max = Read<int16_t>(entry + 2);
      if (table_max > table_min && table_max - table_min >= 8192) {
        min = table_min;
        max = table_max;
      }
#endif
      op1fx::InitRange(&ranges[h][k], min, max);
    }
    ranges_ready[h] = true;
  }
  return ranges[h];
}

// The engine a position's values select on host h, or null for stock.
const Engine* EngineFor(int32_t h, const int16_t* values) {
  const int32_t id = op1fx::EngineId(values);
  if (id == 0) return nullptr;
  for (int32_t i = 0; i < NumEngines(h); ++i) {
    if (Enabled(h, i) && kHosts[h].engines[i].engine->id == id) return kHosts[h].engines[i].engine;
  }
  return nullptr;   // an engine this image doesn't have, or one switched off: stock
}

// fx1's values (a negative values[4..7], IsLegacy) converted in place: the
// engine it marked to this layout, anything else to stock. Values the
// firmware holds (the stored slot values, the master's live array), not
// saved here: the next knob turn or click on that page saves them, and the
// patch check no longer rejects the old ones (ports/rings/op1/patches.cc).
void Migrate(int32_t h, int16_t* values) {
  if (!op1fx::IsLegacy(values)) return;
  const int32_t id = op1fx::LegacyEngineId(values);
  for (int32_t i = 0; i < NumEngines(h); ++i) {
    const Engine* e = kHosts[h].engines[i].engine;
    if (e->id == id && e->migrate) {
      e->migrate(values);
      op1fx::SetEngineId(values, id);
      return;
    }
  }
  for (int32_t k = 0; k < 4; ++k) values[4 + k] = kHosts[h].stock_tail[k];
}

// The engine each position played last, to stop it when that changes; the
// stored values it played and their encoders as played (the LFO's), for
// the screen.
const Engine* playing[op1fx::kNumPositions];
const int16_t* played[op1fx::kNumPositions];
int16_t played_live[op1fx::kNumPositions][4];

void Played(int32_t position, const int16_t* values, const int16_t* live) {
  played[position] = values;
  for (int32_t k = 0; k < 4; ++k) played_live[position][k] = live[k];
}

void Select(int32_t position, const Engine* engine) {
  const Engine* old = playing[position];
  if (old == engine) return;
  if (old && old->stop) old->stop(position);
  playing[position] = engine;
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
#endif

// A slot's effect values (a synth slot, or kNumSlots + a drum kit).
int16_t* SlotValues(int32_t slot) {
#ifdef OP1_SIM
  (void)slot;
  return op1fx::sim_synth_values;
#else
  const uint32_t a = slot < kNumSlots ? kSynthFxValues + slot * 16 : kDrumFxValues + (slot - kNumSlots) * 16;
  return reinterpret_cast<int16_t*>(a);
#endif
}

void Mono(int32_t h, void* self, int16_t* buf, const int16_t* params) {
#ifdef OP1_SIM
  const int32_t slot = 0;
  int16_t* values = const_cast<int16_t*>(params);
#else
  const int32_t slot = LiveSlot();
  int16_t* values = slot >= 0 ? SlotValues(slot) : nullptr;
#endif
  const Engine* engine = nullptr;
  if (values) {
    Migrate(h, values);
    engine = EngineFor(h, values);
  }
  Select(op1fx::POSITION_MONO, engine);
  if (!engine) {
#ifndef OP1_SIM
    reinterpret_cast<op1fx::FxMonoFn>(kHosts[h].mono_process)(self, buf, params);
#endif
    return;
  }
  Played(op1fx::POSITION_MONO, values, params);
  engine->process(op1fx::POSITION_MONO, slot, buf, nullptr, params, values, Ranges(h));
}

void Stereo(int32_t h, void* self, int16_t* l, int16_t* r, const int16_t* params) {
  // The master's live array (0xff8016c4), which its knob case writes too.
  int16_t* values = const_cast<int16_t*>(params);
  Migrate(h, values);
  const Engine* engine = EngineFor(h, values);
  Select(op1fx::POSITION_MASTER, engine);
  if (!engine) {
#ifndef OP1_SIM
    reinterpret_cast<op1fx::FxStereoFn>(kHosts[h].stereo_process)(self, l, r, params);
#endif
    return;
  }
  Played(op1fx::POSITION_MASTER, values, params);
  engine->process(op1fx::POSITION_MASTER, 0, l, r, params, values, Ranges(h));
}

// The host of an effect type that has engines switched on, or -1.
int32_t HostOfType(int32_t type) {
  for (int32_t h = 0; h < kNumHosts; ++h) {
    if (kHosts[h].type == type && NumEnabled(h) > 0) return h;
  }
  return -1;
}

// The values each owner (a synth slot, kNumSlots + a drum kit, or the
// master) last had on each of a host's engines (index 0 = stock), for the
// orange click to bring back.
const int32_t kOwnerMaster = kNumSlots + kNumDrums;
const int32_t kNumOwners = kOwnerMaster + 1;

struct Remembered {
  int16_t values[kMaxEngines + 1][8];
  uint32_t valid;   // bit i: values[i] holds engine index i's
};

Remembered remembered[kNumOwners][kNumHosts];

// A click on host h's values at a position, from their owner.
bool HostClick(int32_t h, int32_t owner, int32_t position, int16_t* values, Click click,
               int32_t direction, bool* changed) {
  *changed = op1fx::IsLegacy(values);
  Migrate(h, values);
  const Host& host = kHosts[h];
  const Engine* engine = EngineFor(h, values);
  if (click != op1fx::CLICK_ORANGE) {
    if (!engine || !engine->click) return false;
    bool clicked = false;
    const bool consumed = engine->click(position, values, Ranges(h), click, direction, &clicked);
    *changed = *changed || clicked;
    return consumed;
  }
  // Orange: stock = index 0, the engines 1..n.
  const int32_t n = NumEngines(h) + 1;
  int32_t index = 0;
  for (int32_t i = 0; i + 1 < n; ++i) {
    if (host.engines[i].engine == engine) index = i + 1;
  }
  Remembered& memory = remembered[owner][h];
  for (int32_t k = 0; k < 8; ++k) memory.values[index][k] = values[k];
  memory.valid |= 1u << index;
  // The next that is stock or an engine switched on (stock always is).
  do {
    index = (index + direction + n) % n;
  } while (index != 0 && !Enabled(h, index - 1));
  if (memory.valid & (1u << index)) {
    for (int32_t k = 0; k < 8; ++k) values[k] = memory.values[index][k];
  } else if (index == 0) {
    for (int32_t k = 0; k < 4; ++k) values[4 + k] = host.stock_tail[k];
  } else {
    const Engine* next = host.engines[index - 1].engine;
    for (int32_t k = 4; k < 8; ++k) values[k] = 0;
    if (next->enter) next->enter(values, Ranges(h));
    op1fx::SetEngineId(values, next->id);
  }
  *changed = true;
  return true;
}

#ifdef OP1_SIM
int16_t* EditedSynth(int32_t* slot, int32_t* h) {
  *slot = 0;
  *h = HOST_REVERB;
  return op1fx::sim_synth_values;
}
int16_t* EditedDrum(int32_t* slot, int32_t* h) {
  *slot = -1;
  *h = -1;
  return nullptr;
}
int16_t* EditedMaster(int32_t* h) {
  *h = HOST_REVERB;
  return op1fx::sim_master_values;
}
void Save(int32_t, const int16_t*) {}
#else
// The synth slot being edited, if its FX page is shown and its effect type
// is hosted: its values, slot and host; else null.
int16_t* EditedSynth(int32_t* slot, int32_t* h) {
  *slot = Read<int32_t>(kCurrentSlot);
  if (*slot < 0 || *slot >= kNumSlots) return nullptr;
  if (Read<int32_t>(kSynthPage) != kFxPage) return nullptr;
  *h = HostOfType(Read<int32_t>(kSlotFxTypes + *slot * 4));
  return *h >= 0 ? SlotValues(*slot) : nullptr;
}

// The drum kit being edited, likewise (slot = kNumSlots + the kit).
int16_t* EditedDrum(int32_t* slot, int32_t* h) {
  const int32_t drum = Read<int32_t>(kEditedDrum);
  if (drum < 0 || drum >= kNumDrums) return nullptr;
  if (Read<int32_t>(kDrumPage) != kFxPage) return nullptr;
  *h = HostOfType(Read<int32_t>(kDrumFxTypes + drum * 4));
  *slot = kNumSlots + drum;
  return *h >= 0 ? SlotValues(*slot) : nullptr;
}

// The master page shows the effect, and the master effect is hosted (by its
// object's vtable: where the master effect's type is kept isn't known yet).
int16_t* EditedMaster(int32_t* h) {
  if (Read<int32_t>(kMasterSubscreen) != kMasterFxSubscreen) return nullptr;
  const uint32_t object = Read<uint32_t>(kFxProducers + 2 * 4);
  if (!object) return nullptr;
  const uint32_t vtable = Read<uint32_t>(object);
  for (int32_t i = 0; i < kNumHosts; ++i) {
    if (kHosts[i].stereo_vtable == vtable && NumEnabled(i) > 0) {
      *h = i;
      return reinterpret_cast<int16_t*>(kMasterFxValues);
    }
  }
  return nullptr;
}

// Saved with the preset as the stock knob handlers save the effect values
// (synth 0x1a50690, drum 0x19775b4: the drum kits' Patch objects follow
// the synth slots').
void Save(int32_t slot, const int16_t* values) {
  reinterpret_cast<SetValuesFn>(kSetFxValues)(reinterpret_cast<void*>(kPatches + slot * kPatchSize),
                                              values);
}
#endif

// Screens: per host, the stock document and each engine's.
struct ScreenState {
  void* stock;
  void* documents[kMaxEngines];
  void* shown;
};

ScreenState screens[kNumHosts];

void ScreenInit(int32_t h, void* self) {
  using namespace op1fx::screen;
  ScreenState& s = screens[h];
  s.stock = DocumentOf(self);
  s.shown = s.stock;
  if (s.stock) reinterpret_cast<InitFn>(kHosts[h].screen_init)(self);
  for (int32_t i = 0; i < NumEngines(h); ++i) {
    const Engine* e = kHosts[h].engines[i].engine;
    s.documents[i] = e->svg && Enabled(h, i) ? LoadDocument(e->svg) : nullptr;
    if (s.documents[i] && e->screen_init) {
      DocumentOf(self) = s.documents[i];
      e->screen_init(self);
      DocumentOf(self) = s.stock;
    }
  }
}

void ScreenUpdate(int32_t h, void* self, int32_t ticks) {
  using namespace op1fx::screen;
  ScreenState& s = screens[h];
  const int32_t position = Read<int32_t>(kShownPosition);
  const int32_t index = Read<int32_t>(kShownIndex);
  int16_t* values = nullptr;
  if (position == op1fx::kScreenMaster) {
    values = reinterpret_cast<int16_t*>(kMasterFxValues);
  } else if (position == op1fx::kScreenSynth && index >= 0 && index < kNumSlots) {
    values = SlotValues(index);
  } else if (position == op1fx::kScreenDrum && index >= 0 && index < kNumDrums) {
    values = SlotValues(kNumSlots + index);
  }
  int32_t engine_index = -1;
  if (values) {
    Migrate(h, values);
    const Engine* e = EngineFor(h, values);
    for (int32_t i = 0; i < NumEngines(h); ++i) {
      if (kHosts[h].engines[i].engine == e && s.documents[i] && e->screen_update) engine_index = i;
    }
  }
  void* document = engine_index >= 0 ? s.documents[engine_index] : s.stock;
  if (document != s.shown) {
    DocumentOf(self) = document;
    if (document) Flush(document);
    s.shown = document;
  }
  if (engine_index >= 0) {
    const int32_t p = position == op1fx::kScreenMaster ? op1fx::POSITION_MASTER : op1fx::POSITION_MONO;
    const int16_t* encoders = playing[p] && played[p] == values ? played_live[p] : values;
    kHosts[h].engines[engine_index].engine->screen_update(self, position, encoders, values, Ranges(h));
    // Update must draw (Screen::UpdateAll doesn't).
    Draw(self);
  } else if (s.stock) {
    reinterpret_cast<UpdateFn>(kHosts[h].screen_update)(self, ticks);
  }
}

}  // namespace

namespace op1fx {

bool SynthFxClick(Click click, int32_t direction) {
  int32_t slot, h;
  int16_t* values = EditedSynth(&slot, &h);
  if (!values) return false;
  bool changed;
  const bool consumed = HostClick(h, slot, POSITION_MONO, values, click, direction, &changed);
  if (changed) Save(slot, values);
  return consumed;
}

bool DrumFxClick(Click click, int32_t direction) {
  int32_t slot, h;
  int16_t* values = EditedDrum(&slot, &h);
  if (!values) return false;
  bool changed;
  const bool consumed = HostClick(h, slot, POSITION_MONO, values, click, direction, &changed);
  if (changed) Save(slot, values);
  return consumed;
}

// The master's values are the live array its state is kept in; its knob
// case writes them the same way, with no save call of its own.
bool MasterFxClick(Click click, int32_t direction) {
  int32_t h;
  int16_t* values = EditedMaster(&h);
  if (!values) return false;
  bool changed;
  return HostClick(h, kOwnerMaster, POSITION_MASTER, values, click, direction, &changed);
}

}  // namespace op1fx

extern "C" void FxReverbMono(void* self, int16_t* buf, const int16_t* params) {
  Mono(HOST_REVERB, self, buf, params);
}

extern "C" void FxReverbStereo(void* self, int16_t* l, int16_t* r, const int16_t* params) {
  Stereo(HOST_REVERB, self, l, r, params);
}

extern "C" void FxDirtMono(void* self, int16_t* buf, const int16_t* params) {
  Mono(HOST_DIRT, self, buf, params);
}

extern "C" void FxDirtStereo(void* self, int16_t* l, int16_t* r, const int16_t* params) {
  Stereo(HOST_DIRT, self, l, r, params);
}

extern "C" void FxModulationMono(void* self, int16_t* buf, const int16_t* params) {
  Mono(HOST_MODULATION, self, buf, params);
}

extern "C" void FxModulationStereo(void* self, int16_t* l, int16_t* r, const int16_t* params) {
  Stereo(HOST_MODULATION, self, l, r, params);
}

extern "C" void FxDelayMono(void* self, int16_t* buf, const int16_t* params) {
  Mono(HOST_DELAY, self, buf, params);
}

extern "C" void FxDelayStereo(void* self, int16_t* l, int16_t* r, const int16_t* params) {
  Stereo(HOST_DELAY, self, l, r, params);
}

#ifndef OP1_SIM
// Screen::LoadAll calls Init once at boot, after loading the stock SVG.
extern "C" void FxReverbScreenInit(void* self) { ScreenInit(HOST_REVERB, self); }

extern "C" void FxReverbScreenUpdate(void* self, int32_t ticks) {
  ScreenUpdate(HOST_REVERB, self, ticks);
}

extern "C" void FxDirtScreenInit(void* self) { ScreenInit(HOST_DIRT, self); }

extern "C" void FxDirtScreenUpdate(void* self, int32_t ticks) { ScreenUpdate(HOST_DIRT, self, ticks); }

extern "C" void FxModulationScreenInit(void* self) { ScreenInit(HOST_MODULATION, self); }

extern "C" void FxModulationScreenUpdate(void* self, int32_t ticks) {
  ScreenUpdate(HOST_MODULATION, self, ticks);
}

extern "C" void FxDelayScreenInit(void* self) { ScreenInit(HOST_DELAY, self); }

extern "C" void FxDelayScreenUpdate(void* self, int32_t ticks) {
  ScreenUpdate(HOST_DELAY, self, ticks);
}
#endif
