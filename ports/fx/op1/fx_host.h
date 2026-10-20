// Effect categories on the OP-1 (docs/fx-categories.md): a stock effect
// type hosts engines of its kind, cycled by an orange click on the FX page
// (shift: back), as the MULTI engine does for phase and Rings. The stock
// effect is engine 0 of its category.
//
//   reverb       spring  -> plate, hall, clouds
//   modulation   cwo     -> ensemble, shift
//   dirt         punch   -> drive, tape, fold, crush
//   delay        delay   -> tape echo
//
// A host owns the type's process words (mono: the live synth slot or drum
// kit; stereo: the master), its screen's Init and Update, and the clicks on
// its FX pages; it calls the stock effect or the selected engine. Which
// engine plays is kept in the position's stored values (the marker below),
// saved with the preset.
//
// The marker: values[4] bits 12-14 = 6 (0x6000..0x6fff) = a hosted engine,
// bits 8-11 its id (1..15, per host). Factory presets hold 0, 4000 (bits
// 12-14 = 0) or 8000 (1) there, and stock code never writes values[4..7], so
// every stock preset stays stock. values[4] bits 0-7 and values[5..7] bits
// 0-14 are the engine's own. Every value stays within 0..32767, the FX
// Ranges of values[4..7] for every type: the synth patch check rejects a
// patch with an effect value out of its range (Test 37: fx1's marker used
// bit 15, a negative value, and a saved slot stopped the boot).

#ifndef OP1FX_OP1_FX_HOST_H_
#define OP1FX_OP1_FX_HOST_H_

#include <cstdint>

