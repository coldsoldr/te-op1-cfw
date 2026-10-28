// Mutable Instruments Marbles as a kennel sequencer feature, id marbles
// (docs/kennel-roadmap.md phase 3; the core: ports/marbles/op1/marbles_seq.*,
// docs/marbles-port.md section 9). A named entry in the sequencer list of
// both instruments; one Marbles per mode.
//
// Transport, as the OP-1's own sequencers (the user's choice, Test 36):
// Marbles runs while a key is held, from the key press; shift pressed while
// keys are held latches it; the next key press takes over; the last key up
// stops it. A key pressed with shift already down plays through. The keys
// are the transport, so they don't sound themselves.
//
// Notes: T1/T2/T3 gates play X1/X2/X3 (overlapping into chords), Y sets the
// velocity. On the synth the pitch comes from the scale (root = the last
// key) or the held keys; on the drums a pitch picks a sound (the key it
// lands on), so a drum kit gets Marbles' rhythms too.
//
// Controls on the marbles screen (the type's press / turn):
//   T page: blue rate, green deja vu, white jitter, orange bias;
//   X page: blue spread, green length, white steps, orange bias;
//   shift + turn: fine (an eighth of a click);
//   blue press: T model (T page) or scale (X page), shift: backwards;
//   green press: the page's deja vu off / on / locked (shift: backwards);
//   white press: pitch source scale / keys; shift + white: the page.
// Settings are saved per mode in the op1.db table marblesseq (the core's
// background saver, seq/seq_text.h sq_store_*), loaded with the player.
//
// Screen: the user's "Dots" design (ports/marbles/op1/marbles.svg, the
// category dots stripped), driven as the firmware build drove it
// (ports/marbles/op1/marbles_op1.cc).

#include "common/knl_seq.h"
#include "op1/marbles_seq.h"

#ifndef OP1_HOST
#include "fx/op1/fx_screen.h"
#endif

namespace {

using namespace knl::seq;
using op1marbles::MarblesSeq;
using op1marbles::Settings;

const int kFineShift = 3;   // shift + turn: 1/8 of a click

MarblesSeq seqs[2];         // per mode
bool seq_ready[2];
int16_t note_ids[2][256];   // per mode, per pitch + 64: op1_seq_note_on's id + 1, 0 = none

MarblesSeq* Seq(int m) {
  m &= 1;
  if (!seq_ready[m]) {
    seqs[m].Init((Entropy() ^ (m ? 0x9e3779b9u : 0)) | 1);
    seq_ready[m] = true;
  }
  return &seqs[m];
}

// Marbles' notes through the core, as the stock sequencers play theirs. The
// id is kept per pitch, so a note-off matches its note-on across an octave
// change.
class Sink : public op1marbles::NoteSink {
 public:
  explicit Sink(int mode) : mode_(mode & 1) {}
  void NoteOn(int32_t pitch, int32_t velocity) override {
    int16_t& slot = note_ids[mode_][(pitch + 64) & 255];
    if (slot) knl::seq::NoteOff(slot - 1);
    const int32_t id = knl::seq::NoteOn(mode_, pitch, velocity);
    slot = id < 0 ? 0 : static_cast<int16_t>(id + 1);
  }
  void NoteOff(int32_t pitch) override {
    int16_t& slot = note_ids[mode_][(pitch + 64) & 255];
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
  Sink sink(m);
  seqs[m].AllNotesOff(&sink);
}

// ---- settings text: "1,page,source,model,scale,tdv,xdv,rate,tbias,jitter,dejavu,spread,xbias,steps,length"
// (the Q16 values as 0..8191) ----
const int kVersion = 1;
const int kFields = 15;

int Serialize(int m, char* out, int n) {
  const Settings& s = Seq(m)->settings();
  const int v[kFields] = {
    kVersion, s.page, s.pitch_source, s.t_model, s.scale, s.t_deja_vu, s.x_deja_vu,
    Q16ToText(s.rate), Q16ToText(s.t_bias), Q16ToText(s.jitter), Q16ToText(s.deja_vu),
    Q16ToText(s.spread), Q16ToText(s.x_bias), Q16ToText(s.steps), Q16ToText(s.length) };
  return sq_join(out, n, v, kFields);
}

int Parse(int m, const char* text, int len) {
  int v[kFields];
  if (sq_split(text, len, v, kFields) != kFields || v[0] != kVersion) return 0;
  if (!InRange(v[1], 0, 1) || !InRange(v[2], 0, 1) || !InRange(v[3], 0, op1marbles::kNumTModels - 1) ||
      !InRange(v[4], 0, op1marbles::kNumScales - 1) || !InRange(v[5], 0, 2) || !InRange(v[6], 0, 2)) {
    return 0;
  }
  for (int i = 7; i < kFields; ++i) {
    if (!InRange(v[i], 0, 8191)) return 0;
  }
  Settings* s = Seq(m)->mutable_settings();
  s->page = v[1];
  s->pitch_source = v[2];
  s->t_model = v[3];
  s->scale = v[4];
  s->t_deja_vu = v[5];
  s->x_deja_vu = v[6];
  s->rate = TextToQ16(v[7]);
  s->t_bias = TextToQ16(v[8]);
  s->jitter = TextToQ16(v[9]);
  s->deja_vu = TextToQ16(v[10]);
  s->spread = TextToQ16(v[11]);
  s->x_bias = TextToQ16(v[12]);
  s->steps = TextToQ16(v[13]);
  s->length = TextToQ16(v[14]);
  return 1;
}

char sql[512];
sq_store_t store = { 0, 0, { 0, 0 }, nullptr, { 0, 0 }, "marblesseq", Serialize, Parse, sql,
                     static_cast<int16_t>(sizeof sql), 256, 0 };

void Dirty() { sq_store_dirty(&store); }

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
  MarblesSeq* s = Seq(mode);
  const int32_t direction = shift ? -1 : 1;
  switch (knob) {
    case kKnobBlue:
      s->OnClick(op1marbles::CLICK_BLUE, direction);
      break;
    case kKnobGreen:
      s->OnClick(op1marbles::CLICK_GREEN, direction);
      break;
    case kKnobWhite:
      s->OnClick(op1marbles::CLICK_WHITE, direction);   // shift: the page
      break;
    default:
      return;
  }
  Dirty();
}

void Turn(int mode, int knob, int delta, int shift) {
  if (knob < 0 || knob > 3) return;
  Seq(mode)->Knob(knob, shift ? delta >> kFineShift : delta);
  Dirty();
}

void MidiStart() __attribute__((unused));
void MidiStart() {}

}  // namespace

