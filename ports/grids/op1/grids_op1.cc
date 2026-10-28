// Grids on the OP-1's pattern sequencer, for the drum instrument
// (docs/grids-port.md §6 and its "Decisions"; docs/sequencer-interface.md
// §12). Pattern's player, controller and screen words go to Grids
// (op1/grids_seq.*) while the drum instrument is live and Grids is on, and
// to stock pattern otherwise (synth pattern stays stock). The orange click
// on the pattern screen swaps Grids and stock pattern (Grids by default;
// RAM only, as the settings); shift + orange prints the last bar into
// stock pattern (16 sixteenths, saved by stock's own save path). Showing
// pattern as "multi" (GridsSeqName, -DOP1_GRIDS_RENAME) waits for a
// trampoline: the chooser's CALL can't reach region A directly.
//
// Threads, as stock: keys, knobs and clicks on the UI thread, Tick on the
// sequencer thread, the screen on the Gfx thread. Notes play from Tick and
// AllNotesOff. The note sink is built on the stack in each call (no static
// constructors in the image).
//
// Screen: the user's "Panel + kit" design (grids.svg) in place of
// pattern's while Grids plays the drums.
// Part of the Grids port; Grids (c) 2011-2012 Emilie Gillet, GPL v3.

#include "op1/grids_op1.h"

#include "op1/grids_seq.h"

#include "fx/op1/modules.h"

