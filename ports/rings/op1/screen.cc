// The MULTI engine's screen on the OP-1: the phase engine's screen
// (PdScreen, docs/screen-interface.md) shows the stock phase drawing or
// Rings', by the displayed slot's engine (knobs[6], op1/settings.h):
//   - the PdScreen still loads "pd.svg" as its document; our Init (PdScreen
//     vtable +0x24) runs the stock Init on it, then loads "rings.svg"
//     (content/display/rings.svg, made from docs/module-briefs/designs/
//     rings.svg by ports/fx/op1/gen_fx_svg.py, layout in rings.md) with
//     the firmware's own SVG loader, finds our elements and reads the
//     colours;
//   - Update (+0x1c) runs once per frame while the screen shows: it puts the
//     engine's document in place (flushing the incoming one's cached
//     drawing, as Screen::UpdateAll does when the screen changes), then
//     runs the stock Update, or sets our elements from the knob values and
//     draws.
//
// Colours are copied from the SVG's own elements in their default state
// (ring0 lit, ring5 dim, model0 lit, model1 not, voice0 lit, voice5 not), so
// the style's byte order doesn't matter.
//
// The blue model dots and the green voice ticks show the slot's knobs[4] and
// knobs[5] (op1/settings.h): one dot lit for the model, 1, 2, 4 or 6 ticks
// lit for the polyphony. The orange engine dots (eng0 phase, eng1 rings) are
// drawn with rings lit and never change: this drawing only shows while
// Rings plays.
//
// The four encoder drawings follow the LFO, as stock screens do: they show
// the knobs after the LFO's modulation (ModulatedKnobs below), and the
// live knobs when no fresh modulated values are coming (no voice playing).

#include "op1/screen.h"

#include <cstdint>

#ifdef OP1_CPU_PROBE
#include "op1/cpu_probe.h"
#endif
#include "op1/op1.h"
#include "op1/screen_math.h"
#include "op1/settings.h"