// ---- the player (knl_seq_player.c's vtable) ----
extern "C" int knl_seq_key_down(void* self, int key, int flag) {
  (void)self;
  if (key < 0 || key >= kNumKeys) return 0;
  // flag = 1 with shift held (Test 20); the flag's absence is covered by the
  // shift byte.
  Seq(Mode())->KeyDown(key, flag != 0 || ShiftHeld());
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
  Sink sink(m);
  MarblesSeq* s = Seq(m);
  s->set_shift_held(ShiftHeld());
  s->Tick(Phase(t), &sink);
}

extern "C" void knl_seq_stop(void* self) {
  (void)self;
  AllOff(0);
  AllOff(1);
}

#ifdef OP1_HOST
// The tests' way in (ports/knl/tests/marbles_host_test.cc).
extern "C" void* knl_host_player() { return GetPlayer(); }
extern "C" int knl_host_key(int mode, int code, int pressed, int shift) { return Key(mode, code, pressed, shift); }
extern "C" void knl_host_press(int mode, int knob, int shift) { Press(mode, knob, shift); }
extern "C" void knl_host_turn(int mode, int knob, int delta, int shift) { Turn(mode, knob, delta, shift); }
extern "C" int knl_host_serialize(int m, char* out, int n) { return Serialize(m, out, n); }
extern "C" int knl_host_parse(int m, const char* s, int len) { return Parse(m, s, len); }
extern "C" int knl_host_store_ready() { return store.ready; }
op1marbles::MarblesSeq* marbles_host_seq(int m) { return Seq(m); }
#else

