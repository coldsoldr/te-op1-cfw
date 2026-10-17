// Marbles on the OP-1's tombola sequencer (docs/sequencer-interface.md §9,
// the user's choices in docs/marbles-port.md §9): tombola's player,
// controller and screen words go to Marbles (op1/marbles_seq.*) while it's
// on, and to stock tombola otherwise. The orange click on the tombola
// screen swaps them (Marbles by default; RAM only, as the settings).
//
// Threads, as stock tombola: keys and knobs arrive on the UI thread, Tick
// on the sequencer thread (96 per beat, Test 20), the screen on the Gfx
// thread. Notes are played from Tick (and from AllNotesOff, as tombola
// flushes its notes there). The note sink is built on the stack in each
// call: the image runs no static constructors, and NoteSink has a vtable.
//
// Screen: the user's Orbit design (marbles.svg) in place of tombola's while
// Marbles is on.

#include "op1/marbles_op1.h"

#include "op1/marbles_seq.h"

namespace {

using op1marbles::MarblesSeq;

// Firmware (docs/sequencer-interface.md §5; docs/screen-interface.md §2).
const uint32_t kMakeMidiNote = 0x01a85014;    // (MidiNote*, key, velocity)
const uint32_t kNoteOn = 0x01a85750;          // (MidiNote*, 0)
const uint32_t kNoteOff = 0x01a85840;         // (MidiNote*)
const uint32_t kTombolaPlayer = 0x01dfc254;
const uint32_t kRandomState = 0xff802bc8;     // the OP-1's LCG
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

// An encoder detent: raw deltas of ~1024 (the FX pages' ranges move ~1/60
// of their span per detent) -> 1/64 of the range; shift: 1/8 of that.
const int32_t kFineShift = 3;

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

template <typename T>
T Read(uint32_t address) {
  return *reinterpret_cast<const volatile T*>(address);
}

MarblesSeq seq;
bool seq_ready;
volatile bool marbles_on = true;
int16_t midi_note[256];       // per pitch + 64: the MIDI note it started as

MarblesSeq* Seq() {
  if (!seq_ready) {
    seq.Init(Read<uint32_t>(kRandomState) | 1);
    seq_ready = true;
  }
  return &seq;
}

// Notes through the firmware's calls, as tombola plays them. The MIDI note
// is kept per pitch, so a note-off matches its note-on across an octave
// change.
class OpSink : public op1marbles::NoteSink {
 public:
  void NoteOn(int32_t pitch, int32_t velocity) override {
    MidiNote n;
    reinterpret_cast<MakeMidiNoteFn>(kMakeMidiNote)(&n, 0, velocity);
    n.note += pitch;
    if (n.note < 0 || n.note > 127) {
      midi_note[pitch + 64] = -1;
      return;
    }
    midi_note[pitch + 64] = static_cast<int16_t>(n.note);
    reinterpret_cast<NoteOnFn>(kNoteOn)(&n, 0);
  }
  void NoteOff(int32_t pitch) override {
    const int32_t note = midi_note[pitch + 64];
    if (note < 0) return;
    MidiNote n;
    reinterpret_cast<MakeMidiNoteFn>(kMakeMidiNote)(&n, 0, 0);
    n.note = note;
    reinterpret_cast<NoteOffFn>(kNoteOff)(&n);
  }
};

int32_t Click(int32_t key, int32_t direction) {
  switch (key & 0xff) {
    case kClickOrange: {
      if (!marbles_on) {
        // Silence stock tombola's balls before Marbles takes over.
        reinterpret_cast<PlayerFn>(op1marbles::kStockAllNotesOff)(reinterpret_cast<void*>(kTombolaPlayer));
      } else {
        OpSink sink;
        Seq()->AllNotesOff(&sink);
      }
      marbles_on = !marbles_on;
      return 1;
    }
    case kClickBlue:
      if (!marbles_on) return 0;
      Seq()->OnClick(op1marbles::CLICK_BLUE, direction);
      return 1;
    case kClickGreen:
      if (!marbles_on) return 0;
      Seq()->OnClick(op1marbles::CLICK_GREEN, direction);
      return 1;
    case kClickWhite:
      if (!marbles_on) return 0;
      Seq()->OnClick(op1marbles::CLICK_WHITE, direction);
      return 1;
    default:
      return 0;
  }
}

}  // namespace

extern "C" int32_t MarblesKeyDown(void* player, int32_t key, int32_t flag) {
  if (!marbles_on) return reinterpret_cast<KeyDownFn>(op1marbles::kStockKeyDown)(player, key, flag);
  // flag = 1 with shift held (Test 20): played through, as tombola does.
  Seq()->KeyDown(key, flag != 0);
  return 1;
}

extern "C" void MarblesKeyUp(void* player, int32_t key) {
  if (!marbles_on) {
    reinterpret_cast<KeyUpFn>(op1marbles::kStockKeyUp)(player, key);
    return;
  }
  Seq()->KeyUp(key);
}

