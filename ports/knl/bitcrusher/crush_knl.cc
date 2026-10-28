// CRUSH as a kennel effect plugin, id bitcrusher (hundekraft ships its own
// "crush", so not that id; the user's name, 2026-10-27): the same DSP as the
// firmware build (fx/src/crush/crush.h) behind kennel's op1_fx_stereo_t, plus
// an input gain. One instance per position (synth, drum, master); the master
// gets the stereo call, the instruments the mono one.
//
// Knobs (raw filter-slot words, normalised with op1_fx_param_q15):
//   blue rate, green bits, white jitter, orange mix.
// Blue and green turn the stock way, clockwise = more: a higher sample rate,
// more bits (the user, 2026-10-27). The core's rate and bits words are the
// amount of crushing, so both are the knob's word mirrored (32767 - v), in
// the render and on the screen alike; the defaults are mirrored too.
// Blue press: page 2, where blue sets the input gain (word 5: -6 dB at 0,
// 0 dB at the centre, +6 dB at the top, saved with the sound); shift + blue
// there moves it a tenth of a dB a click (knl::Fine). The gain drives the
// crusher only: the dry side of the mix is the signal as it came.
//
// The screen (gen_crush_knl_svg.py, the canvas "Bitcrusher screen", option C):
//   stair0..stair7, clk{k}a/b   variant set by rate: the staircase and its
//                               clock row (the two halves jittered apart)
//   bit0..bit7                  count set, 8 - round(7 x bits) (16 bits: 8)
//   dot                         the sampling dot, running the steps
//   gbar, gzero, gbox, gs/gd/gt/gdot   the gain gauge: the bar scaled -1..1
//                               about the centre, its colour warming with
//                               the input level; the bracket and readout
//                               while blue is on page 2
//   mbar                        the mix gauge, scaled about its foot

extern "C" {
#include "op1_plugin.h"
}

#include "common/knl_fx.h"
#include "fx/op1/fx_screen.h"
#include "fx/src/crush/crush.h"