// ---- the screen (Gfx thread): the "Dots" design, ports/marbles/op1/marbles-screen.md ----
namespace {

using namespace op1fx::screen;

// An id may not contain "_x" (the parser decodes "_xHH_" escapes), so loopX
// and leg<i>X. Bias (bar3, top right) has no legend icon: three icon pairs.
const char* const kNodeIds[] = {
  "loop_t", "loopX", "bar0", "bar1", "bar2", "bar3", "chipT", "chipX", "chip_model", "chip_scale",
  "leg0_t", "leg1_t", "leg2_t", "leg0X", "leg1X", "leg2X", "page1ind", "page2ind" };
const int32_t kLegends = 3;
enum {
  N_LOOP_T, N_LOOP_X, N_BAR0, N_CHIP_T = N_BAR0 + 4, N_CHIP_X, N_CHIP_MODEL, N_CHIP_SCALE,
  N_LEG_T, N_LEG_X = N_LEG_T + kLegends, N_PAGE1 = N_LEG_X + kLegends, N_PAGE2, N_COUNT };
static_assert(sizeof(kNodeIds) / sizeof(kNodeIds[0]) == N_COUNT, "node ids");
// PitchSource order. src_keys is a group: GetLineStyle on a group gives
// every child the first child's style, so it is recoloured as one.
const char* const kSourceIds[2] = { "src_scale", "src_keys" };
const int32_t kLanes = 3;
const int32_t kColumns = op1marbles::kHistory;
const int32_t kBarAnchor[4] = { 44, 132, 220, 232 };   // px, the bars' left ends

bool ready;
void* nodes[N_COUNT];
void* marks[kLanes][kColumns];
uint8_t* loop_line[2];
uint8_t* source_line[2];
uint8_t* page_ind_line[2];
uint8_t* page_ind_fill[2];
Colour bright, dim;              // the deja vu brackets (green)
Colour source_on, source_off;    // the pitch source icons
Colour ind_line_on, ind_line_off, ind_fill_on, ind_fill_off;   // the page squares

void ScreenInit(void* scr) {
  ready = false;
  void* document = DocumentOf(scr);
  if (!document) return;
  // GetLineStyle asserts on a missing id: check them all first.
  if (!FindAll(document, kNodeIds, nodes, N_COUNT)) return;
  void* sources[2];
  if (!FindAll(document, kSourceIds, sources, 2)) return;
  char id[5] = { 'h', '0', '0', 0, 0 };   // h<lane><col>, col 0..15
  for (int32_t lane = 0; lane < kLanes; ++lane) {
    for (int32_t c = 0; c < kColumns; ++c) {
      id[1] = static_cast<char>('0' + lane);
      if (c < 10) {
        id[2] = static_cast<char>('0' + c);
        id[3] = 0;
      } else {
        id[2] = '1';
        id[3] = static_cast<char>('0' + c - 10);
      }
      marks[lane][c] = Find(document, id);
      if (!marks[lane][c]) return;
    }
  }
  loop_line[0] = LineStyle(scr, "loop_t");
  loop_line[1] = LineStyle(scr, "loopX");
  for (int32_t i = 0; i < 2; ++i) source_line[i] = LineStyle(scr, kSourceIds[i]);
  page_ind_line[0] = LineStyle(scr, "page1ind");
  page_ind_line[1] = LineStyle(scr, "page2ind");
  page_ind_fill[0] = FillStyle(scr, "page1ind");
  page_ind_fill[1] = FillStyle(scr, "page2ind");   // #010101, not "none"
  // The file's default state: loop_t bright (locked), loopX dim (on),
  // src_scale selected, src_keys not; page1ind lit, page2ind not.
  ReadColour(&bright, loop_line[0]);
  ReadColour(&dim, loop_line[1]);
  ReadColour(&source_on, source_line[0]);
  ReadColour(&source_off, source_line[1]);
  ReadColour(&ind_line_on, page_ind_line[0]);
  ReadColour(&ind_fill_on, page_ind_fill[0]);
  ReadColour(&ind_line_off, page_ind_line[1]);
  ReadColour(&ind_fill_off, page_ind_fill[1]);
  ready = true;
}

// index / (steps - 1) in Q16.
int32_t StepU(int32_t index, int32_t steps) { return index * 65535 / (steps - 1); }

void Draw(const MarblesSeq* m) {
  const Settings& s = m->settings();
  const bool page_t = s.page == op1marbles::PAGE_T;

  // Page squares: T, X.
  WriteColour(page_ind_line[0], page_t ? ind_line_on : ind_line_off);
  WriteColour(page_ind_fill[0], page_t ? ind_fill_on : ind_fill_off);
  WriteColour(page_ind_line[1], page_t ? ind_line_off : ind_line_on);
  WriteColour(page_ind_fill[1], page_t ? ind_fill_off : ind_fill_on);

  // History: the marks of the steps that played, raised by their height
  // (14 px of travel).
  for (int32_t lane = 0; lane < kLanes; ++lane) {
    const uint16_t gates = m->history_gates(lane);
    for (int32_t c = 0; c < kColumns; ++c) {
      const bool on = (gates >> c) & 1;
      SetVisible(marks[lane][c], on);
      if (on) SetTranslate(marks[lane][c], 0, -(m->history_height(lane, c) * 14 * 1024 / 255));
    }
  }

  // Loop brackets: the last `length` columns (anchor x = 276, the right
  // end), hidden when off, dim when on, bright when locked.
  const int32_t n = m->length_steps();
  const int32_t modes[2] = { s.t_deja_vu, s.x_deja_vu };
  for (int32_t i = 0; i < 2; ++i) {
    void* bracket = nodes[N_LOOP_T + i];
    SetVisible(bracket, modes[i] != op1marbles::DEJA_VU_OFF);
    WriteColour(loop_line[i], modes[i] == op1marbles::DEJA_VU_LOCKED ? bright : dim);
    ScaleX(bracket, n * 64, 276);
  }

  // Legend: the page's three icons; the bars (bar0..2 along the bottom,
  // bar3 = bias at the top right), each scaled about its left end.
  for (int32_t i = 0; i < kLegends; ++i) {
    SetVisible(nodes[N_LEG_T + i], page_t);
    SetVisible(nodes[N_LEG_X + i], !page_t);
  }
  int32_t u[4];
  if (page_t) {
    int32_t k = 0;
    while (k < op1marbles::kNumDivisions - 1 && op1marbles::kDivisionTicks[k] != m->division_ticks()) ++k;
    u[0] = StepU(k, op1marbles::kNumDivisions);
    u[1] = s.deja_vu;
    u[2] = s.jitter;
    u[3] = s.t_bias;
  } else {
    u[0] = s.spread;
    u[1] = StepU(n - 1, op1marbles::kMaxLength);
    u[2] = s.steps;
    u[3] = s.x_bias;
  }
  for (int32_t i = 0; i < 4; ++i) ScaleX(nodes[N_BAR0 + i], u[i] >> 6, kBarAnchor[i]);

  // Chips: the model (page T, 7 dots) or scale (page X, 6 dots) row, its
  // lit dot 10 px per step; the pitch source.
  SetVisible(nodes[N_CHIP_T], page_t);
  SetVisible(nodes[N_CHIP_X], !page_t);
  SetTranslate(nodes[N_CHIP_MODEL], s.t_model * 10240, 0);   // 10 px, Q10
  SetTranslate(nodes[N_CHIP_SCALE], s.scale * 10240, 0);
  for (int32_t i = 0; i < 2; ++i) WriteColour(source_line[i], i == s.pitch_source ? source_on : source_off);
}

void ScreenUpdate(void* scr, int dt) {
  const int m = Mode();
  if (ready && seq_ready[m]) Draw(&seqs[m]);
  op1_screen_draw(scr, dt);
}

}  // namespace

extern "C" const op1_screen_t SCREEN_marbles = { ScreenInit, ScreenUpdate };

namespace {
const op1_seq_type_t kType = {
  .name = "marbles", .alias = 0, .player = GetPlayer, .svg = "marbles.svg", .screen = &SCREEN_marbles,
  .key = Key, .press = Press, .turn = Turn, .shift_wants = 0, .midi_start = MidiStart };
const char* const kClaims[] = { 0 };
}  // namespace

extern "C" const op1_feature_t FEATURE_marbles = {
  .api = OP1_FEATURE_API, .priority = 1000, .name = "marbles",
  .note_on = 0, .note_off = 0, .key_sync = 0, .page = 0, .tick = 0,
  .seq = &kType, .lfo = 0, .drum = 0, .tape = 0, .view = 0,
  .event = 0, .input = 0, .midi_in = 0, .note_in = 0, .claims = kClaims,
  .overlay = 0, .com = 0, .midi_raw = 0, .tape_view = 0, .drum_synth = 0 };

#endif  // OP1_HOST