extern "C" void MarblesTick(void* player, int32_t t) {
  if (!marbles_on) {
    reinterpret_cast<TickFn>(op1marbles::kStockTick)(player, t);
    return;
  }
  OpSink sink;
  Seq()->Tick(t, &sink);
}

extern "C" void MarblesUiFrame(void* player, int32_t ticks) {
  if (!marbles_on) reinterpret_cast<TickFn>(op1marbles::kStockUiFrame)(player, ticks);
}

extern "C" void MarblesAllNotesOff(void* player) {
  if (seq_ready) {
    OpSink sink;
    seq.AllNotesOff(&sink);
  }
  reinterpret_cast<PlayerFn>(op1marbles::kStockAllNotesOff)(player);
}

extern "C" int32_t MarblesCtlKeyDown(void* controller, int32_t key) {
  if (Click(key, 1)) return 1;
  return reinterpret_cast<CtlKeyFn>(op1marbles::kStockCtlKeyDown)(controller, key);
}

extern "C" int32_t MarblesCtlShiftKeyDown(void* controller, int32_t key) {
  if (Click(key, -1)) return 1;
  return reinterpret_cast<CtlKeyFn>(op1marbles::kStockCtlShiftKeyDown)(controller, key);
}

extern "C" void MarblesCtlKnob(void* controller, int32_t k, int16_t* delta) {
  if (!marbles_on) {
    reinterpret_cast<CtlKnobFn>(op1marbles::kStockCtlKnob)(controller, k, delta);
    return;
  }
  const int32_t raw = *delta;
  // k 0..3: the encoders; 4.. with shift (as the synth pages): fine.
  Seq()->Knob(k & 3, k >= 4 ? raw >> kFineShift : raw);
}

// The screen: stock tombola, or Marbles' (marbles.svg, the user's Orbit
// design, docs/marbles-screen-brief.md; mappings in its handover README,
// kept as ports/marbles/op1/marbles-screen.md). Colours are read from the
// file's example state (gen_marbles_svg.py checks it): gate1 lit, gate3
// unlit, pageTind selected, pageXind not.
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

const uint32_t kGetLineStyle = 0x01a3b854;    // (screen, id) -> RGBA bytes
const uint32_t kGetFillStyle = 0x01a3b96c;
const uint32_t kSetTransform = 0x019ead58;    // (node, const int32 m[9]), Q10

const char* const kNodeIds[] = {
  "pageT", "pageX", "ind0", "ind1", "ind2", "ind3", "model_mk", "scale_mk" };
enum { N_PAGE_T, N_PAGE_X, N_IND0, N_IND1, N_IND2, N_IND3, N_MODEL, N_SCALE, N_COUNT };
static_assert(sizeof(kNodeIds) / sizeof(kNodeIds[0]) == N_COUNT, "node ids");
const char* const kGateIds[3] = { "gate1", "gate2", "gate3" };
const char* const kDejaVuIds[3] = { "dv_off", "dv_on", "dv_locked" };   // DejaVuMode order
const char* const kSourceIds[2] = { "src_scale", "src_keys" };          // PitchSource order
const char* const kPageIds[2] = { "pageTind", "pageXind" };             // Page order

struct Colour {
  uint8_t rgba[4];
};

void* stock_document;
void* marbles_document;
void* shown_document;
bool screen_ready;
void* nodes[N_COUNT];
uint8_t* gate_line[3];
uint8_t* gate_fill[3];
uint8_t* dv_line[3];
uint8_t* source_line[2];
uint8_t* page_line[2];
Colour gate_line_on, gate_fill_on, gate_line_off, gate_fill_off, selected, dim;

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

bool AllPresent(void* document, const char* const* ids, int32_t n) {
  for (int32_t i = 0; i < n; ++i) {
    if (!Find(document, ids[i])) return false;
  }
  return true;
}

void SetVisible(void* node, bool visible) {
  *reinterpret_cast<volatile uint8_t*>(reinterpret_cast<uintptr_t>(node) + kNodeVisible) =
      visible ? 1 : 0;
}

// The element's own transform (its wrapper group's stays): scale about the
// local origin, then translate; Q10.
void SetTransform(void* node, int32_t sx, int32_t sy, int32_t tx, int32_t ty) {
  const int32_t m[9] = { sx, 0, 0, 0, sy, 0, tx, ty, 0x400 };
  reinterpret_cast<SetTransformFn>(kSetTransform)(node, m);
}

void Read(Colour* c, const uint8_t* style) {
  for (int32_t i = 0; i < 4; ++i) c->rgba[i] = style[i];
}

void Write(uint8_t* style, const Colour& c) {
  for (int32_t i = 0; i < 4; ++i) style[i] = c.rgba[i];
}

