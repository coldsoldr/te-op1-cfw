// TAPE ECHO as a kennel effect plugin, id tape_echo ("tape echo" in the
// list; was cs_tape_echo, the user's name 2026-10-28). The core of
// fx/src/tape_echo/tape_echo.h at half the effect rate behind
// fx/op1/half_rate.h, one instance per position in its scratch buffer
// (common/knl_fx.h).
//   knobs: blue time, green feedback, white age, orange mix
//   blue press:  page 2, where blue sets the input gain (word 5: -6 dB at
//                0, 0 dB at the centre, +6 dB at the top; shift + blue a
//                tenth of a dB a click, knl::Fine). The gain scales what
//                reaches the record head only: the dry side of the mix
//                doesn't see it.
//   shift + blue on page 1: the time locks to the project tempo's
//                divisions (1/16T, 1/16, 1/8T, 1/16., 1/8, 1/4T, 1/8., 1/4,
//                1/2T, 1/4., 1/2: shortest first, a division a click,
//                clockwise = longer; the ones over 1.5 s at the tempo are skipped).
//                Each shifted click (a change of the stored time word) is a
//                step; the plugin feeds the core the division's time in place
//                of the word (TimeLock; the word is never rewritten: the stock
//                knob keeps its own copy of a page-1 value, Test 61). A turn
//                without shift frees it. The tempo: knl::TempoIncrement.
//   green press: latches hold (the tape loops what it holds; not saved)
//   white press: page 2, where white picks the head pattern (word 7, one
//                click per pattern: head 3; heads 2+3; 1+2+3; 1+3). Was
//                blue's page 2 in cs_tape_echo.
//
// The screen (gen_tape_echo_knl_svg.py, the canvas "Tape echo screen" design
// B, "Schematic", 2026-10-28):
//   hd1..hd3 (hp, tp)  the play heads with their taps, translated with the
//                      time (head 3 at 32 + 130 t px from the record head),
//                      lit per pattern
//   wbox               the bracket round the heads while white is on page 2
//   tape               the tape wave, scaled by age about the tape row and
//                      breathing with the wow
//   dot                the signal on the tape, record head -> head 3 at the
//                      tape speed (one trip per TIME, at most 8 a second)
//   bt1..bt24          16th ticks from the record head while shift is held
//                      on the time page
//   dv0..dv10          the division read out while the time sits on one
//   pt0..pt3           the pattern read out while white is on page 2
//   fb1..fb4           feedback, a count set (all four while holding)
//   inl, ina, rech, ret1..ret3, hold   the hold colours
//   gbar, gzero, gbox, gs/gd/gdot/gt   the gain gauge, bracket and readout
//   mbar               the mix gauge, scaled about its foot

#include "common/knl_fx.h"
#include "fx/op1/fx_screen.h"
#include "fx/op1/half_rate.h"
#include "fx/op1/screen_anim.h"
#include "fx/src/tape_echo/tape_echo.h"
#include "op1/op1.h"

namespace {

struct Instance {
  op1fx::TapeEcho echo;
  op1fx::HalfRate half;
};

knl::Fine fine;
const int32_t kTenthDb = 273;   // 32767 / 120: one tenth of a dB of word 5, the readout's unit

int32_t Pattern(int32_t word7) { return knl::ClickMode(word7, op1fx::kTapeEchoNumPatterns); }   // one click per pattern

// ---- the tempo divisions ----

// A 96th note (a 16th triplet is two) in Q4 samples at 22050 Hz, from the
// tempo increment: 16 x 22050 x 60 / 24 x 1662149.125 / inc.
const int64_t kNinetySixthNum = 1466015528250LL;
const int32_t kNumDivisions = 11;
// 1/16T 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/2T 1/4. 1/2: the last three fit the 1.5 s ceiling from 54, 60 and 80 BPM up.
const int32_t kDivision96ths[kNumDivisions] = { 4, 6, 8, 9, 12, 16, 18, 24, 32, 36, 48 };

inline int32_t NinetySixthQ4(uint32_t inc) { return static_cast<int32_t>(kNinetySixthNum / inc); }

// The time word of division d at this tempo, -1 if it's outside the range.
inline int32_t DivisionWord(int32_t d, int32_t s96_q4) {
  return op1fx::TapeEchoTimeWord(s96_q4 * kDivision96ths[d]);
}

// Shift + blue on the time page: the time walks the divisions. Each change
// of the stored time word (0) while shift is held is one step (the stock
// knob moves it a small way a click under shift); the plugin then feeds the
// core the division's time in place of the word, until a turn without
// shift (or a sound loading) frees it. The word itself is never rewritten:
// Test 61 round 1 showed the stock knob keeps its own copy of a page-1
// value and writes that back on the next click (a rewrite only held until
// then, so only the divisions next to the knob's own time were reachable).
// The lock is a performance state, not saved with the sound.
struct TimeLock {
  int16_t last[knl::kNumPositions];
  bool seen[knl::kNumPositions];
  volatile int8_t div[knl::kNumPositions];   // the division locked to, -1 free (the screen reads it)

