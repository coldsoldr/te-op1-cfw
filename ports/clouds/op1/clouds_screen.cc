// Clouds' screen on the OP-1: the phone effect's screen (PhoneScreen,
// vtable Init +0x24 / Update +0x1c at 0x1911358 / 0x1911350) shows the
// stock phone drawing or Clouds', by the shown position's engine
// (op1/clouds_settings.h), the way the MULTI screen swaps pd.svg and
// rings.svg (ports/rings/op1/screen.cc):
//   - Init runs the stock Init on ptch.svg, then loads "clouds.svg"
//     (content/display/clouds.svg, made by op1/gen_clouds_svg.py from the
//     user's design) and finds its elements;
//   - Update puts the right document in place each frame, then runs the
//     stock Update or sets Clouds' elements and draws.
//
// The position on screen is the stock Update's: [0x1df4e3c] (0 synth,
// 1 drum, 3 master) and [0x1df4e38] (the slot or kit). The mappings are
// the design's (docs/clouds-screen-design.md). Colours are
// read from the SVG's own elements in their default state (mode0
// selected, mode1 not, mode3 disabled; page 1 lit, page 2 not).

#include <cstdint>

#include "op1/clouds_fx.h"
#include "src/core/params.h"

namespace {

// Firmware (docs/screen-interface.md §2-§4; the stock phone Update 0x1a1cfac).
const uint32_t kNodeFind = 0x019eb8ac;        // (document, id, assert) -> node
const uint32_t kGetLineStyle = 0x01a3b854;    // (screen, id) -> RGBA bytes
const uint32_t kGetFillStyle = 0x01a3b96c;
const uint32_t kSetTransform = 0x019ead58;    // (node, const int32 m[9])
const uint32_t kScreenDraw = 0x01a3b4c8;      // (screen)
const uint32_t kNew = 0x01a5e3b0;             // operator new (size)
const uint32_t kSvgFileNew = 0x01a50350;      // SvgFile::SvgFile (this, name) -> this
const uint32_t kSvgFileLoad = 0x01a503d8;     // (svg file) -> parsed document
const uint32_t kShownIndex = 0x01df4e38;      // the slot or kit on screen
const uint32_t kShownPosition = 0x01df4e3c;   // 0 synth, 1 drum, 3 master
const uint32_t kNodeVisible = 0x50;           // byte in a Node

typedef void* (*NodeFindFn)(void* document, const char* id, int32_t assert_missing);
typedef uint8_t* (*StyleFn)(void* screen, const char* id);
typedef void (*SetTransformFn)(void* node, const int32_t* m);
typedef void (*DrawFn)(void* screen);
typedef void (*InitFn)(void* screen);
typedef void (*UpdateFn)(void* screen, int32_t ticks);
typedef void* (*NewFn)(uint32_t size);
typedef void* (*SvgFileNewFn)(void* file, const char* name);
typedef void* (*SvgFileLoadFn)(void* file);

void*& DocumentOf(void* screen) { return *reinterpret_cast<void**>(screen); }

void* LoadDocument(const char* name) {
  void* file = reinterpret_cast<NewFn>(kNew)(8);
  if (!file) return nullptr;
  file = reinterpret_cast<SvgFileNewFn>(kSvgFileNew)(file, name);
  return file ? reinterpret_cast<SvgFileLoadFn>(kSvgFileLoad)(file) : nullptr;
}

// Screen::UpdateAll's flush of a document's cached drawing (screen.cc).
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

void SetTranslate(void* node, int32_t tx_q10, int32_t ty_q10) {
  int32_t m[9] = { 0x400, 0, 0, 0, 0x400, 0, tx_q10, ty_q10, 0x400 };
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

// Element ids (the design's README, "Id list").
const char* const kSizeIds[6] = { "size0", "size1", "size2", "size3", "size4", "size5" };
const char* const kRegIds[8] = { "reg0", "reg1", "reg2", "reg3", "reg4", "reg5", "reg6", "reg7" };
const char* const kRndIds[8] = { "rnd0", "rnd1", "rnd2", "rnd3", "rnd4", "rnd5", "rnd6", "rnd7" };
const char* const kTextureIds[5] = { "texture0", "texture1", "texture2", "texture3", "texture4" };
const char* const kWetIds[5] = { "wet0", "wet1", "wet2", "wet3", "wet4" };
const char* const kFbIds[5] = { "fb0", "fb1", "fb2", "fb3", "fb4" };
const char* const kRvIds[5] = { "rv0", "rv1", "rv2", "rv3", "rv4" };
const char* const kCpuIds[6] = { "cpu0", "cpu1", "cpu2", "cpu3", "cpu4", "cpu5" };
const char* const kModeBoxIds[4] = { "mode0_box", "mode1_box", "mode2_box", "mode3_box" };
const char* const kModeIconIds[4] = { "mode0_icon", "mode1_icon", "mode2_icon", "mode3_icon" };
const char* const kNodeIds[] = {
  "page1", "page2", "position", "pitch", "face", "face_frozen", "freeze_off", "freeze_on",
  "cpu", "mode3_off", "page1ind", "page2ind" };
enum {
  N_PAGE1, N_PAGE2, N_POSITION, N_PITCH, N_FACE, N_FACE_FROZEN, N_FREEZE_OFF, N_FREEZE_ON,
  N_CPU, N_MODE3_OFF, N_PAGE1IND, N_PAGE2IND, N_COUNT };
static_assert(sizeof(kNodeIds) / sizeof(kNodeIds[0]) == N_COUNT, "node ids");

bool ready;
void* nodes[N_COUNT];
void* size_nodes[6];
void* reg_nodes[8];
void* rnd_nodes[8];
void* texture_nodes[5];
void* wet_nodes[5];
void* fb_nodes[5];
void* rv_nodes[5];
void* cpu_nodes[6];
uint8_t* mode_box_styles[4];
uint8_t* mode_icon_styles[4];
uint8_t* page_ind_line[2];
uint8_t* page_ind_fill[2];
Colour box_on, box_off, icon_on, icon_off, disabled;
Colour ind_line_on, ind_line_off, ind_fill_on, ind_fill_off;

bool AllPresent(void* document, const char* const* ids, int32_t n) {
  for (int32_t i = 0; i < n; ++i) {
    if (!Find(document, ids[i])) return false;
  }
  return true;
}

void FindAll(void* document, const char* const* ids, void** out, int32_t n) {
  for (int32_t i = 0; i < n; ++i) out[i] = Find(document, ids[i]);
}

void* phone_document;    // the stock drawing
void* clouds_document;   // ours, or null if it didn't load
void* shown_document;

// Our elements, with self's document = clouds.svg.
void CloudsInit(void* self) {
  ready = false;
  void* document = DocumentOf(self);
  if (!document) return;
  // GetLineStyle asserts on a missing id: check them all first, and leave
  // the screen static rather than crash if the SVG is wrong.
  if (!AllPresent(document, kNodeIds, N_COUNT) || !AllPresent(document, kSizeIds, 6) ||
      !AllPresent(document, kRegIds, 8) || !AllPresent(document, kRndIds, 8) ||
      !AllPresent(document, kTextureIds, 5) || !AllPresent(document, kWetIds, 5) ||
      !AllPresent(document, kFbIds, 5) || !AllPresent(document, kRvIds, 5) ||
      !AllPresent(document, kCpuIds, 6) || !AllPresent(document, kModeBoxIds, 4) ||
      !AllPresent(document, kModeIconIds, 4)) {
    return;
  }
  FindAll(document, kNodeIds, nodes, N_COUNT);
  FindAll(document, kSizeIds, size_nodes, 6);
  FindAll(document, kRegIds, reg_nodes, 8);
  FindAll(document, kRndIds, rnd_nodes, 8);
  FindAll(document, kTextureIds, texture_nodes, 5);
  FindAll(document, kWetIds, wet_nodes, 5);
  FindAll(document, kFbIds, fb_nodes, 5);
  FindAll(document, kRvIds, rv_nodes, 5);
  FindAll(document, kCpuIds, cpu_nodes, 6);
  StyleFn line = reinterpret_cast<StyleFn>(kGetLineStyle);
  StyleFn fill = reinterpret_cast<StyleFn>(kGetFillStyle);
  for (int32_t i = 0; i < 4; ++i) {
    mode_box_styles[i] = line(self, kModeBoxIds[i]);
    // On a group, GetLineStyle gives every child the first child's style:
    // the icon is recoloured as one.
    mode_icon_styles[i] = line(self, kModeIconIds[i]);
  }
  page_ind_line[0] = line(self, "page1ind");
  page_ind_line[1] = line(self, "page2ind");
  page_ind_fill[0] = fill(self, "page1ind");
  page_ind_fill[1] = fill(self, "page2ind");   // #010101 (gen_clouds_svg.py)

  // The file's defaults: mode0 selected, mode1 not, mode3 disabled; page 1
  // lit, page 2 not.
  Read(&box_on, mode_box_styles[0]);
  Read(&icon_on, mode_icon_styles[0]);
  Read(&box_off, mode_box_styles[1]);
  Read(&icon_off, mode_icon_styles[1]);
  Read(&disabled, mode_icon_styles[3]);
  Read(&ind_line_on, page_ind_line[0]);
  Read(&ind_fill_on, page_ind_fill[0]);
  Read(&ind_line_off, page_ind_line[1]);
  Read(&ind_fill_off, page_ind_fill[1]);
  ready = true;
}

// round(v / 32767 * steps), v in 0..32767.
int32_t Steps(int32_t v, int32_t steps) {
  return (v * steps + 16383) / 32767;
}

void ShowOne(void* const* set, int32_t n, int32_t k) {
  for (int32_t i = 0; i < n; ++i) SetVisible(set[i], i == k);
}

void ShowFirst(void* const* set, int32_t n, int32_t count) {
  for (int32_t i = 0; i < n; ++i) SetVisible(set[i], i < count);
}

void CloudsUpdate(void* self, const op1fx::ScreenInfo& s) {
  if (ready) {
    const int32_t* v = s.values;
    const bool page2 = s.page == 1;
    SetVisible(nodes[N_PAGE1], !page2);
    SetVisible(nodes[N_PAGE2], page2);

    // Page 1. Position: x = (pos - 0.5) x 120 px; pitch: y = -semitones x
    // 0.625 px (Q10 translates: 120 x 1024 / 32767 = 15 / 4, 0.625 x 1024
    // / 65536 = 5 / 512).
    SetTranslate(nodes[N_POSITION], (v[0] - 16384) * 15 / 4, 0);
    SetTranslate(nodes[N_PITCH], 0, -((s.pitch_q16 >> 6) * 5) >> 3);
    ShowOne(size_nodes, 6, Steps(v[1], 5));
    // Density, bipolar: n = round(|d| x 8) drops, regular left, random right.
    const int32_t d = v[3] - 16384;
    const int32_t n = ((d < 0 ? -d : d) * 8 + 8192) / 16384;
    ShowFirst(reg_nodes, 8, d < 0 ? n : 0);
    ShowFirst(rnd_nodes, 8, d > 0 ? n : 0);
    SetVisible(nodes[N_FACE], !s.freeze);
    SetVisible(nodes[N_FACE_FROZEN], s.freeze);

    // Page 2.
    ShowOne(texture_nodes, 5, Steps(v[4], 4));
    ShowOne(wet_nodes, 5, v[5] == 0 ? -1 : Steps(v[5], 4));
    ShowFirst(fb_nodes, 5, Steps(v[6], 5));
    ShowFirst(rv_nodes, 5, Steps(v[7], 5));

    // Both pages.
    for (int32_t i = 0; i < 4; ++i) {
      if (i == op1clouds::MODE_SPECTRAL) {
        Write(mode_box_styles[i], s.mode == i ? box_on : box_off);
        Write(mode_icon_styles[i], s.mode == i ? icon_on : disabled);
      } else {
        Write(mode_box_styles[i], s.mode == i ? box_on : box_off);
        Write(mode_icon_styles[i], s.mode == i ? icon_on : icon_off);
      }
    }
    SetVisible(nodes[N_MODE3_OFF], s.mode != op1clouds::MODE_SPECTRAL);
    SetVisible(nodes[N_FREEZE_OFF], !s.freeze);
    SetVisible(nodes[N_FREEZE_ON], s.freeze);
    Write(page_ind_line[0], page2 ? ind_line_off : ind_line_on);
    Write(page_ind_fill[0], page2 ? ind_fill_off : ind_fill_on);
    Write(page_ind_line[1], page2 ? ind_line_on : ind_line_off);
    Write(page_ind_fill[1], page2 ? ind_fill_on : ind_fill_off);
    // CPU: Clouds' share of the call period, n = ceil(load x 6).
    SetVisible(nodes[N_CPU], true);
    ShowFirst(cpu_nodes, 6, (s.load_percent * 6 + 99) / 100);
  }
  // Update must draw (Screen::UpdateAll doesn't).
  reinterpret_cast<DrawFn>(kScreenDraw)(self);
}

}  // namespace

// Screen::LoadAll calls Init once at boot, after loading ptch.svg.
extern "C" void CloudsScreenInit(void* self) {
  phone_document = DocumentOf(self);
  shown_document = phone_document;
  if (phone_document) reinterpret_cast<InitFn>(op1fx::kStockPhoneScreenInit)(self);
  clouds_document = LoadDocument("clouds.svg");
  if (clouds_document) {
    DocumentOf(self) = clouds_document;
    CloudsInit(self);
    DocumentOf(self) = phone_document;
  }
}

extern "C" void CloudsScreenUpdate(void* self, int32_t ticks) {
  const int32_t position = *reinterpret_cast<const volatile int32_t*>(kShownPosition);
  const int32_t index = *reinterpret_cast<const volatile int32_t*>(kShownIndex);
  op1fx::ScreenInfo info;
  const bool clouds = clouds_document && op1fx::GetScreenInfo(position, index, &info);
  void* document = clouds ? clouds_document : phone_document;
  if (document != shown_document) {
    DocumentOf(self) = document;
    if (document) Flush(document);
    shown_document = document;
  }
  if (clouds) {
    CloudsUpdate(self, info);
  } else if (phone_document) {
    reinterpret_cast<UpdateFn>(op1fx::kStockPhoneScreenUpdate)(self, ticks);
  }
}
