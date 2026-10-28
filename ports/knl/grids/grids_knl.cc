// Mutable Instruments Grids as a kennel sequencer feature, id grids
// (docs/kennel-roadmap.md phase 3; the core: ports/grids/op1/grids_seq.*,
// docs/grids-port.md "Decisions"). A named entry in the sequencer list of
// both instruments; one Grids per mode. Meant for the drums; on the synth
// the three parts play keys 0, 2 and 8 as notes.
//
// Transport, as the OP-1's own drum sequencers (the user's choice, Test
// 36): Grids runs while a key is held, its bar starting on the key press;
// shift pressed while a key is held latches it; the next key press clears
// the latch; the last key up stops it. Keys don't sound themselves, except
// the one an assign captures (an audition).
//
// Controls on the grids screen (the type's press / turn):
//   drums: blue map X, green map Y, white chaos, orange swing;
//   Euclidean: blue / green / white the parts' lengths, orange swing;
//   shift + turn: the fill page (BD / SD / HH fill or hits, orange accent);
//   blue press: drums / Euclidean, shift: 16ths / 32nds;
//   green press: re-roll the chaos, shift: forget the accent keys;
//   white press: assign BD -> SD -> HH -> accents -> off (the next key is the
//   part's sound), shift: the random voice on / off;
//   shift + orange press: print the last bar into the stock pattern
//   sequencer's drum data (16 sixteenths; the stock saver writes it).
// Settings are saved per mode in the op1.db table gridsseq (the core's
// background saver, seq/seq_text.h sq_store_*), loaded with the player.
//
// Screen: the user's "Panel + kit" design (ports/grids/op1/grids.svg, the
// category dots stripped), driven as the firmware build drove it
// (ports/grids/op1/grids_op1.cc).
// Part of the Grids port; Grids (c) 2011-2012 Emilie Gillet, GPL v3.

#include "common/knl_seq.h"
#include "op1/grids_seq.h"

#ifndef OP1_HOST
#include "fx/op1/fx_screen.h"
#endif

namespace {

using namespace knl::seq;
using op1grids::GridsSeq;
using op1grids::GridsUi;

const uint16_t kSeed = 0x21;   // Grids' own

GridsSeq seqs[2];              // per mode
bool seq_ready[2];
int16_t note_ids[2][32];       // per mode, per key: op1_seq_note_on's id + 1, 0 = none

GridsSeq* Seq(int m) {
  m &= 1;
  if (!seq_ready[m]) {
    seqs[m].Init(static_cast<uint16_t>(kSeed + m * 0x3c7));
    seq_ready[m] = true;
  }
  return &seqs[m];
}

// Grids' notes through the core: a drum key as its own sound, a synth key
// as its pitch.
class Out : public op1grids::NoteOut {
 public:
  explicit Out(int mode) : mode_(mode & 1) {}
  void NoteOn(int32_t key, int32_t velocity) override {
    if (key < 0 || key >= 32) return;
    int16_t& slot = note_ids[mode_][key];
    if (slot) knl::seq::NoteOff(slot - 1);
    const int32_t id = knl::seq::NoteOn(mode_, key, velocity);
    slot = id < 0 ? 0 : static_cast<int16_t>(id + 1);
  }
  void NoteOff(int32_t key) override {
    if (key < 0 || key >= 32) return;
    int16_t& slot = note_ids[mode_][key];
    if (!slot) return;
    knl::seq::NoteOff(slot - 1);
    slot = 0;
  }