  void Reset(int32_t pos) {
    seen[pos % knl::kNumPositions] = false;
    div[pos % knl::kNumPositions] = -1;
  }

  void Apply(int32_t pos) {
    pos %= knl::kNumPositions;
    const int32_t v = op1_fx_knobs(pos)[0];
    if (!seen[pos]) {
      last[pos] = static_cast<int16_t>(v);
      div[pos] = -1;
      seen[pos] = true;
      return;
    }
    const int32_t prev = last[pos];
    if (v == prev) return;
    last[pos] = static_cast<int16_t>(v);
    const int32_t d = v - prev;
    const int32_t mag = d < 0 ? -d : d;
    if (!knl::ShiftHeld() || mag > 3 * knl::kPageClick || knl::Pressed(pos, 0)) {
      div[pos] = -1;   // a free turn, or a sound loaded
      return;
    }
    const int32_t s96 = NinetySixthQ4(knl::TempoIncrement());
    const int32_t dir = d > 0 ? 1 : -1;
    int32_t target = -1;
    if (div[pos] >= 0) {
      // On a division: the next one that exists in the turn's direction (the end of the list: stay).
      for (int32_t k = div[pos] + dir; k >= 0 && k < kNumDivisions; k += dir) {
        if (DivisionWord(k, s96) >= 0) { target = k; break; }
      }
      if (target < 0) target = div[pos];
    } else {
      // Free: the nearest division beyond where the knob is.
      if (dir > 0) {
        for (int32_t k = 0; k < kNumDivisions; ++k) {
          if (DivisionWord(k, s96) > prev) { target = k; break; }
        }
      } else {
        for (int32_t k = kNumDivisions - 1; k >= 0; --k) {
          const int32_t word = DivisionWord(k, s96);
          if (word >= 0 && word < prev) { target = k; break; }
        }
      }
    }
    if (target >= 0) div[pos] = static_cast<int8_t>(target);
  }

