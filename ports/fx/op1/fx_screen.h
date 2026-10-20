// The firmware's screen and SVG calls the effect screens use
// (docs/screen-interface.md §2-§4): a hosted engine's screen is a second
// document swapped into the stock effect's Screen object (fx_host.cc), the
// way the MULTI screen swaps pd.svg and rings.svg.

#ifndef OP1FX_OP1_FX_SCREEN_H_
#define OP1FX_OP1_FX_SCREEN_H_

#include <cstdint>

namespace op1fx {
namespace screen {

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

inline void*& DocumentOf(void* screen) { return *reinterpret_cast<void**>(screen); }

inline void* LoadDocument(const char* name) {
  void* file = reinterpret_cast<NewFn>(kNew)(8);
  if (!file) return nullptr;
  file = reinterpret_cast<SvgFileNewFn>(kSvgFileNew)(file, name);
  return file ? reinterpret_cast<SvgFileLoadFn>(kSvgFileLoad)(file) : nullptr;
}

// Screen::UpdateAll's flush of a document's cached drawing (screen.cc).
inline void Flush(void* document) {
  const uint8_t* vtable =
      *reinterpret_cast<uint8_t* const*>(reinterpret_cast<uintptr_t>(document) + 0x54);
  const int16_t adjust = *reinterpret_cast<const int16_t*>(vtable + 0x78);
  void* fn = *reinterpret_cast<void* const*>(vtable + 0x7c);
  reinterpret_cast<InitFn>(fn)(reinterpret_cast<uint8_t*>(document) + adjust);
}

inline void Draw(void* screen) { reinterpret_cast<DrawFn>(kScreenDraw)(screen); }

inline void* Find(void* document, const char* id) {
  return reinterpret_cast<NodeFindFn>(kNodeFind)(document, id, 0);
}

inline void SetTranslate(void* node, int32_t tx_q10, int32_t ty_q10) {
  int32_t m[9] = { 0x400, 0, 0, 0, 0x400, 0, tx_q10, ty_q10, 0x400 };
  reinterpret_cast<SetTransformFn>(kSetTransform)(node, m);
}

inline void SetVisible(void* node, bool visible) {
  *reinterpret_cast<volatile uint8_t*>(reinterpret_cast<uintptr_t>(node) + kNodeVisible) =
      visible ? 1 : 0;
}

inline uint8_t* LineStyle(void* screen, const char* id) {
  return reinterpret_cast<StyleFn>(kGetLineStyle)(screen, id);
}

inline uint8_t* FillStyle(void* screen, const char* id) {
  return reinterpret_cast<StyleFn>(kGetFillStyle)(screen, id);
}

// SVG's matrix(a, b, c, d, e, f), each Q10 (1.0 = 1024; e, f in pixels x
// 1024). The designs' notes give their transforms in this form.
inline void SetMatrix(void* node, int32_t a, int32_t d, int32_t e, int32_t f) {
  int32_t m[9] = { a, 0, 0, 0, d, 0, e, f, 0x400 };
  reinterpret_cast<SetTransformFn>(kSetTransform)(node, m);
}

// Scale x by s (Q10) about x = anchor (pixels): matrix(s,0,0,1,anchor(1-s),0).
inline void ScaleX(void* node, int32_t s, int32_t anchor) {
  SetMatrix(node, s, 0x400, anchor * (0x400 - s), 0);
}

// Every node of a set found (GetLineStyle asserts on a missing id: a screen
// checks its ids before using any).
inline bool FindAll(void* document, const char* const* ids, void** out, int32_t n) {
  for (int32_t i = 0; i < n; ++i) {
    out[i] = Find(document, ids[i]);
    if (!out[i]) return false;
  }
  return true;
}

// A variant set: only member k shown (none for k out of range).
inline void ShowOne(void* const* set, int32_t n, int32_t k) {
  for (int32_t i = 0; i < n; ++i) SetVisible(set[i], i == k);
}

// A count set: the first `count` shown.
inline void ShowFirst(void* const* set, int32_t n, int32_t count) {
  for (int32_t i = 0; i < n; ++i) SetVisible(set[i], i < count);
}

// round(v / 32767 x steps), v in 0..32767.
inline int32_t Steps(int32_t v, int32_t steps) { return (v * steps + 16383) / 32767; }

struct Colour {
  uint8_t rgba[4];
};

inline void ReadColour(Colour* c, const uint8_t* style) {
  for (int32_t i = 0; i < 4; ++i) c->rgba[i] = style[i];
}

inline void WriteColour(uint8_t* style, const Colour& c) {
  for (int32_t i = 0; i < 4; ++i) style[i] = c.rgba[i];
}

// The orange engine dots of a category's screens (eng0..engN-1, one per
// engine of the category, stock first), lit by the shown engine. The file
// draws its own engine's dot lit and the others dim: Init reads both
// styles from there. Line and fill are set apart (a circle has both).
struct EngineDots {
  static const int32_t kMax = 8;
  int32_t n;
  uint8_t* line[kMax];
  uint8_t* fill[kMax];
  Colour line_on, line_off, fill_on, fill_off;

  // `own`: this screen's engine index (its lit dot in the file); false if
  // an id is missing.
  bool Init(void* screen, int32_t count, int32_t own) {
    static const char* const kIds[kMax] = { "eng0", "eng1", "eng2", "eng3",
                                            "eng4", "eng5", "eng6", "eng7" };
    n = count;
    void* document = DocumentOf(screen);
    for (int32_t i = 0; i < n; ++i) {
      if (!Find(document, kIds[i])) return false;
    }
    for (int32_t i = 0; i < n; ++i) {
      line[i] = LineStyle(screen, kIds[i]);
      fill[i] = FillStyle(screen, kIds[i]);
    }
    const int32_t other = own == 0 ? 1 : 0;
    ReadColour(&line_on, line[own]);
    ReadColour(&fill_on, fill[own]);
    ReadColour(&line_off, line[other]);
    ReadColour(&fill_off, fill[other]);
    return true;
  }

  void Show(int32_t lit) const {
    for (int32_t i = 0; i < n; ++i) {
      WriteColour(line[i], i == lit ? line_on : line_off);
      WriteColour(fill[i], i == lit ? fill_on : fill_off);
    }
  }
};

// The mix bar the designs share: rect x 232, 56 px wide, scaled from its
// left end, u = v / 32767 (at least 0.02 so it stays visible).
inline void SetMixBar(void* node, int32_t v15) {
  int32_t s = v15 >> 5;   // Q10
  if (s < 20) s = 20;
  ScaleX(node, s, 232);
}

}  // namespace screen
}  // namespace op1fx

#endif  // OP1FX_OP1_FX_SCREEN_H_