 private:
  int mode_;
};

void AllOff(int m) {
  m &= 1;
  if (!seq_ready[m]) return;
  Out out(m);
  seqs[m].AllNotesOff(&out);
}

// ---- settings text: "1,x,y,chaos,swing,fill0,fill1,fill2,len0,len1,len2,accent,mode,res,rnd,
// key0,key1,key2,akey0,akey1,akey2" (the Q16 values as 0..8191; a key 0..23, 255 = none) ----
const int kVersion = 1;
const int kFields = 21;

int Serialize(int m, char* out, int n) {
  const GridsUi& u = Seq(m)->ui();
  const int v[kFields] = {
    kVersion, Q16ToText(u.x), Q16ToText(u.y), Q16ToText(u.chaos), Q16ToText(u.swing),
    Q16ToText(u.fill[0]), Q16ToText(u.fill[1]), Q16ToText(u.fill[2]),
    Q16ToText(u.length[0]), Q16ToText(u.length[1]), Q16ToText(u.length[2]), Q16ToText(u.accent),
    u.mode, u.resolution, u.random_voice, u.key[0], u.key[1], u.key[2],
    u.accent_key[0], u.accent_key[1], u.accent_key[2] };
  return sq_join(out, n, v, kFields);
}

bool KeyOrNone(int v) { return InRange(v, 0, kNumKeys - 1) || v == op1grids::kNoKey; }

int Parse(int m, const char* text, int len) {
  int v[kFields];
  if (sq_split(text, len, v, kFields) != kFields || v[0] != kVersion) return 0;
  for (int i = 1; i <= 11; ++i) {
    if (!InRange(v[i], 0, 8191)) return 0;
  }
  if (!InRange(v[12], 0, 1) || !InRange(v[13], 0, 1) || !InRange(v[14], 0, 1)) return 0;
  for (int i = 15; i < 18; ++i) {
    if (!InRange(v[i], 0, kNumKeys - 1)) return 0;
  }
  for (int i = 18; i < 21; ++i) {
    if (!KeyOrNone(v[i])) return 0;
  }
  GridsUi* u = Seq(m)->mutable_ui();
  u->x = TextToQ16(v[1]);
  u->y = TextToQ16(v[2]);
  u->chaos = TextToQ16(v[3]);
  u->swing = TextToQ16(v[4]);
  for (int i = 0; i < 3; ++i) {
    u->fill[i] = TextToQ16(v[5 + i]);
    u->length[i] = TextToQ16(v[8 + i]);
    u->key[i] = static_cast<uint8_t>(v[15 + i]);
    u->accent_key[i] = static_cast<uint8_t>(v[18 + i]);
  }
  u->accent = TextToQ16(v[11]);
  u->mode = static_cast<uint8_t>(v[12]);
  u->resolution = static_cast<uint8_t>(v[13]);
  u->random_voice = static_cast<uint8_t>(v[14]);
  u->assign = op1grids::ASSIGN_OFF;
  return 1;
}

char sql[512];
sq_store_t store = { 0, 0, { 0, 0 }, nullptr, { 0, 0 }, "gridsseq", Serialize, Parse, sql,
                     static_cast<int16_t>(sizeof sql), 256, 0 };

void Dirty() { sq_store_dirty(&store); }

#ifndef OP1_HOST
// The last bar into the stock pattern sequencer's drum data
// (docs/sequencer-interface.md section 12.2): 16 sixteenths, all 24 keys,
// length 16 from step 0, then stock's save. Accents can't be stored
// (pattern's notes are on/off). The data objects are the instrument's own,
// built by the getter whichever sequencer is active.
const uint32_t kPatternData = 0x01a083c4;       // () -> the live instrument's PatternData
const uint32_t kPatternSetLength = 0x01a08030;  // (pd, length 1..16)
const uint32_t kPatternSetStart = 0x01a08080;   // (pd, start 0..15)
const uint32_t kPatternSave = 0x0193ef3c;       // (pd): mark dirty, wake the saver
const uint32_t kPatternNotes = 0x08;            // PatternData: 16 steps x 24 keys, a byte each
typedef uint8_t* (*PatternDataFn)();
typedef void (*PatternSetFn)(uint8_t* pd, int32_t value);
typedef void (*PatternSaveFn)(uint8_t* pd);

void Print(int m) {
  if (m != 1) return;   // the drums' pattern only
  uint8_t* pd = reinterpret_cast<PatternDataFn>(kPatternData)();
  if (!pd) return;
  const uint32_t* bar = Seq(m)->bar();
  for (int32_t step = 0; step < op1grids::kPrintSteps; ++step) {
    for (int32_t key = 0; key < kNumKeys; ++key) {
      pd[kPatternNotes + step * kNumKeys + key] = static_cast<uint8_t>((bar[step] >> key) & 1);
    }
  }
  reinterpret_cast<PatternSetFn>(kPatternSetLength)(pd, op1grids::kPrintSteps);
  reinterpret_cast<PatternSetFn>(kPatternSetStart)(pd, 0);
  reinterpret_cast<PatternSaveFn>(kPatternSave)(pd);
}
#else
void Print(int m) { (void)m; }
#endif

// ---- the type's callbacks (KeyHandler thread, its screen shown) ----
void* GetPlayer() {
  if (!store.ready) {
    Seq(0);
    Seq(1);
    sq_store_load(&store);
  }
  return knl_seq_player_object();
}

int Key(int mode, int code, int pressed, int shift) {
  (void)mode;
  (void)pressed;
  (void)shift;
  return ConsumedKey(code) ? 1 : 0;
}

void Press(int mode, int knob, int shift) {
  GridsSeq* s = Seq(mode);
  const int32_t direction = shift ? -1 : 1;
  switch (knob) {
    case kKnobBlue:
      s->OnClick(op1grids::GRIDS_BLUE, direction);
      break;
    case kKnobGreen:
      s->OnClick(op1grids::GRIDS_GREEN, direction);
      break;
    case kKnobWhite:
      s->OnClick(op1grids::GRIDS_WHITE, direction);
      break;
    case kKnobOrange:
      if (!shift) return;
      Print(mode & 1);
      return;
    default:
      return;
  }
  Dirty();
}

void Turn(int mode, int knob, int delta, int shift) {
  if (knob < 0 || knob > 3) return;
  Seq(mode)->Knob(shift ? knob + 4 : knob, delta);   // shift: the fill page
  Dirty();
}

void MidiStart() __attribute__((unused));
void MidiStart() {}

}  // namespace