namespace {

// Firmware (docs/screen-interface.md §2-§4).
const uint32_t kNodeFind = 0x019eb8ac;        // (document, id, assert) -> node
const uint32_t kGetLineStyle = 0x01a3b854;    // (screen, id) -> RGBA bytes
const uint32_t kGetFillStyle = 0x01a3b96c;
const uint32_t kSetTransform = 0x019ead58;    // (node, const int32 m[9])
const uint32_t kScreenDraw = 0x01a3b4c8;      // (screen)
const uint32_t kStockInit = 0x01a0cca0;       // PdScreen Init (this)
const uint32_t kStockUpdate = 0x01a0c0a0;     // PdScreen Update (this, ticks)
const uint32_t kNew = 0x01a5e3b0;             // operator new (size)
const uint32_t kSvgFileNew = 0x01a50350;      // SvgFile::SvgFile (this, name) -> this
const uint32_t kSvgFileLoad = 0x01a503d8;     // (svg file) -> parsed document
const uint32_t kDisplayedSlot = 0x01df4e38;   // synth instance on screen
const uint32_t kKnobs = 0xff8013c4;           // int16 knobs[8] per instance
const int32_t kNumSlots = 8;
const uint32_t kNodeVisible = 0x50;           // byte in a Node
const uint32_t kSlotTypes = 0xff8016e4;       // engine type per slot
// The knobs after the LFO, as stock screens read them (ReadInterface
// 0x1a28210): the Core publishes the rendered voice's modulated knobs with
// its slot and engine type (0xff8010b8, new-flag B[0xff8010d4]); TakeRecord
// copies a new record into the one stock keeps (0x1ab321c) and returns 1.
// ReadInterface uses it when slot and type match and its flag is 0.
const uint32_t kTakeRecord = 0x0196749c;      // (unused, record*) -> 1 if new
const uint32_t kRecord = 0x01ab321c;
const int32_t kStaleFrames = 8;               // then the live knobs again

typedef void* (*NodeFindFn)(void* document, const char* id, int32_t assert_missing);
typedef uint8_t* (*StyleFn)(void* screen, const char* id);
typedef void (*SetTransformFn)(void* node, const int32_t* m);
typedef void (*DrawFn)(void* screen);
typedef void (*InitFn)(void* screen);
typedef void (*UpdateFn)(void* screen, int32_t ticks);
typedef void* (*NewFn)(uint32_t size);
typedef void* (*SvgFileNewFn)(void* file, const char* name);
typedef void* (*SvgFileLoadFn)(void* file);
typedef int32_t (*TakeRecordFn)(int32_t unused, void* record);

struct Record {         // as ReadInterface reads it
  int16_t knobs[8];
  int32_t slot;
  int32_t type;
  int32_t flag;
};

// Screen object (Screen::Screen 0x1a3bab0): +0 the document, +8 its SvgFile.
void*& DocumentOf(void* screen) { return *reinterpret_cast<void**>(screen); }

// The firmware's SVG load, as Screen::Screen and Screen::LoadAll do it.
void* LoadDocument(const char* name) {
  void* file = reinterpret_cast<NewFn>(kNew)(8);
  if (!file) return nullptr;
  file = reinterpret_cast<SvgFileNewFn>(kSvgFileNew)(file, name);
  return file ? reinterpret_cast<SvgFileLoadFn>(kSvgFileLoad)(file) : nullptr;
}

// What Screen::UpdateAll calls on every screen's document when the screen
// changes (0x1a3bb9a): the document's vtable (at +0x54) slot +0x7c, with its
// this adjustment at +0x78. It seems to drop cached drawing.
void Flush(void* document) {
  const uint8_t* vtable =
      *reinterpret_cast<uint8_t* const*>(reinterpret_cast<uintptr_t>(document) + 0x54);
  const int16_t adjust = *reinterpret_cast<const int16_t*>(vtable + 0x78);
  void* fn = *reinterpret_cast<void* const*>(vtable + 0x7c);
  reinterpret_cast<InitFn>(fn)(reinterpret_cast<uint8_t*>(document) + adjust);
}

void* Find(void* document, const char* id) {
  return reinterpret_cast<NodeFindFn>(kNodeFind)(document, id, 0);
}
uint8_t* LineStyle(void* screen, const char* id) {
  return reinterpret_cast<StyleFn>(kGetLineStyle)(screen, id);
}
uint8_t* FillStyle(void* screen, const char* id) {
  return reinterpret_cast<StyleFn>(kGetFillStyle)(screen, id);
}

void SetTranslate(void* node, int32_t tx, int32_t ty) {
  int32_t m[9] = { 0x400, 0, 0, 0, 0x400, 0, tx, ty, 0x400 };
  reinterpret_cast<SetTransformFn>(kSetTransform)(node, m);
}

// Scale y by s (Q10) about y = anchor (pixels): matrix(1,0,0,s,0,anchor(1-s)).
void ScaleY(void* node, int32_t s, int32_t anchor) {
  int32_t m[9] = { 0x400, 0, 0, 0, s, 0, 0, anchor * (0x400 - s), 0x400 };
  reinterpret_cast<SetTransformFn>(kSetTransform)(node, m);
}

void SetVisible(void* node, bool visible) {
  *reinterpret_cast<volatile uint8_t*>(reinterpret_cast<uintptr_t>(node) + kNodeVisible) =
      visible ? 1 : 0;
}

struct Colour {
  uint8_t rgba[4];
};

void Read(Colour* c, const uint8_t* style) {
  for (int32_t i = 0; i < 4; ++i) c->rgba[i] = style[i];
}

void Write(uint8_t* style, const Colour& c) {
  for (int32_t i = 0; i < 4; ++i) style[i] = c.rgba[i];
}

const char* const kRingIds[6] = { "ring0", "ring1", "ring2", "ring3", "ring4", "ring5" };
const char* const kModelIds[6] = { "model0", "model1", "model2", "model3", "model4", "model5" };
const char* const kVoiceIds[6] = { "voice0", "voice1", "voice2", "voice3", "voice4", "voice5" };
const char* const kNodeIds[3] = { "brightness", "damping", "posdot" };
const char* const kPluckIds[op1::kPluckVariants] = {
  "pluck0", "pluck1", "pluck2", "pluck3", "pluck4", "pluck5", "pluck6", "pluck7",
  "pluck8", "pluck9", "pluck10", "pluck11", "pluck12", "pluck13", "pluck14", "pluck15",
  "pluck16", "pluck17", "pluck18", "pluck19", "pluck20", "pluck21", "pluck22", "pluck23" };

bool ready;
uint8_t* ring_styles[6];
uint8_t* model_line_styles[6];
uint8_t* model_fill_styles[6];
uint8_t* voice_styles[6];
void* nodes[3];                         // brightness, damping, posdot
void* pluck_nodes[op1::kPluckVariants];
Colour ring_on, ring_off;
Colour model_line_on, model_line_off, model_fill_on, model_fill_off;
Colour voice_on, voice_off;

bool AllPresent(void* document, const char* const* ids, int32_t n) {
  for (int32_t i = 0; i < n; ++i) {
    if (!Find(document, ids[i])) return false;
  }
  return true;
}

void* pd_document;      // the stock drawing
void* rings_document;   // ours, or null if it didn't load
void* shown_document;   // the one last put in place

// Our elements, with self's document = rings.svg.
void RingsInit(void* self) {
  ready = false;
  void* document = DocumentOf(self);
  if (!document) return;
  // GetLineStyle asserts on a missing id: check them all first, and leave
  // the screen static rather than crash if the SVG is wrong.
  if (!AllPresent(document, kRingIds, 6) || !AllPresent(document, kModelIds, 6) ||
      !AllPresent(document, kVoiceIds, 6) || !AllPresent(document, kNodeIds, 3) ||
      !AllPresent(document, kPluckIds, op1::kPluckVariants)) {
    return;
  }
  for (int32_t i = 0; i < 6; ++i) {
    ring_styles[i] = LineStyle(self, kRingIds[i]);
    // The model dots are circles: lit fills them too.
    model_line_styles[i] = LineStyle(self, kModelIds[i]);
    model_fill_styles[i] = FillStyle(self, kModelIds[i]);
    voice_styles[i] = LineStyle(self, kVoiceIds[i]);
  }
  for (int32_t i = 0; i < 3; ++i) nodes[i] = Find(document, kNodeIds[i]);
  for (int32_t i = 0; i < op1::kPluckVariants; ++i) pluck_nodes[i] = Find(document, kPluckIds[i]);

  // The SVG's defaults: ring0 lit, ring5 dim; model0 lit, model1 not;
  // voice0 lit, voice5 not.
  Read(&ring_on, ring_styles[0]);
  Read(&ring_off, ring_styles[5]);
  Read(&model_line_on, model_line_styles[0]);
  Read(&model_line_off, model_line_styles[1]);
  Read(&model_fill_on, model_fill_styles[0]);
  Read(&model_fill_off, model_fill_styles[1]);
  Read(&voice_on, voice_styles[0]);
  Read(&voice_off, voice_styles[5]);
  ready = true;
}

int32_t frames_since_record = kStaleFrames;

// The slot's knobs as the screen shows them: 0..3 after the LFO while
// fresh records for this slot come in (one per frame is plenty), else as
// set; 4..7 always as set (our settings, which the LFO doesn't touch).
void ModulatedKnobs(int32_t slot, const int16_t* knobs, int16_t* shown) {
  Record* record = reinterpret_cast<Record*>(kRecord);
  if (reinterpret_cast<TakeRecordFn>(kTakeRecord)(0, record)) {
    frames_since_record = 0;
  } else if (frames_since_record < kStaleFrames) {
    ++frames_since_record;
  }
  const bool modulated = frames_since_record < kStaleFrames && record->slot == slot &&
                         record->type == *reinterpret_cast<const volatile int32_t*>(kSlotTypes + slot * 4) &&
                         record->flag == 0;
  for (int32_t k = 0; k < 8; ++k) shown[k] = modulated && k < 4 ? record->knobs[k] : knobs[k];
}

void RingsUpdate(void* self, const int16_t* knobs) {
  if (ready) {
    const op1::ScreenState s = op1::ComputeScreenState(knobs);
    for (int32_t i = 0; i < 6; ++i) {
      Write(ring_styles[i], i < s.lit_rings ? ring_on : ring_off);
    }
    // The bars grow up from their foot; posdot rides the string.
    ScaleY(nodes[0], s.brightness_sy, op1::kBarFoot);
    ScaleY(nodes[1], s.damping_sy, op1::kBarFoot);
    SetTranslate(nodes[2], s.posdot_tx, 0);
    for (int32_t i = 0; i < op1::kPluckVariants; ++i) SetVisible(pluck_nodes[i], i == s.pluck);
    const int32_t model = op1::ModelSetting(knobs);
    for (int32_t i = 0; i < 6; ++i) {
      const bool on = i == model;
      Write(model_line_styles[i], on ? model_line_on : model_line_off);
      Write(model_fill_styles[i], on ? model_fill_on : model_fill_off);
    }
    // Ticks lit = the voices the polyphony allows: 1, 2, 4, or 6 for "all".
    const int32_t voices = op1::kPolyVoices[op1::PolySetting(knobs)];
    for (int32_t i = 0; i < 6; ++i) Write(voice_styles[i], i < voices ? voice_on : voice_off);
  }
  // Update must draw (Screen::UpdateAll doesn't).
  reinterpret_cast<DrawFn>(kScreenDraw)(self);
}

}  // namespace