namespace {

using op1grids::GridsSeq;

// Firmware (docs/sequencer-interface.md §5, §12; docs/screen-interface.md §2).
const uint32_t kMakeMidiNote = 0x01a85014;    // (MidiNote*, key, velocity)
const uint32_t kNoteOn = 0x01a85750;          // (MidiNote*, 0)
const uint32_t kNoteOff = 0x01a85840;         // (MidiNote*)
const uint32_t kInstrument = 0x01de4c40;      // 0 synth, 1 drum
const uint32_t kPatternPlayer = 0x01de33d8;
const uint32_t kPatternData = 0x01a083c4;     // () -> the live instrument's PatternData
const uint32_t kPatternSetLength = 0x01a08030;  // (pd, length 1..16)
const uint32_t kPatternSetStart = 0x01a08080;   // (pd, start 0..15)
const uint32_t kPatternSave = 0x0193ef3c;     // (pd): mark dirty, wake the saver
const uint32_t kPatternNotes = 0x08;          // PatternData: 16 steps x 24 keys, a byte each
const uint32_t kShiftHeld = 0x01dd7f78;       // byte: MainInterface's shift flag
const uint32_t kNodeFind = 0x019eb8ac;
const uint32_t kScreenDraw = 0x01a3b4c8;
const uint32_t kNew = 0x01a5e3b0;
const uint32_t kSvgFileNew = 0x01a50350;
const uint32_t kSvgFileLoad = 0x01a503d8;
const uint32_t kNodeVisible = 0x50;

const int32_t kClickBlue = 0x3b;
const int32_t kClickGreen = 0x3c;
const int32_t kClickWhite = 0x3d;
const int32_t kClickOrange = 0x3e;

struct MidiNote {   // MakeMidiNote 0x1a85014
  int32_t note;
  int32_t velocity;
  uint8_t on;
  uint8_t pad[3];
  int32_t zero;
};

typedef void (*MakeMidiNoteFn)(MidiNote* n, int32_t key, int32_t velocity);
typedef void (*NoteOnFn)(MidiNote* n, int32_t flag);
typedef void (*NoteOffFn)(MidiNote* n);
typedef int32_t (*KeyDownFn)(void* player, int32_t key, int32_t flag);
typedef void (*KeyUpFn)(void* player, int32_t key);
typedef void (*PlayerFn)(void* player);
typedef void (*TickFn)(void* player, int32_t t);
typedef int32_t (*CtlKeyFn)(void* controller, int32_t key);
typedef void (*CtlKnobFn)(void* controller, int32_t k, int16_t* delta);
typedef uint8_t* (*PatternDataFn)();
typedef void (*PatternSetFn)(uint8_t* pd, int32_t value);
typedef void (*PatternSaveFn)(uint8_t* pd);
typedef const char* (*SeqNameFn)(int32_t index);

template <typename T>
T Read(uint32_t address) {
  return *reinterpret_cast<const volatile T*>(address);
}

GridsSeq seq;
bool seq_ready;
volatile bool grids_on = true;

GridsSeq* Seq() {
  if (!seq_ready) {
    seq.Init(0x21);
    seq_ready = true;
  }
  return &seq;
}

bool Drums() { return Read<int32_t>(kInstrument) == 1; }
bool Active() { return op1::ModuleEnabled(op1::MODULE_GRIDS) && grids_on && Drums(); }

// Drum keys through the firmware's calls (no octave in drum mode).
class OpOut : public op1grids::NoteOut {
 public:
  void NoteOn(int32_t key, int32_t velocity) override {
    MidiNote n;
    reinterpret_cast<MakeMidiNoteFn>(kMakeMidiNote)(&n, key, velocity);
    reinterpret_cast<NoteOnFn>(kNoteOn)(&n, 0);
  }
  void NoteOff(int32_t key) override {
    MidiNote n;
    reinterpret_cast<MakeMidiNoteFn>(kMakeMidiNote)(&n, key, 0);
    reinterpret_cast<NoteOffFn>(kNoteOff)(&n);
  }
};

// The last bar into stock pattern's drum data (§12): 16 sixteenths, all 24
// keys, length 16 from step 0, then stock's save. Accents can't be stored
// (pattern's notes are on/off).
void Print() {
  uint8_t* pd = reinterpret_cast<PatternDataFn>(kPatternData)();
  if (!pd) return;
  const uint32_t* bar = Seq()->bar();
  for (int32_t step = 0; step < op1grids::kPrintSteps; ++step) {
    for (int32_t key = 0; key < 24; ++key) {
      pd[kPatternNotes + step * 24 + key] = static_cast<uint8_t>((bar[step] >> key) & 1);
    }
  }
  reinterpret_cast<PatternSetFn>(kPatternSetLength)(pd, op1grids::kPrintSteps);
  reinterpret_cast<PatternSetFn>(kPatternSetStart)(pd, 0);
  reinterpret_cast<PatternSaveFn>(kPatternSave)(pd);
}

int32_t Click(int32_t key, int32_t direction) {
  if (!op1::ModuleEnabled(op1::MODULE_GRIDS)) return 0;   // switched off: stock pattern
  if (!Drums()) return 0;   // synth pattern: stock
  switch (key & 0xff) {
    case kClickOrange:
      if (direction < 0) {
        if (!grids_on) return 0;
        Print();
        return 1;
      }
      {
        // Silence the side going out; Grids restarts on the bar either way.
        if (!grids_on) {
          reinterpret_cast<PlayerFn>(op1grids::kStockAllNotesOff)(reinterpret_cast<void*>(kPatternPlayer));
        }
        OpOut out;
        Seq()->AllNotesOff(&out);
      }
      grids_on = !grids_on;
      return 1;
    case kClickBlue:
      if (!grids_on) return 0;
      Seq()->OnClick(op1grids::GRIDS_BLUE, direction);
      return 1;
    case kClickGreen:
      if (!grids_on) return 0;
      Seq()->OnClick(op1grids::GRIDS_GREEN, direction);
      return 1;
    case kClickWhite:
      if (!grids_on) return 0;
      Seq()->OnClick(op1grids::GRIDS_WHITE, direction);
      return 1;
    default:
      return 0;
  }
}

}  // namespace

extern "C" int32_t GridsKeyDown(void* player, int32_t key, int32_t flag) {
  if (!Active()) return reinterpret_cast<KeyDownFn>(op1grids::kStockKeyDown)(player, key, flag);
  Seq()->KeyDown(key);
  return 1;
}