namespace {

struct Instance {
  op1fx::Crush crush;
};

Instance instances[3];   // zero-initialised: no constructor runs
knl::Fine fine;
const int32_t kTenthDb = 273;   // 32767 / 120: one tenth of a dB of word 5, the readout's unit

// The input level per position for the screen (the block's peak, decaying).
volatile int16_t in_level[3];

op1fx::CrushParams ParamsFromRaw(const int16_t* p) {
  op1fx::CrushParams q;
  q.rate = 32767 - op1_fx_param_q15(p, 0);   // clockwise = a higher rate
  q.bits = 32767 - op1_fx_param_q15(p, 1);   // clockwise = more bits
  q.jitter = op1_fx_param_q15(p, 2);
  q.mix = op1_fx_param_q15(p, 3);
  q.gain_q14 = knl::GainQ14(knl::Word(p, 5));   // common/knl_fx.h
  return q;
}

void Meter(int pos, const int16_t* a, const int16_t* b, int32_t n) {
  int32_t peak = 0;
  for (int32_t i = 0; i < n; ++i) {
    int32_t x = a[i] < 0 ? -a[i] : a[i];
    if (b) {
      const int32_t y = b[i] < 0 ? -b[i] : b[i];
      if (y > x) x = y;
    }
    if (x > peak) peak = x;
  }
  const int32_t held = in_level[pos] - (in_level[pos] >> 3);
  in_level[pos] = static_cast<int16_t>(peak > held ? peak : held);
}

void Init(int pos) {
  instances[pos % 3].crush.Init();
  fine.Reset(pos);
}

void Render(int pos, int16_t* buf, const int16_t* p) {
  fine.Apply(pos, 1, kTenthDb);
  Meter(pos % 3, buf, nullptr, OP1_FX_BLOCK);
  instances[pos % 3].crush.Process(buf, nullptr, OP1_FX_BLOCK, ParamsFromRaw(p));
}

void RenderStereo(int pos, int16_t* l, int16_t* r, const int16_t* p) {
  fine.Apply(pos, 1, kTenthDb);
  Meter(pos % 3, l, r, OP1_FX_BLOCK);
  instances[pos % 3].crush.Process(l, r, OP1_FX_BLOCK, ParamsFromRaw(p));
}

// ---- screen (graphics thread) ----

using namespace op1fx::screen;

const int32_t kX0 = 40, kX1 = 288, kCY = 76, kBottom = 124, kAmp = 48;
const int32_t kGainBar = 24;
// The variants' step widths x 10 (gen_crush_knl_svg.py STEP_WIDTHS).
const int32_t kStepW10[8] = { 80, 106, 139, 184, 243, 321, 423, 559 };
// 48 sin(2 pi i / 64), the reference sine's height per 1/64 of the span.
const int32_t kSine48[64] = {
  0, 5, 9, 14, 18, 22, 27, 30, 34, 37, 40, 42, 44, 46, 47, 48, 48, 48, 47, 46, 44, 42, 40, 37, 34, 30, 27, 22, 18, 14, 9, 5,
  0, -5, -9, -14, -18, -22, -27, -30, -34, -37, -40, -42, -44, -46, -47, -48, -48, -48, -47, -46, -44, -42, -40, -37, -34, -30, -27, -22, -18, -14, -9, -5 };

const char* const kStairIds[8] = { "stair0", "stair1", "stair2", "stair3", "stair4", "stair5", "stair6", "stair7" };
const char* const kClkAIds[8] = { "clk0a", "clk1a", "clk2a", "clk3a", "clk4a", "clk5a", "clk6a", "clk7a" };
const char* const kClkBIds[8] = { "clk0b", "clk1b", "clk2b", "clk3b", "clk4b", "clk5b", "clk6b", "clk7b" };
const char* const kBitIds[8] = { "bit0", "bit1", "bit2", "bit3", "bit4", "bit5", "bit6", "bit7" };
const char* const kSignIds[2] = { "gs0", "gs1" };
const char* const kDigitIds[7] = { "gd0", "gd1", "gd2", "gd3", "gd4", "gd5", "gd6" };
const char* const kTenthIds[10] = { "gt0", "gt1", "gt2", "gt3", "gt4", "gt5", "gt6", "gt7", "gt8", "gt9" };
const char* const kNodeIds[5] = { "dot", "gbar", "gbox", "gdot", "mbar" };
const char* const kPaletteIds[8] = { "palwhite", "paldim", "palblue", "palgreen", "paldimgreen", "palorange", "palblack", "palmid" };
enum { N_DOT, N_GBAR, N_GBOX, N_GDOT, N_MBAR };
enum { C_WHITE, C_DIM, C_BLUE, C_GREEN, C_DIMGREEN, C_ORANGE, C_BLACK, C_MID };

bool ready;
void* stair_nodes[8];
void* clk_a[8];
void* clk_b[8];
void* bit_nodes[8];
void* sign_nodes[2];
void* digit_nodes[7];
void* tenth_nodes[10];
void* nodes[5];
uint8_t* stair_styles[8];
uint8_t* gbar_fill;
Colour pal[8];
uint32_t frame_rng;
uint32_t dot_phase;   // Q16 steps

Colour Blend(const Colour& a, const Colour& b, int32_t v) {
  Colour c;
  for (int32_t i = 0; i < 4; ++i) c.rgba[i] = static_cast<uint8_t>(a.rgba[i] + (((b.rgba[i] - a.rgba[i]) * v) >> 15));
  return c;
}

inline int32_t Clamp15(int32_t v) { return v > 32767 ? 32767 : v; }

void ScreenInit(void* scr) {
  ready = false;
  void* document = DocumentOf(scr);
  if (!document) return;
  frame_rng = 1;
  dot_phase = 0;
  void* tmp[8];
  if (!FindAll(document, kStairIds, stair_nodes, 8) || !FindAll(document, kClkAIds, clk_a, 8) ||
      !FindAll(document, kClkBIds, clk_b, 8) || !FindAll(document, kBitIds, bit_nodes, 8) ||
      !FindAll(document, kSignIds, sign_nodes, 2) || !FindAll(document, kDigitIds, digit_nodes, 7) ||
      !FindAll(document, kTenthIds, tenth_nodes, 10) || !FindAll(document, kNodeIds, nodes, 5) ||
      !FindAll(document, kPaletteIds, tmp, 8)) {
    return;
  }
  for (int32_t i = 0; i < 8; ++i) {
    ReadColour(&pal[i], LineStyle(scr, kPaletteIds[i]));
    stair_styles[i] = LineStyle(scr, kStairIds[i]);
  }
  gbar_fill = FillStyle(scr, "gbar");
  ready = true;
}

void ScreenUpdate(void* scr, int dt) {
  if (ready) {
    const int32_t pos = knl::ScreenPos();
    int32_t v[4];
    knl::ScreenPage1(v);   // 0..32767 as the stock screens see them (LFO included)
    const int32_t rate = 32767 - v[0], bits = 32767 - v[1], jitter = v[2], mix = v[3];   // mirrored, as the render
    const int32_t gain = knl::ScreenWord(5);
    const bool page2 = knl::Pressed(pos, 0);
    const int32_t level = Clamp15(in_level[pos % 3] * 4);

    // Rate: the staircase and its clock row, dimmed while blue is on page 2.
    const int32_t k = Steps(rate, 7);
    ShowOne(stair_nodes, 8, k);
    ShowOne(clk_a, 8, k);
    ShowOne(clk_b, 8, k);
    WriteColour(stair_styles[k], page2 ? pal[C_DIM] : pal[C_BLUE]);
    // Bits: the ladder.
    ShowFirst(bit_nodes, 8, 8 - Steps(bits, 7));
    // Jitter: the clock row's two halves shaken apart, up to 4 px.
    frame_rng = frame_rng * 1664525u + 1013904223u;
    const int32_t noise = static_cast<int32_t>(frame_rng >> 28) - 8;   // -8..7
    // jitter x noise is at most 32767 x 8 = 2^18; >> 6 leaves 4096 = 4 px in
    // Q10 (>> 14 left 0.016 px: the row never visibly moved, 2026-10-27).
    const int32_t shake = (jitter * noise) >> 6;                         // Q10 px, |.| <= 4 px
    SetTranslate(clk_a[k], shake, 0);
    SetTranslate(clk_b[k], -shake, 0);
    // The sampling dot: one step per tick of the clock, faster at higher rates.
    dot_phase += 1800 + 400 * k;
    const int32_t steps = (kX1 - kX0) * 10 / kStepW10[k] + 1;
    const int32_t step = static_cast<int32_t>((dot_phase >> 16) % static_cast<uint32_t>(steps));
    const int32_t x10 = kX0 * 10 + step * kStepW10[k];                  // x 10
    const int32_t i64 = ((x10 - kX0 * 10) * 64 / ((kX1 - kX0) * 10)) & 63;
    const int32_t level_y = kCY - ((kSine48[i64] + (kSine48[i64] >= 0 ? 6 : -6)) / 12) * 12;
    SetTranslate(nodes[N_DOT], (x10 - kX0 * 10) * 1024 / 10, (level_y - kCY) * 1024);
    // Gain: the bar scaled -1..1 about the centre; its colour warms with the input.
    const int32_t d = (gain - 16384) >> 4;                               // Q10, -1024..1023
    SetMatrix(nodes[N_GBAR], 0x400, d, 0, kCY * (0x400 - d));
    WriteColour(gbar_fill, Blend(pal[C_MID], pal[C_WHITE], level));
    // Page 2: the bracket and the readout (tenths of a dB, -60..60).
    SetVisible(nodes[N_GBOX], page2);
    SetVisible(nodes[N_GDOT], page2);
    const int32_t tenths = (gain * 120 + 16383) / 32767 - 60;
    const int32_t mag = tenths < 0 ? -tenths : tenths;
    ShowOne(sign_nodes, 2, page2 ? (tenths < 0 ? 1 : 0) : -1);
    ShowOne(digit_nodes, 7, page2 ? mag / 10 : -1);
    ShowOne(tenth_nodes, 10, page2 ? mag % 10 : -1);
    // Mix: the bar scaled about its foot.
    const int32_t s = mix >> 5;
    SetMatrix(nodes[N_MBAR], 0x400, s < 8 ? 8 : s, 0, kBottom * (0x400 - (s < 8 ? 8 : s)));
  }
  op1_screen_draw(scr, dt);
}

}  // namespace

// The descriptors the packer looks up by name: a const object needs the
// single-declaration form of extern "C" to get external linkage (inside an
// extern "C" { } block it would stay internal).
extern "C" const op1_fx_stereo_t FX_crush = {
  { Init, Render, { OP1_PRESS_PAGE, OP1_PRESS_NONE, OP1_PRESS_NONE, OP1_PRESS_NONE } },
  RenderStereo,
};

extern "C" const op1_screen_t SCREEN_crush = { ScreenInit, ScreenUpdate };