namespace op1fx {

// An effect's process (vtable +0x14). Mono: synth and drum position, in
// place, 64 samples; stereo: master, params at [SP+12] (docs/next-projects.md
// §1, Test 16).
typedef void (*FxMonoFn)(void* self, int16_t* buf, const int16_t* params);
typedef void (*FxStereoFn)(void* self, int16_t* l, int16_t* r, const int16_t* params);
const int32_t kFxBlockSize = 64;

// Cycles of the work the image measures (the synth engines' render and
// note-on, every hosted engine), a running total: the governor of each
// instance reads what the others took since its previous call.
extern uint32_t measured_cycles;

// Encoder clicks, in the order of op1/input.cc's key codes.
enum Click { CLICK_BLUE = 0, CLICK_GREEN = 1, CLICK_WHITE = 2, CLICK_ORANGE = 3 };

// The two positions an engine instance can play in.
enum Position { POSITION_MONO = 0, POSITION_MASTER = 1, kNumPositions = 2 };

// The screen's position codes ([0x1df4e3c]).
const int32_t kScreenSynth = 0;
const int32_t kScreenDrum = 1;
const int32_t kScreenMaster = 3;

// An encoder's range ({min, max} from the FX Ranges table) and the
// reciprocal that maps it onto 0..32767 without a division.
const int32_t kValueMax = 32767;

struct EncoderRange {
  int32_t min;
  int32_t max;
  uint32_t recip;   // (32767 << 15) / (max - min)
};

inline void InitRange(EncoderRange* r, int32_t min, int32_t max) {
  if (max <= min) {
    min = 0;
    max = kValueMax;
  }
  r->min = min;
  r->max = max;
  r->recip = (static_cast<uint32_t>(kValueMax) << 15) / static_cast<uint32_t>(max - min);
}

inline int32_t EncoderToValue(const EncoderRange& r, int32_t e) {
  if (e <= r.min) return 0;
  if (e >= r.max) return kValueMax;
  // (e - min) < 2^16 and recip < 2^17 (ranges are wider than 8192): the
  // product fits 32 unsigned bits.
  const int32_t v = static_cast<int32_t>((static_cast<uint32_t>(e - r.min) * r.recip + 16384) >> 15);
  return v > kValueMax ? kValueMax : v;
}

// The inverse, for a page swap (not on the audio path: it divides).
inline int16_t ValueToEncoder(const EncoderRange& r, int32_t v) {
  return static_cast<int16_t>(r.min + (v * (r.max - r.min) + kValueMax / 2) / kValueMax);
}

// The marker.
const int32_t kMarker = 0x6000;
const int32_t kMarkerMask = 0xf000;   // bit 15 (never set) and bits 12-14
const int32_t kEngineShift = 8;
const int32_t kEngineMask = 0xf;
const int32_t kEngineBits = 0x00ff;   // values[4]'s bits an engine owns
const int32_t kWordBits = 0x7fff;     // values[5..7]'s

inline int32_t EngineId(const int16_t* values) {
  const int32_t w = static_cast<uint16_t>(values[4]);
  return (w & kMarkerMask) == kMarker ? (w >> kEngineShift) & kEngineMask : 0;
}

inline void SetEngineId(int16_t* values, int32_t id) {
  const int32_t w = static_cast<uint16_t>(values[4]) & kEngineBits;
  values[4] = static_cast<int16_t>(kMarker | (id & kEngineMask) << kEngineShift | w);
}

// fx1's marker (Test 37): values[4] bit 15 set, bits 11-14 the engine id.
// Any negative values[4..7] is fx1's (or out of range): the host converts
// it (fx_host.cc Migrate).
inline bool IsLegacy(const int16_t* values) {
  return (values[4] | values[5] | values[6] | values[7]) < 0;
}
inline int32_t LegacyEngineId(const int16_t* values) {
  return values[4] < 0 ? (static_cast<uint16_t>(values[4]) >> 11) & kEngineMask : 0;
}

// What a hosted engine provides. position: POSITION_MONO or _MASTER; values:
// the position's 8 stored values; ranges: the host type's four encoder ranges.
// Every function is optional except process.
struct Engine {
  int32_t id;          // within its host, 1..15 (stable: saved in presets)
  const char* svg;     // its screen (content/display), or null: the stock screen
  // One call (64 samples; r is null for mono). live[0..3]: the encoders as
  // the call passes them (mono: the effect LFO's modulated copy at
  // 0xff801724; master: the stored values); values: the stored ones, for
  // the settings in [4..7] (the LFO moves the copy's [4..7] on spring).
  void (*process)(int32_t position, int32_t slot, int16_t* l, int16_t* r, const int16_t* live,
                  const int16_t* values, const EncoderRange* ranges);
  // fx1's layout (IsLegacy) converted in place to this engine's, or null.
  void (*migrate)(int16_t* values);
  // The position stopped playing this engine (another engine or stock took
  // over): the next process call must restart.
  void (*stop)(int32_t position);
  // A fresh start: the engine's bits of values[4..7] (the host sets the
  // marker afterwards).
  void (*enter)(int16_t* values, const EncoderRange* ranges);
  // A blue, green or white click: whether it was consumed, and in *changed
  // whether the values changed (to be saved).
  bool (*click)(int32_t position, int16_t* values, const EncoderRange* ranges, Click click,
                int32_t direction, bool* changed);
  // Screen: Init runs once at boot with the screen's document = svg's;
  // Update sets the elements for the shown position (kScreen*) each frame
  // (the host then draws). encoders[0..3]: as last played if that position
  // plays these values (the LFO's), else the stored ones.
  void (*screen_init)(void* screen);
  void (*screen_update)(void* screen, int32_t screen_position, const int16_t* encoders,
                        const int16_t* values, const EncoderRange* ranges);
};

// The engine index (0 = stock) of a host's engine id: its dot on the
// category's screens. The ids are the categories' full orders
// (docs/fx-categories.md), so the id is the index.
inline int32_t EngineDot(int32_t id) { return id; }

// Clicks on the FX pages (op1/input.cc): whether the click was consumed.
// orange always on a hosted type (the engine); the others while an engine
// plays and takes them.
bool SynthFxClick(Click click, int32_t direction);
bool DrumFxClick(Click click, int32_t direction);
bool MasterFxClick(Click click, int32_t direction);

#ifdef OP1_SIM
// The simulator's stand-ins: the synth FX page's slot values and the
// master's, both on the reverb host; and, if set, the encoder ranges every
// host uses (else each host's table fallback).
extern int16_t sim_synth_values[8];
extern int16_t sim_master_values[8];
extern const int16_t (*sim_ranges)[2];
#endif

}  // namespace op1fx