extern "C" void GridsKeyUp(void* player, int32_t key) {
  if (!Active()) {
    reinterpret_cast<KeyUpFn>(op1grids::kStockKeyUp)(player, key);
    return;
  }
  Seq()->KeyUp(key);
}

extern "C" void GridsTick(void* player, int32_t t) {
  if (!Active()) {
    reinterpret_cast<TickFn>(op1grids::kStockTick)(player, t);
    return;
  }
  OpOut out;
  Seq()->set_shift_held(Read<uint8_t>(kShiftHeld) != 0);
  Seq()->Tick(t, &out);
}

extern "C" void GridsUiFrame(void* player, int32_t ticks) {
  if (!Active()) reinterpret_cast<TickFn>(op1grids::kStockUiFrame)(player, ticks);
}

extern "C" void GridsAllNotesOff(void* player) {
  if (seq_ready) {
    OpOut out;
    seq.AllNotesOff(&out);
  }
  reinterpret_cast<PlayerFn>(op1grids::kStockAllNotesOff)(player);
}

// While Grids is active, other keys skip stock's controller (its cursor and
// pattern.svg lookups would assert on grids.svg, Test 23) and fall through
// to SeqManager (0 = not consumed); the player hooks already got the keys.
extern "C" int32_t GridsCtlKeyDown(void* controller, int32_t key) {
  if (Click(key, 1)) return 1;
  if (Active()) return 0;
  return reinterpret_cast<CtlKeyFn>(op1grids::kStockCtlKeyDown)(controller, key);
}

extern "C" int32_t GridsCtlShiftKeyDown(void* controller, int32_t key) {
  if (Click(key, -1)) return 1;
  if (Active()) return 0;
  return reinterpret_cast<CtlKeyFn>(op1grids::kStockCtlShiftKeyDown)(controller, key);
}

extern "C" int32_t GridsCtlKeyUp(void* controller, int32_t key) {
  if (Active()) return 0;
  return reinterpret_cast<CtlKeyFn>(op1grids::kStockCtlKeyUp)(controller, key);
}

extern "C" void GridsCtlKnob(void* controller, int32_t k, int16_t* delta) {
  if (!Active()) {
    reinterpret_cast<CtlKnobFn>(op1grids::kStockCtlKnob)(controller, k, delta);
    return;
  }
  // A detent: raw ~1024 -> 1/64 of the range (as Marbles); k 4.. = shift
  // held: the fill page.
  Seq()->Knob(k < 8 ? k : (k & 3) + 4, *delta);
}

// "multi" in place of "pattern" where the chooser looks the current type up
// by its shown name (the list's query shows it as "multi" too).
extern "C" const char* GridsSeqName(int32_t index) {
  if (index == 0) return "multi";
  return reinterpret_cast<SeqNameFn>(op1grids::kStockSeqName)(index);
}