// Screen::LoadAll calls Init once at boot, after loading pd.svg.
extern "C" void MultiScreenInit(void* self) {
  pd_document = DocumentOf(self);
  shown_document = pd_document;
  if (pd_document) reinterpret_cast<InitFn>(kStockInit)(self);
  rings_document = LoadDocument(op1::kRingsSvgName);
  if (rings_document) {
    DocumentOf(self) = rings_document;
    RingsInit(self);
    DocumentOf(self) = pd_document;
  }
#ifdef OP1_CPU_PROBE
  op1cpu::Init();
#endif
}

extern "C" void MultiScreenUpdate(void* self, int32_t ticks) {
  const int32_t slot = *reinterpret_cast<const volatile int32_t*>(kDisplayedSlot);
  const int16_t* knobs =
      slot >= 0 && slot < kNumSlots ? reinterpret_cast<const int16_t*>(kKnobs + slot * 16) : nullptr;
  const bool rings = knobs && rings_document && op1::EngineSetting(knobs) == op1::ENGINE_RINGS;
  void* document = rings ? rings_document : pd_document;
#ifdef OP1_CPU_PROBE
  // The readout in place of Rings' drawing (white click, op1/input.cc).
  op1cpu::Frame();
  void* readout = rings ? op1cpu::Document() : nullptr;
  if (readout) document = readout;
#endif
  if (document != shown_document) {
    DocumentOf(self) = document;
    if (document) Flush(document);
    shown_document = document;
  }
#ifdef OP1_CPU_PROBE
  if (readout) {
    op1cpu::Draw(self);
    return;
  }
#endif
  if (rings) {
    int16_t shown[8];
    ModulatedKnobs(slot, knobs, shown);
    RingsUpdate(self, shown);
  } else if (pd_document) {
    reinterpret_cast<UpdateFn>(kStockUpdate)(self, ticks);
  }
}

namespace op1 {

// Our drawing, loaded by MultiScreenInit from content/display/.
const char kRingsSvgName[] = "rings.svg";

}  // namespace op1