// ---- the player (knl_seq_player.c's vtable) ----
extern "C" int knl_seq_key_down(void* self, int key, int flag) {
  (void)self;
  (void)flag;
  if (key < 0 || key >= kNumKeys) return 0;
  Seq(Mode())->KeyDown(key);
  return 1;
}

extern "C" void knl_seq_key_up(void* self, int key) {
  (void)self;
  if (key < 0 || key >= kNumKeys) return;
  Seq(Mode())->KeyUp(key);
}

extern "C" void knl_seq_reset(void* self) {
  (void)self;
  AllOff(Mode());
}

extern "C" void knl_seq_tick(void* self, int t) {
  (void)self;
  const int m = Mode();
  Out out(m);
  GridsSeq* s = Seq(m);
  s->set_shift_held(ShiftHeld());
  s->Tick(Phase(t), &out);
}

extern "C" void knl_seq_stop(void* self) {
  (void)self;
  AllOff(0);
  AllOff(1);
}

#ifdef OP1_HOST
// The tests' way in (ports/knl/tests/grids_host_test.cc).
extern "C" void* knl_host_player() { return GetPlayer(); }
extern "C" int knl_host_key(int mode, int code, int pressed, int shift) { return Key(mode, code, pressed, shift); }
extern "C" void knl_host_press(int mode, int knob, int shift) { Press(mode, knob, shift); }
extern "C" void knl_host_turn(int mode, int knob, int delta, int shift) { Turn(mode, knob, delta, shift); }
extern "C" int knl_host_serialize(int m, char* out, int n) { return Serialize(m, out, n); }
extern "C" int knl_host_parse(int m, const char* s, int len) { return Parse(m, s, len); }
extern "C" int knl_host_store_ready() { return store.ready; }
op1grids::GridsSeq* grids_host_seq(int m) { return Seq(m); }
#else