// The screen: stock pattern, or Grids' (grids.svg, the frameless redesign
// aligned with the fx screens: docs/module-briefs/designs/grids.md, made
// with ports/fx/op1/gen_fx_svg.py from docs/module-briefs/designs/grids.svg):
// the map with its dot, the kit pieces lighting on their hits (bigger on
// accents, lit while a part's key fills it), the assign arrow, the three bars
// beside the kit and the orange bar top right (the fill page while shift is
// held), the playhead and the state icons in the top strip. The engine dots
// (eng0 pattern, eng1 grids) are static. No driven element is inside a
// moved group: transforms are in screen coordinates; dot, assign and the
// state icons are groups, driven as a whole. Colours are read from the
// file's example state: part_hh lit, part_sd idle, rnd_on off.
namespace {

typedef void* (*NodeFindFn)(void* document, const char* id, int32_t assert_missing);
typedef uint8_t* (*StyleFn)(void* screen, const char* id);
typedef void (*SetTransformFn)(void* node, const int32_t* m);
typedef void (*DrawFn)(void* screen);
typedef void (*InitFn)(void* screen);
typedef void (*UpdateFn)(void* screen, int32_t ticks);
typedef void* (*NewFn)(uint32_t size);
typedef void* (*SvgFileNewFn)(void* file, const char* name);
typedef void* (*SvgFileLoadFn)(void* file);

const uint32_t kGetLineStyle = 0x01a3b854;
const uint32_t kGetFillStyle = 0x01a3b96c;
const uint32_t kSetTransform = 0x019ead58;    // (node, const int32 m[9]), Q10

const char* const kNodeIds[] = {
  "dot", "assign", "ind0", "ind1", "ind2", "ind3", "playhead", "mode_drums", "mode_euclid",
  "res16", "res32", "accent_vel", "accent_key", "shift" };
enum {
  N_DOT, N_ASSIGN, N_IND0, N_PLAYHEAD = N_IND0 + 4, N_MODE_DRUMS, N_MODE_EUCLID, N_RES16, N_RES32,
  N_ACCENT_VEL, N_ACCENT_KEY, N_SHIFT, N_COUNT };
static_assert(sizeof(kNodeIds) / sizeof(kNodeIds[0]) == N_COUNT, "node ids");
// The kit pieces in part order (BD, SD, HH), their centres' y, and the
// assign arrow's offset towards each (drawn pointing at HH).
const char* const kPartIds[3] = { "part_bd", "part_sd", "part_hh" };
const int32_t kPartCentreY[3] = { 107, 74, 42 };
const int32_t kPartCentreX = 150;
// The bars' left ends: ind0..ind2 beside the kit, ind3 the orange bar.
const int32_t kBarX[4] = { 200, 200, 200, 232 };
const int32_t kAssignDy[3] = { 65, 32, 0 };

struct Colour {
  uint8_t rgba[4];
};

// The old gen_grids_svg.py split a path with several subpaths into one path
// per subpath (the OP-1 joins subpaths with a line): id, idP2, idP3, ...
// The redesign draws those as groups instead, so each id is one node (its
// group) and no P2 exists; the lookup stays for any older file.
const int32_t kMaxParts = 9;

struct Element {
  void* part[kMaxParts];
  int32_t count;
};

void* stock_document;
void* grids_document;
void* shown_document;
bool screen_ready;
Element nodes[N_COUNT];
void* parts[3];
uint8_t* part_line[3];
uint8_t* part_fill[3];
uint8_t* rnd_line[kMaxParts];
int32_t rnd_count;
Colour lit_line, lit_fill, idle_line, idle_fill;

void*& DocumentOf(void* screen) { return *reinterpret_cast<void**>(screen); }

void* LoadDocument(const char* name) {
  void* file = reinterpret_cast<NewFn>(kNew)(8);
  if (!file) return nullptr;
  file = reinterpret_cast<SvgFileNewFn>(kSvgFileNew)(file, name);
  return file ? reinterpret_cast<SvgFileLoadFn>(kSvgFileLoad)(file) : nullptr;
}

// Screen::UpdateAll's flush of a document's cached drawing (as
// ports/rings/op1/screen.cc).
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

// "id", then "idP2" .. "idP9" while they exist (no "_": the parser cuts
// "_..._" out of ids); false if "id" is missing.
// names receives each part's id (for the style lookups).
bool FindParts(void* document, const char* id, Element* e, char (*names)[24] = nullptr) {
  char name[24];
  int32_t n = 0;
  while (id[n] && n < 20) {
    name[n] = id[n];
    ++n;
  }
  name[n] = 0;
  e->count = 0;
  for (int32_t i = 0; i < kMaxParts; ++i) {
    if (i) {
      name[n] = 'P';
      name[n + 1] = static_cast<char>('1' + i);
      name[n + 2] = 0;
    }
    void* node = Find(document, name);
    if (!node) break;
    if (names) {
      for (int32_t c = 0; c < 24; ++c) names[i][c] = name[c];
    }
    e->part[e->count++] = node;
  }
  return e->count > 0;
}

void SetVisible(void* node, bool visible) {
  *reinterpret_cast<volatile uint8_t*>(reinterpret_cast<uintptr_t>(node) + kNodeVisible) =
      visible ? 1 : 0;
}

void SetVisible(const Element& e, bool visible) {
  for (int32_t i = 0; i < e.count; ++i) SetVisible(e.part[i], visible);
}

// Scale (Q10) about the local origin, then translate (Q10 px).
void SetTransform(void* node, int32_t sx, int32_t sy, int32_t tx, int32_t ty) {
  const int32_t m[9] = { sx, 0, 0, 0, sy, 0, tx, ty, 0x400 };
  reinterpret_cast<SetTransformFn>(kSetTransform)(node, m);
}

void SetTransform(const Element& e, int32_t sx, int32_t sy, int32_t tx, int32_t ty) {
  for (int32_t i = 0; i < e.count; ++i) SetTransform(e.part[i], sx, sy, tx, ty);
}

void Read(Colour* c, const uint8_t* style) {
  for (int32_t i = 0; i < 4; ++i) c->rgba[i] = style[i];
}

void Write(uint8_t* style, const Colour& c) {
  for (int32_t i = 0; i < 4; ++i) style[i] = c.rgba[i];
}

void GridsInit(void* self) {
  void* document = DocumentOf(self);
  // GetLineStyle asserts on a missing id: check them all first.
  for (int32_t i = 0; i < N_COUNT; ++i) {
    if (!FindParts(document, kNodeIds[i], &nodes[i])) return;
  }
  for (int32_t i = 0; i < 3; ++i) {
    parts[i] = Find(document, kPartIds[i]);
    if (!parts[i]) return;
  }
  Element rnd;
  char rnd_names[kMaxParts][24];
  if (!FindParts(document, "rnd_on", &rnd, rnd_names)) return;
  StyleFn line = reinterpret_cast<StyleFn>(kGetLineStyle);
  StyleFn fill = reinterpret_cast<StyleFn>(kGetFillStyle);
  for (int32_t i = 0; i < 3; ++i) {
    part_line[i] = line(self, kPartIds[i]);
    part_fill[i] = fill(self, kPartIds[i]);
  }
  for (int32_t i = 0; i < rnd.count; ++i) rnd_line[i] = line(self, rnd_names[i]);
  rnd_count = rnd.count;
  // The file's example state: part_hh lit, part_sd idle.
  Read(&lit_line, part_line[2]);
  Read(&lit_fill, part_fill[2]);
  Read(&idle_line, part_line[1]);
  Read(&idle_fill, part_fill[1]);
  screen_ready = true;
}

// A bar: u (Q16) as a width about its left end x0, at least 0.02.
void Bar(const Element& node, int32_t u_q16, int32_t x0) {
  int32_t s = u_q16 >> 6;   // Q10
  if (s < 20) s = 20;
  SetTransform(node, s, 0x400, x0 * (0x400 - s), 0);
}

void GridsDraw(void* self, const GridsSeq* g) {
  const op1grids::GridsUi& u = g->ui();
  const bool euclid = u.mode == op1grids::OUTPUT_MODE_EUCLIDEAN;
  const bool shift = Read<uint8_t>(kShiftHeld) != 0;

  // The map's dot (a crosshair drawn at 32,106): x 32 + 64 X, y 106 - 64 Y.
  SetVisible(nodes[N_DOT], !euclid);
  SetTransform(nodes[N_DOT], 0x400, 0x400, (u.x * 64) >> 6, -((u.y * 64) >> 6));

  // The kit: lit on the last step's hits and while a part's key is held;
  // 1.25 x about the centre on an accent.
  const uint8_t state = g->last_state();
  const uint32_t held = g->held_parts();
  for (int32_t p = 0; p < 3; ++p) {
    const bool hit = ((state >> p) & 1) || ((held >> p) & 1);
    Write(part_line[p], hit ? lit_line : idle_line);
    Write(part_fill[p], hit ? lit_fill : idle_fill);
    if ((state >> (p + 3)) & 1 && ((state >> p) & 1)) {
      SetTransform(parts[p], 1280, 1280, -256 * kPartCentreX, -256 * kPartCentreY[p]);   // c x -0.25
    } else {
      SetTransform(parts[p], 0x400, 0x400, 0, 0);
    }
  }
  // The assign arrow points at the part (or its accent) being assigned.
  const int32_t assign = u.assign;
  SetVisible(nodes[N_ASSIGN], assign != op1grids::ASSIGN_OFF);
  if (assign != op1grids::ASSIGN_OFF) {
    SetTransform(nodes[N_ASSIGN], 0x400, 0x400, 0, kAssignDy[(assign - 1) % 3] << 10);
  }

  // The bars: the fill page while shift is held.
  int32_t v[4];
  if (shift) {
    v[0] = u.fill[0];
    v[1] = u.fill[1];
    v[2] = u.fill[2];
    v[3] = u.accent;
  } else if (euclid) {
    v[0] = u.length[0];
    v[1] = u.length[1];
    v[2] = u.length[2];
    v[3] = u.swing;
  } else {
    v[0] = u.x;
    v[1] = u.y;
    v[2] = u.chaos;
    v[3] = u.swing;
  }
  for (int32_t i = 0; i < 4; ++i) Bar(nodes[N_IND0 + i], v[i], kBarX[i]);

  // The playhead: 16 px per sixteenth, 8 per thirty-second.
  const int32_t step_px = u.resolution == op1grids::RESOLUTION_16TH ? 16 : 8;
  SetTransform(nodes[N_PLAYHEAD], 0x400, 0x400, g->step() * step_px * 1024, 0);

  // State icons.
  bool accent_keys = false;
  for (int32_t i = 0; i < 3; ++i) accent_keys = accent_keys || u.accent_key[i] != op1grids::kNoKey;
  SetVisible(nodes[N_MODE_DRUMS], !euclid);
  SetVisible(nodes[N_MODE_EUCLID], euclid);
  SetVisible(nodes[N_RES16], u.resolution == op1grids::RESOLUTION_16TH);
  SetVisible(nodes[N_RES32], u.resolution != op1grids::RESOLUTION_16TH);
  SetVisible(nodes[N_ACCENT_VEL], !accent_keys);
  SetVisible(nodes[N_ACCENT_KEY], accent_keys);
  for (int32_t i = 0; i < rnd_count; ++i) Write(rnd_line[i], u.random_voice ? lit_line : idle_line);
  SetVisible(nodes[N_SHIFT], shift);

  reinterpret_cast<DrawFn>(kScreenDraw)(self);
}

}  // namespace

extern "C" void GridsScreenInit(void* self) {
  stock_document = DocumentOf(self);
  shown_document = stock_document;
  if (stock_document) reinterpret_cast<InitFn>(op1grids::kStockScreenInit)(self);
  grids_document = LoadDocument("grids.svg");
  if (grids_document) {
    DocumentOf(self) = grids_document;
    GridsInit(self);
    DocumentOf(self) = stock_document;
  }
}

extern "C" void GridsScreenUpdate(void* self, int32_t ticks) {
  const bool grids = Active() && screen_ready;
  void* document = grids ? grids_document : stock_document;
  if (document != shown_document) {
    DocumentOf(self) = document;
    if (document) Flush(document);
    shown_document = document;
  }
  if (grids) {
    GridsDraw(self, Seq());
  } else if (stock_document) {
    reinterpret_cast<UpdateFn>(op1grids::kStockScreenUpdate)(self, ticks);
  }
}