  // The time word the core gets: the division's while locked (and it exists
  // at this tempo), else the knob's.
  int32_t Word(int32_t pos, int32_t knob_word, int32_t s96) const {
    const int32_t k = div[pos % knl::kNumPositions];
    if (k < 0) return knob_word;
    const int32_t word = DivisionWord(k, s96);
    return word < 0 ? knob_word : word;
  }
};

TimeLock lock;

void Init(int pos) {
  fine.Reset(pos);
  lock.Reset(pos);
  Instance* in = knl::Instance<Instance>(pos);
  if (!in) return;
  knl::Zero(in, sizeof(*in));
  in->echo.Init();
  in->half.Stop();
}

void Process(int pos, Instance* in, int16_t* l, int16_t* r, const int16_t* p) {
  if (!in->half.Begin(op1::Cycles())) {
    in->echo.Init();
    in->half.End();
    return;
  }
  int32_t v[4];
  knl::Page1(p, v);
  op1fx::TapeEchoParams q;
  q.time = lock.Word(pos, v[0], NinetySixthQ4(knl::TempoIncrement()));
  q.feedback = v[1];
  q.age = v[2];
  q.heads = Pattern(knl::Word(p, 7));
  q.hold = knl::Pressed(pos, 1);
  in->half.Decimate(l, r);
  // The input gain: what reaches the record head, saturating (the dry
  // side of the mix is the input as it came).
  const int32_t g = knl::GainQ14(knl::Word(p, 5));
  int16_t gl[op1fx::kHalfN], gr[op1fx::kHalfN];
  for (int32_t c = 0; c < (r ? 2 : 1); ++c) {
    const int16_t* src = in->half.in(c);
    int16_t* dst = c ? gr : gl;
    for (int32_t i = 0; i < op1fx::kHalfN; ++i) {
      int32_t y = (src[i] * g) >> 14;
      if (y > 32767) y = 32767;
      if (y < -32768) y = -32768;
      dst[i] = static_cast<int16_t>(y);
    }
  }
  in->echo.Process(gl, r ? gr : nullptr, in->half.wet(0), r ? in->half.wet(1) : nullptr, op1fx::kHalfN, q);
  in->half.Mix(l, r, v[3]);
  in->half.End();
}

void Render(int pos, int16_t* buf, const int16_t* p) {
  fine.Apply(pos, 1, kTenthDb);
  lock.Apply(pos);
  Instance* in = knl::Instance<Instance>(pos);
  if (in) Process(pos, in, buf, nullptr, p);
}

void RenderStereo(int pos, int16_t* l, int16_t* r, const int16_t* p) {
  fine.Apply(pos, 1, kTenthDb);
  lock.Apply(pos);
  Instance* in = knl::Instance<Instance>(pos);
  if (in) Process(pos, in, l, r, p);
}

// ---- screen (graphics thread) ----

using namespace op1fx::screen;

const int32_t kCY = 76, kBottom = 124, kTapeY = 56, kRec = 68;
const int32_t kSpan0Q10 = 97 << 10;          // head 3's drawn distance from the record head
const int32_t kSpanMinQ10 = 32 << 10;        // at time 0; + 130 px at time 1
const int32_t kBoxInset = 10;                // the bracket is span - 10 wide
const int32_t kDotHzMax = 8 << 16;           // the dot's trips a second at the shortest times
const int64_t kDotHzNum = 23121100800LL;     // 16 x 22050 x 65536: Hz (Q16) = this / time_q4
const int32_t kWaveMin = 102;                // the wave at age 0: a tenth (Q10)

const char* const kHeadIds[3] = { "hd1", "hd2", "hd3" };
const char* const kHeadPolyIds[3] = { "hp1", "hp2", "hp3" };
const char* const kTapIds[3] = { "tp1", "tp2", "tp3" };
const char* const kTickIds[24] = { "bt1", "bt2", "bt3", "bt4", "bt5", "bt6", "bt7", "bt8", "bt9", "bt10", "bt11", "bt12",
                                   "bt13", "bt14", "bt15", "bt16", "bt17", "bt18", "bt19", "bt20", "bt21", "bt22", "bt23", "bt24" };
const char* const kDivIds[kNumDivisions] = { "dv0", "dv1", "dv2", "dv3", "dv4", "dv5", "dv6", "dv7", "dv8", "dv9", "dv10" };
const char* const kPatIds[4] = { "pt0", "pt1", "pt2", "pt3" };
const char* const kFbIds[4] = { "fb1", "fb2", "fb3", "fb4" };
const char* const kSignIds[2] = { "gs0", "gs1" };
const char* const kDigitIds[7] = { "gd0", "gd1", "gd2", "gd3", "gd4", "gd5", "gd6" };
const char* const kTenthIds[10] = { "gt0", "gt1", "gt2", "gt3", "gt4", "gt5", "gt6", "gt7", "gt8", "gt9" };
const char* const kNodeIds[7] = { "wbox", "tape", "dot", "gbar", "gbox", "gdot", "mbar" };
enum { N_WBOX, N_TAPE, N_DOT, N_GBAR, N_GBOX, N_GDOT, N_MBAR };
const char* const kHoldLineIds[6] = { "inl", "ina", "rech", "ret1", "ret2", "ret3" };

bool ready;
void* head_nodes[3];
void* tick_nodes[24];
void* div_nodes[kNumDivisions];
void* pat_nodes[4];
void* fb_nodes[4];
void* sign_nodes[2];
void* digit_nodes[7];
void* tenth_nodes[10];
void* nodes[7];
uint8_t* head_line[3];
uint8_t* head_fill[3];
uint8_t* tap_line[3];
uint8_t* hold_lines[6];
uint8_t* hold_glyph;
Colour head_line_on, head_line_off, head_fill_on, head_fill_off, tap_on, tap_off;
Colour mid, dim, green;
op1fx::FrameClock clock;
uint32_t dot_phase, wow_phase;

void ScreenInit(void* scr) {
  ready = false;
  void* document = DocumentOf(scr);
  if (!document) return;
  void* tmp[6];
  ready = FindAll(document, kHeadIds, head_nodes, 3) && FindAll(document, kHeadPolyIds, tmp, 3) &&
          FindAll(document, kTapIds, tmp, 3) && FindAll(document, kTickIds, tick_nodes, 24) &&
          FindAll(document, kDivIds, div_nodes, kNumDivisions) && FindAll(document, kPatIds, pat_nodes, 4) &&
          FindAll(document, kFbIds, fb_nodes, 4) && FindAll(document, kSignIds, sign_nodes, 2) &&
          FindAll(document, kDigitIds, digit_nodes, 7) && FindAll(document, kTenthIds, tenth_nodes, 10) &&
          FindAll(document, kNodeIds, nodes, 7) && FindAll(document, kHoldLineIds, tmp, 6) && Find(document, "hold");
  if (!ready) return;
  for (int32_t i = 0; i < 3; ++i) {
    head_line[i] = LineStyle(scr, kHeadPolyIds[i]);
    head_fill[i] = FillStyle(scr, kHeadPolyIds[i]);
    tap_line[i] = LineStyle(scr, kTapIds[i]);
  }
  for (int32_t i = 0; i < 6; ++i) hold_lines[i] = LineStyle(scr, kHoldLineIds[i]);
  hold_glyph = LineStyle(scr, "hold");
  // The file ships in pattern 0: head 3 lit, heads 1 and 2 off.
  ReadColour(&head_line_on, head_line[2]);
  ReadColour(&head_fill_on, head_fill[2]);
  ReadColour(&head_line_off, head_line[0]);
  ReadColour(&head_fill_off, head_fill[0]);
  ReadColour(&tap_on, tap_line[2]);
  ReadColour(&tap_off, tap_line[0]);
  ReadColour(&mid, LineStyle(scr, "palmid"));
  ReadColour(&dim, LineStyle(scr, "paldim"));
  ReadColour(&green, LineStyle(scr, "palgreen"));
  dot_phase = wow_phase = 0;
}

void ScreenUpdate(void* scr, int dt) {
  if (ready) {
    const int32_t pos = knl::ScreenPos();
    int32_t v[4];
    knl::ScreenPage1(v);   // 0..32767 as the stock screens see them (LFO included)
    const int32_t gain = knl::ScreenWord(5);
    const int32_t pattern = Pattern(knl::ScreenWord(7));
    const bool blue2 = knl::Pressed(pos, 0);
    const bool hold = knl::Pressed(pos, 1);
    const bool white2 = knl::Pressed(pos, 2);
    const uint32_t cycles = clock.Tick();

    // Time (the division's while locked): the heads' distance from the record head, the dot's speed, the beat ticks.
    const int32_t s96 = NinetySixthQ4(knl::TempoIncrement());
    const int32_t div = lock.div[pos % knl::kNumPositions];
    const int32_t time = lock.Word(pos, v[0], s96);
    const int32_t span = kSpanMinQ10 + ((time * 4160) >> 10);   // 32 + 130 t px, Q10
    const int32_t d3 = span - kSpan0Q10;
    const int32_t d1 = d3 / 3;
    SetTranslate(head_nodes[0], d1, 0);
    SetTranslate(head_nodes[1], 2 * d1, 0);
    SetTranslate(head_nodes[2], d3, 0);
    const int32_t time_q4 = op1fx::TapeEchoTimeSamplesQ4(time);
    int64_t hz = kDotHzNum / time_q4;
    if (hz > kDotHzMax) hz = kDotHzMax;
    dot_phase += op1fx::PhaseStep(static_cast<int32_t>(hz), cycles);
    SetTranslate(nodes[N_DOT], static_cast<int32_t>((static_cast<int64_t>(dot_phase >> 22) * span) >> 10), 0);
    const bool ticks = knl::ShiftHeld() && !blue2;
    for (int32_t k = 1; k <= 24; ++k) {
      const int64_t at = static_cast<int64_t>(s96) * 6 * k;   // the kth 16th, Q4 samples
      const bool show = ticks && at <= time_q4;
      SetVisible(tick_nodes[k - 1], show);
      if (show) SetTranslate(tick_nodes[k - 1], static_cast<int32_t>(at * span / time_q4), 0);
    }

    // The heads lit per pattern; the bracket while white is on page 2.
    const int32_t heads = op1fx::kTapeEchoPatternHeads[pattern];
    for (int32_t i = 0; i < 3; ++i) {
      const bool on = (heads >> i) & 1;
      WriteColour(head_line[i], on ? head_line_on : head_line_off);
      WriteColour(head_fill[i], on ? head_fill_on : head_fill_off);
      WriteColour(tap_line[i], on ? tap_on : tap_off);
    }
    SetVisible(nodes[N_WBOX], white2);
    if (white2) {
      ScaleX(nodes[N_WBOX], static_cast<int32_t>((static_cast<int64_t>(span - (kBoxInset << 10)) << 10) / (kSpan0Q10 - (kBoxInset << 10))), kRec + 18);
    }

    // Age: the wave's amplitude, breathing with the wow (0.5 .. 2 Hz, a quarter either way).
    wow_phase += op1fx::PhaseStep((1 << 15) + ((v[2] * 3) << 1), cycles);   // 0.5 Hz + 1.5 Hz x age (Q16)
    int32_t sy = v[2] >> 5;
    if (sy < kWaveMin) sy = kWaveMin;
    sy = (sy * (0x400 + (op1fx::SinQ15(wow_phase) >> 7))) >> 10;
    SetMatrix(nodes[N_TAPE], 0x400, sy, 0, kTapeY * (0x400 - sy));

    // Feedback: the chevrons (all four while holding); the hold colours.
    ShowFirst(fb_nodes, 4, hold ? 4 : Steps(v[1], 4));
    for (int32_t i = 0; i < 3; ++i) WriteColour(hold_lines[i], hold ? dim : mid);       // the input and the record head dim
    for (int32_t i = 3; i < 6; ++i) WriteColour(hold_lines[i], hold ? green : mid);     // the return path lights
    WriteColour(hold_glyph, hold ? green : mid);

    // Gain: the bar scaled -1..1 about the centre; page 2 shows the bracket and the readout.
    const int32_t d = (gain - 16384) >> 4;   // Q10, -1024..1023
    SetMatrix(nodes[N_GBAR], 0x400, d, 0, kCY * (0x400 - d));
    SetVisible(nodes[N_GBOX], blue2);
    SetVisible(nodes[N_GDOT], blue2);
    const int32_t tenths = (gain * 120 + 16383) / 32767 - 60;
    const int32_t mag = tenths < 0 ? -tenths : tenths;
    ShowOne(sign_nodes, 2, blue2 ? (tenths < 0 ? 1 : 0) : -1);
    ShowOne(digit_nodes, 7, blue2 ? mag / 10 : -1);
    ShowOne(tenth_nodes, 10, blue2 ? mag % 10 : -1);
    // The other readouts: the pattern while white is on page 2, else the
    // division locked to, if any.
    ShowOne(pat_nodes, 4, !blue2 && white2 ? pattern : -1);
    ShowOne(div_nodes, kNumDivisions, !blue2 && !white2 ? div : -1);
    // Mix: the bar scaled about its foot.
    const int32_t s = v[3] >> 5;
    SetMatrix(nodes[N_MBAR], 0x400, s < 8 ? 8 : s, 0, kBottom * (0x400 - (s < 8 ? 8 : s)));
  }
  op1_screen_draw(scr, dt);
}

}  // namespace

extern "C" const op1_fx_stereo_t FX_tape_echo = {
  { Init, Render, { OP1_PRESS_PAGE, OP1_PRESS_LATCH, OP1_PRESS_PAGE, OP1_PRESS_NONE } },
  RenderStereo,
};

extern "C" const op1_screen_t SCREEN_tape_echo = { ScreenInit, ScreenUpdate };