// The hosts' entries (ports/rings/op1/patches.cc): process words and screen
// Init/Update, per category.
extern "C" void FxReverbMono(void* self, int16_t* buf, const int16_t* params);
extern "C" void FxReverbStereo(void* self, int16_t* l, int16_t* r, const int16_t* params);
extern "C" void FxReverbScreenInit(void* self);
extern "C" void FxReverbScreenUpdate(void* self, int32_t ticks);
extern "C" void FxDirtMono(void* self, int16_t* buf, const int16_t* params);
extern "C" void FxDirtStereo(void* self, int16_t* l, int16_t* r, const int16_t* params);
extern "C" void FxDirtScreenInit(void* self);
extern "C" void FxDirtScreenUpdate(void* self, int32_t ticks);
extern "C" void FxModulationMono(void* self, int16_t* buf, const int16_t* params);
extern "C" void FxModulationStereo(void* self, int16_t* l, int16_t* r, const int16_t* params);
extern "C" void FxModulationScreenInit(void* self);
extern "C" void FxModulationScreenUpdate(void* self, int32_t ticks);
extern "C" void FxDelayMono(void* self, int16_t* buf, const int16_t* params);
extern "C" void FxDelayStereo(void* self, int16_t* l, int16_t* r, const int16_t* params);
extern "C" void FxDelayScreenInit(void* self);
extern "C" void FxDelayScreenUpdate(void* self, int32_t ticks);

namespace op1fx {

// The words those entries replace (docs/fx-categories.md).
const uint32_t kSpringMonoProcessSlot = 0x019113d0;
const uint32_t kSpringMonoProcess = 0x01a326e8;
const uint32_t kSpringStereoProcessSlot = 0x019113f0;
const uint32_t kSpringStereoProcess = 0x01a32ffc;
const uint32_t kSpringScreenUpdateSlot = 0x01911418;
const uint32_t kSpringScreenUpdate = 0x01a33f90;
const uint32_t kSpringScreenInitSlot = 0x01911420;
const uint32_t kSpringScreenInit = 0x01a33abc;
const uint32_t kPunchMonoProcessSlot = 0x01910c30;
const uint32_t kPunchMonoProcess = 0x019e6260;
const uint32_t kPunchStereoProcessSlot = 0x01910c50;
const uint32_t kPunchStereoProcess = 0x019e662c;
const uint32_t kPunchScreenUpdateSlot = 0x01910c78;
const uint32_t kPunchScreenUpdate = 0x019e6a18;
const uint32_t kPunchScreenInitSlot = 0x01910c80;
const uint32_t kPunchScreenInit = 0x019e6790;
const uint32_t kCwoMonoProcessSlot = 0x0190f588;
const uint32_t kCwoMonoProcess = 0x0194480c;
const uint32_t kCwoStereoProcessSlot = 0x0190f5a8;
const uint32_t kCwoStereoProcess = 0x019450fc;
const uint32_t kCwoScreenUpdateSlot = 0x0190f558;
const uint32_t kCwoScreenUpdate = 0x01942788;
const uint32_t kCwoScreenInitSlot = 0x0190f560;
const uint32_t kCwoScreenInit = 0x01943178;
const uint32_t kDelayMonoProcessSlot = 0x0190fb80;
const uint32_t kDelayMonoProcess = 0x0197d8c4;
const uint32_t kDelayStereoProcessSlot = 0x0190fba0;
const uint32_t kDelayStereoProcess = 0x0197dc42;
const uint32_t kDelayScreenUpdateSlot = 0x0190fc80;
const uint32_t kDelayScreenUpdate = 0x01984f3c;
const uint32_t kDelayScreenInitSlot = 0x0190fc88;
const uint32_t kDelayScreenInit = 0x01984a88;

}  // namespace op1fx

#endif  // OP1FX_OP1_FX_HOST_H_