// ---- the screen (Gfx thread): the "Panel + kit" design, ports/grids/op1/grids-screen.md ----
namespace {

using namespace op1fx::screen;

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

bool ready;
void* nodes[N_COUNT];
void* parts[3];
uint8_t* part_line[3];
uint8_t* part_fill[3];
uint8_t* rnd_line;
Colour lit_line, lit_fill, idle_line, idle_fill;

void ScreenInit(void* scr) {
  ready = false;
  void* document = DocumentOf(scr);
  if (!document) return;
  // GetLineStyle asserts on a missing id: check them all first.
  if (!FindAll(document, kNodeIds, nodes, N_COUNT) || !FindAll(document, kPartIds, parts, 3)) return;
  if (!Find(document, "rnd_on")) return;
  for (int32_t i = 0; i < 3; ++i) {
    part_line[i] = LineStyle(scr, kPartIds[i]);
    part_fill[i] = FillStyle(scr, kPartIds[i]);
  }
  rnd_line = LineStyle(scr, "rnd_on");
  // The file's example state: part_hh lit, part_sd idle.
  ReadColour(&lit_line, part_line[2]);
  ReadColour(&lit_fill, part_fill[2]);
  ReadColour(&idle_line, part_line[1]);
  ReadColour(&idle_fill, part_fill[1]);
  ready = true;
}

// A bar: u (Q16) as a width about its left end x0, at least 0.02.
void Bar(void* node, int32_t u_q16, int32_t x0) {
  int32_t s = u_q16 >> 6;   // Q10
  if (s < 20) s = 20;
  ScaleX(node, s, x0);
}

void Draw(const GridsSeq* g) {
  const GridsUi& u = g->ui();
  const bool euclid = u.mode == op1grids::OUTPUT_MODE_EUCLIDEAN;
  const bool shift = ShiftHeld();

  // The map's dot (a crosshair drawn at 32,106): x 32 + 64 X, y 106 - 64 Y.
  SetVisible(nodes[N_DOT], !euclid);
  SetTranslate(nodes[N_DOT], (u.x * 64) >> 6, -((u.y * 64) >> 6));

  // The kit: lit on the last step's hits and while a part's key is held;
  // 1.25 x about the centre on an accent.
  const uint8_t state = g->last_state();
  const uint32_t held = g->held_parts();
  for (int32_t p = 0; p < 3; ++p) {
    const bool hit = ((state >> p) & 1) || ((held >> p) & 1);
    WriteColour(part_line[p], hit ? lit_line : idle_line);
    WriteColour(part_fill[p], hit ? lit_fill : idle_fill);
    if (((state >> (p + 3)) & 1) && ((state >> p) & 1)) {
      SetMatrix(parts[p], 1280, 1280, -256 * kPartCentreX, -256 * kPartCentreY[p]);   // c x -0.25
    } else {
      SetMatrix(parts[p], 0x400, 0x400, 0, 0);
    }
  }
  // The assign arrow points at the part (or its accent) being assigned.
  const int32_t assign = u.assign;
  SetVisible(nodes[N_ASSIGN], assign != op1grids::ASSIGN_OFF);
  if (assign != op1grids::ASSIGN_OFF) SetTranslate(nodes[N_ASSIGN], 0, kAssignDy[(assign - 1) % 3] << 10);

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
  SetTranslate(nodes[N_PLAYHEAD], g->step() * step_px * 1024, 0);

  // State icons.
  bool accent_keys = false;
  for (int32_t i = 0; i < 3; ++i) accent_keys = accent_keys || u.accent_key[i] != op1grids::kNoKey;
  SetVisible(nodes[N_MODE_DRUMS], !euclid);
  SetVisible(nodes[N_MODE_EUCLID], euclid);
  SetVisible(nodes[N_RES16], u.resolution == op1grids::RESOLUTION_16TH);
  SetVisible(nodes[N_RES32], u.resolution != op1grids::RESOLUTION_16TH);
  SetVisible(nodes[N_ACCENT_VEL], !accent_keys);
  SetVisible(nodes[N_ACCENT_KEY], accent_keys);
  WriteColour(rnd_line, u.random_voice ? lit_line : idle_line);
  SetVisible(nodes[N_SHIFT], shift);
}

void ScreenUpdate(void* scr, int dt) {
  const int m = Mode();
  if (ready && seq_ready[m]) Draw(&seqs[m]);
  op1_screen_draw(scr, dt);
}

}  // namespace

extern "C" const op1_screen_t SCREEN_grids = { ScreenInit, ScreenUpdate };

namespace {
const op1_seq_type_t kType = {
  .name = "grids", .alias = 0, .player = GetPlayer, .svg = "grids.svg", .screen = &SCREEN_grids,
  .key = Key, .press = Press, .turn = Turn, .shift_wants = 0, .midi_start = MidiStart };
const char* const kClaims[] = { 0 };
}  // namespace

extern "C" const op1_feature_t FEATURE_grids = {
  .api = OP1_FEATURE_API, .priority = 1000, .name = "grids",
  .note_on = 0, .note_off = 0, .key_sync = 0, .page = 0, .tick = 0,
  .seq = &kType, .lfo = 0, .drum = 0, .tape = 0, .view = 0,
  .event = 0, .input = 0, .midi_in = 0, .note_in = 0, .claims = kClaims,
  .overlay = 0, .com = 0, .midi_raw = 0, .tape_view = 0, .drum_synth = 0 };

#endif  // OP1_HOST