void MarblesInit(void* self) {
  void* document = DocumentOf(self);
  // GetLineStyle asserts on a missing id: check them all first.
  if (!AllPresent(document, kNodeIds, N_COUNT) || !AllPresent(document, kGateIds, 3) ||
      !AllPresent(document, kDejaVuIds, 3) || !AllPresent(document, kSourceIds, 2) ||
      !AllPresent(document, kPageIds, 2)) {
    return;
  }
  for (int32_t i = 0; i < N_COUNT; ++i) nodes[i] = Find(document, kNodeIds[i]);
  StyleFn line = reinterpret_cast<StyleFn>(kGetLineStyle);
  StyleFn fill = reinterpret_cast<StyleFn>(kGetFillStyle);
  for (int32_t i = 0; i < 3; ++i) {
    gate_line[i] = line(self, kGateIds[i]);
    gate_fill[i] = fill(self, kGateIds[i]);
    dv_line[i] = line(self, kDejaVuIds[i]);
  }
  for (int32_t i = 0; i < 2; ++i) {
    source_line[i] = line(self, kSourceIds[i]);
    page_line[i] = line(self, kPageIds[i]);
  }
  Read(&gate_line_on, gate_line[0]);
  Read(&gate_fill_on, gate_fill[0]);
  Read(&gate_line_off, gate_line[2]);
  Read(&gate_fill_off, gate_fill[2]);
  Read(&selected, page_line[0]);
  Read(&dim, page_line[1]);
  screen_ready = true;
}

// Q16 (0..65535) -> the README's u in Q16.
int32_t StepU(int32_t index, int32_t steps) { return index * 65535 / (steps - 1); }

void MarblesDraw(void* self, const MarblesSeq* m) {
  const op1marbles::Settings& s = m->settings();
  const bool page_t = s.page == op1marbles::PAGE_T;
  SetVisible(nodes[N_PAGE_T], page_t);
  SetVisible(nodes[N_PAGE_X], !page_t);

  int32_t u[4];
  if (page_t) {
    int32_t k = 0;
    while (k < op1marbles::kNumDivisions - 1 && op1marbles::kDivisionTicks[k] != m->division_ticks()) ++k;
    u[0] = StepU(k, op1marbles::kNumDivisions);
    u[1] = s.t_bias;
    u[2] = s.jitter;
    u[3] = s.deja_vu;
  } else {
    u[0] = s.spread;
    u[1] = s.x_bias;
    u[2] = s.steps;
    u[3] = StepU(m->length_steps() - 1, op1marbles::kMaxLength);
  }
  // ind0: scale 0.15 + 0.85 u; ind1: x (u - 0.5) x 100; ind2: y -u x 100;
  // ind3: scale y max(u, 0.02). Q10 (1024 = 1.0 or 1 px).
  const int32_t s0 = 154 + ((870 * u[0]) >> 16);
  SetTransform(nodes[N_IND0], s0, s0, 0, 0);
  SetTransform(nodes[N_IND1], 0x400, 0x400, ((u[1] - 32768) * 25) >> 4, 0);
  SetTransform(nodes[N_IND2], 0x400, 0x400, 0, -((u[2] * 25) >> 4));
  const int32_t sy = u[3] >> 6;
  SetTransform(nodes[N_IND3], 0x400, sy < 20 ? 20 : sy, 0, 0);

  // model_mk: x + 17 per model; scale_mk: x + 20.4 per scale.
  SetTransform(nodes[N_MODEL], 0x400, 0x400, s.t_model * 17 * 1024, 0);
  SetTransform(nodes[N_SCALE], 0x400, 0x400, (s.scale * 20890), 0);   // 20.4 x 1024

  const uint8_t gates = m->gates();
  for (int32_t i = 0; i < 3; ++i) {
    const bool on = (gates >> i) & 1;
    Write(gate_line[i], on ? gate_line_on : gate_line_off);
    Write(gate_fill[i], on ? gate_fill_on : gate_fill_off);
  }
  const int32_t dv = page_t ? s.t_deja_vu : s.x_deja_vu;
  for (int32_t i = 0; i < 3; ++i) Write(dv_line[i], i == dv ? selected : dim);
  for (int32_t i = 0; i < 2; ++i) {
    Write(source_line[i], i == s.pitch_source ? selected : dim);
    Write(page_line[i], i == s.page ? selected : dim);
  }
  reinterpret_cast<DrawFn>(kScreenDraw)(self);
}

}  // namespace

extern "C" void MarblesScreenInit(void* self) {
  stock_document = DocumentOf(self);
  shown_document = stock_document;
  if (stock_document) reinterpret_cast<InitFn>(op1marbles::kStockScreenInit)(self);
  marbles_document = LoadDocument("marbles.svg");
  if (marbles_document) {
    DocumentOf(self) = marbles_document;
    MarblesInit(self);
    DocumentOf(self) = stock_document;
  }
}

extern "C" void MarblesScreenUpdate(void* self, int32_t ticks) {
  const bool marbles = marbles_on && screen_ready;
  void* document = marbles ? marbles_document : stock_document;
  if (document != shown_document) {
    DocumentOf(self) = document;
    if (document) Flush(document);
    shown_document = document;
  }
  if (marbles) {
    MarblesDraw(self, Seq());
  } else if (stock_document) {
    reinterpret_cast<UpdateFn>(op1marbles::kStockScreenUpdate)(self, ticks);
  }
}
