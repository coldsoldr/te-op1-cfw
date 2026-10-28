// SHIFT (frequency shifter / ring modulator) as a kennel effect plugin, id
// freq_shift (was cs_shift; the user's name, 2026-10-27). The DSP of
// fx/src/shift/shift.h at the effect rate, one instance per position in
// its scratch buffer (common/knl_fx.h). Knobs: blue shift (centre = 0 Hz),
// green feedback, white spread (master only), orange mix.
//   Blue press:  page 2, where blue sets the input gain (word 5: -6 dB at
//                0, 0 dB at the centre, +6 dB at the top, saved with the
//                sound; shift + blue a tenth of a dB a click, knl::Fine).
//                The gain scales the wet path only (the analytic signal
//                after the Hilbert, ShiftParams::gain): the dry side of the
//                mix doesn't see it.
//   Green press: page 2, where green sets the mode (word 6, one click flips
//                it: shifter / ring modulation). The mode was blue's page 2
//                in cs_shift; blue's is the gain on every module now.
//
// The screen (gen_shift_knl_svg.py, the canvas "Freq shift screen" design
// B, "Dial", 2026-10-27):
//   ndlL, ndlR         the needles, rotated about the hub: 78 degrees each
//                      way for +-2 kHz (L at shift + spread, R at shift -
//                      spread on the master; one needle elsewhere; in ring
//                      mode both, mirrored about the top)
//   sp1..sp7           the spread arc, a variant set by the spread, rotated
//                      to the shift's angle (every position, shifter mode;
//                      on the master it spans the needles)
//   mbox               the bracket round the mode marks while green is on
//                      page 2
//   rp1..rp4           the sound's ripples: arcs scaled from r 20 out to the
//                      rim and faded, born at the hub at the input level
//   fu1..fu4           feedback, a count set: arcs sweeping right from the
//                      top, mirrored about the hub for a shift down
//   fr1..fr4           feedback in ring mode: arcs centred on the top
//   modea1, modea2, moder   the mode marks' colours
//   gbar, gzero, gbox, gs/gd/gdot/gt   the gain gauge, bracket and readout
//   mbar               the mix gauge, scaled about its foot
// The needles' rotation is a full affine (RotateAbout): the renderer's
// transform is a 3 x 3 matrix, the first use of its b and c terms.

#include "common/knl_fx.h"
#include "fx/op1/fx_screen.h"
#include "fx/op1/screen_anim.h"
#include "fx/src/shift/shift.h"

namespace {

struct Instance {
  op1fx::FreqShift shift;
};

knl::Fine fine;
const int32_t kTenthDb = 273;   // 32767 / 120: one tenth of a dB of word 5, the readout's unit

// The input level per position for the screen's ripples (the block's peak, decaying).
volatile int16_t in_level[3];

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

bool Ring(int32_t word6) { return knl::ClickMode(word6, 2) == 1; }   // one click flips the mode

op1fx::ShiftParams Params(const int16_t* p) {
  int32_t v[4];
  knl::Page1(p, v);
  op1fx::ShiftParams q;
  q.shift = v[0];
  q.feedback = v[1];
  q.spread = v[2];
  q.mix = v[3];
  q.ring = Ring(knl::Word(p, 6));
  q.gain = knl::GainQ14(knl::Word(p, 5));
  return q;
}

void Init(int pos) {
  fine.Reset(pos);
  Instance* in = knl::Instance<Instance>(pos);
  if (!in) return;
  knl::Zero(in, sizeof(*in));
  in->shift.Init();
}

void Render(int pos, int16_t* buf, const int16_t* p) {
  fine.Apply(pos, 1, kTenthDb);
  Meter(pos % 3, buf, nullptr, knl::kBlock);
  Instance* in = knl::Instance<Instance>(pos);
  if (!in) return;
  in->shift.Process(buf, nullptr, knl::kBlock, Params(p));
}

void RenderStereo(int pos, int16_t* l, int16_t* r, const int16_t* p) {
  fine.Apply(pos, 1, kTenthDb);
  Meter(pos % 3, l, r, knl::kBlock);
  Instance* in = knl::Instance<Instance>(pos);
  if (!in) return;
  in->shift.Process(l, r, knl::kBlock, Params(p));
}

// ---- screen (graphics thread) ----

using namespace op1fx::screen;

const int32_t kCY = 76, kBottom = 124, kHubX = 160, kHubY = 124;
const int32_t kRippleScale = 4096;      // a ripple grows from r 20 to the rim: x5 = 1 + 4096/1024
const int32_t kRippleHz = 78643;        // 1.2 Hz (Q16): a ripple's journey takes 0.8 s
const int32_t kRippleGate = 1500;       // the level (0..32767) under which no ripple starts
const uint32_t kRippleGap = 0x30000000u;   // the youngest ripple's phase before the next may start
// 78 degrees as a Q32 turn: 78 / 360 x 2^32.
const uint32_t kSweepTurn = 930537128u;

const char* const kRpIds[4] = { "rp1", "rp2", "rp3", "rp4" };
const char* const kFuIds[4] = { "fu1", "fu2", "fu3", "fu4" };
const char* const kFrIds[4] = { "fr1", "fr2", "fr3", "fr4" };
const char* const kSpIds[7] = { "sp1", "sp2", "sp3", "sp4", "sp5", "sp6", "sp7" };
const char* const kSignIds[2] = { "gs0", "gs1" };
const char* const kDigitIds[7] = { "gd0", "gd1", "gd2", "gd3", "gd4", "gd5", "gd6" };
const char* const kTenthIds[10] = { "gt0", "gt1", "gt2", "gt3", "gt4", "gt5", "gt6", "gt7", "gt8", "gt9" };
const char* const kNodeIds[7] = { "ndlL", "ndlR", "gbar", "gbox", "gdot", "mbar", "mbox" };
const char* const kModeIds[3] = { "modea1", "modea2", "moder" };
enum { N_NDLL, N_NDLR, N_GBAR, N_GBOX, N_GDOT, N_MBAR, N_MBOX };

bool ready;
void* rp_nodes[4];
uint8_t* rp_lines[4];
uint32_t rp_phase[4];
int32_t rp_amp[4];
int32_t rp_young;   // the ripple started last
Colour rp_cold, rp_hot;
op1fx::FrameClock clock;
void* fu_nodes[4];
void* fr_nodes[4];
void* sp_nodes[7];
void* sign_nodes[2];
void* digit_nodes[7];
void* tenth_nodes[10];
void* nodes[7];
uint8_t* mode_lines[3];
Colour mode_on, mode_off;

Colour Blend(const Colour& a, const Colour& b, int32_t v) {
  Colour c;
  for (int32_t i = 0; i < 4; ++i) c.rgba[i] = static_cast<uint8_t>(a.rgba[i] + (((b.rgba[i] - a.rgba[i]) * v) >> 15));
  return c;
}

// Rotate a node by the Q32 turn `turn` (clockwise on the screen) about
// (cx, cy): matrix(cos, sin, -sin, cos, e, f).
void RotateAbout(void* node, uint32_t turn, int32_t cx, int32_t cy) {
  const int32_t c = op1fx::CosQ15(turn) >> 5;   // Q10
  const int32_t s = op1fx::SinQ15(turn) >> 5;
  const int32_t e = cx * (0x400 - c) + cy * s;
  const int32_t f = cy * (0x400 - c) - cx * s;
  int32_t m[9] = { c, s, 0, -s, c, 0, e, f, 0x400 };
  reinterpret_cast<SetTransformFn>(kSetTransform)(node, m);
}

// A needle's turn for a position -1..1 (Q15): 78 degrees each way.
uint32_t Turn(int32_t f15) {
  return static_cast<uint32_t>(static_cast<int64_t>(f15) * static_cast<int32_t>(kSweepTurn) >> 15);
}

void ScreenInit(void* scr) {
  ready = false;
  void* document = DocumentOf(scr);
  if (!document) return;
  void* tmp[3];
  ready = FindAll(document, kRpIds, rp_nodes, 4) && FindAll(document, kFuIds, fu_nodes, 4) && FindAll(document, kFrIds, fr_nodes, 4) &&
          FindAll(document, kSpIds, sp_nodes, 7) && FindAll(document, kSignIds, sign_nodes, 2) &&
          FindAll(document, kDigitIds, digit_nodes, 7) && FindAll(document, kTenthIds, tenth_nodes, 10) &&
          FindAll(document, kNodeIds, nodes, 7) && FindAll(document, kModeIds, tmp, 3);
  if (!ready) return;
  for (int32_t i = 0; i < 3; ++i) mode_lines[i] = LineStyle(scr, kModeIds[i]);
  ReadColour(&mode_on, mode_lines[0]);    // the file ships in shifter mode: the arrow green
  ReadColour(&mode_off, mode_lines[2]);   // the ring dim
  for (int32_t i = 0; i < 4; ++i) {
    rp_lines[i] = LineStyle(scr, kRpIds[i]);
    rp_amp[i] = 0;
  }
  rp_young = -1;
  ReadColour(&rp_hot, LineStyle(scr, "rp1"));   // the ripples' colour at birth (palmid)
  ReadColour(&rp_cold, LineStyle(scr, "palblack"));
}

void ScreenUpdate(void* scr, int dt) {
  if (ready) {
    const int32_t pos = knl::ScreenPos();
    int32_t v[4];
    knl::ScreenPage1(v);   // 0..32767 as the stock screens see them (LFO included)
    const int32_t gain = knl::ScreenWord(5);
    const bool ring = Ring(knl::ScreenWord(6));
    const bool page2 = knl::Pressed(pos, 0);
    const bool stereo = pos == OP1_FX_MASTER;

    // The mode marks, and the bracket round them while green is on page 2.
    WriteColour(mode_lines[0], ring ? mode_off : mode_on);
    WriteColour(mode_lines[1], ring ? mode_off : mode_on);
    WriteColour(mode_lines[2], ring ? mode_on : mode_off);
    SetVisible(nodes[N_MBOX], knl::Pressed(pos, 1));

    // Shift: -1..1 (Q15). The spread parts the needles a third of the sweep
    // each way on the master only (where it acts); its arc shows the knob
    // everywhere.
    int32_t f = (v[0] - 16384) * 2;
    if (f > 32767) f = 32767;
    const int32_t af = f < 0 ? -f : f;
    const int32_t sp = v[2];
    const int32_t half = stereo ? (sp * 10923) >> 15 : 0;   // spread / 3
    if (ring) {
      // The carriers: L at |shift| + spread/3, R at |shift| - spread/3 (the
      // master), mirrored about the top.
      int32_t cl = af + half, cr = af - half;
      if (cl > 32767) cl = 32767;
      if (cr < 0) cr = 0;
      RotateAbout(nodes[N_NDLL], Turn(cl), kHubX, kHubY);
      RotateAbout(nodes[N_NDLR], Turn(-cr), kHubX, kHubY);
      SetVisible(nodes[N_NDLR], true);
    } else {
      int32_t fl = f + half, fr = f - half;
      if (fl > 32767) fl = 32767;
      if (fr < -32767) fr = -32767;
      RotateAbout(nodes[N_NDLL], Turn(fl), kHubX, kHubY);
      RotateAbout(nodes[N_NDLR], Turn(fr), kHubX, kHubY);
      SetVisible(nodes[N_NDLR], stereo);
    }
    // Spread: the arc (a variant by the spread), turned to the shift (to the
    // carrier in ring mode); on the master it spans the needles.
    const int32_t k = Steps(sp, 7);
    ShowOne(sp_nodes, 7, k - 1);
    if (k > 0) RotateAbout(sp_nodes[k - 1], Turn(ring ? af : f), kHubX, kHubY);
    // The sound's ripples: born at the hub when the input is up, growing to
    // the rim over 0.8 s and fading, at most one every fifth of a journey.
    const int32_t level = knl::Clamp15(in_level[pos % 3] * 4);
    const uint32_t step = op1fx::PhaseStep(kRippleHz, clock.Tick());
    for (int32_t i = 0; i < 4; ++i) {
      if (rp_amp[i] == 0) continue;
      const uint32_t next = rp_phase[i] + step;
      if (next < rp_phase[i]) {   // wrapped: the journey is over
        rp_amp[i] = 0;
        SetVisible(rp_nodes[i], false);
        continue;
      }
      rp_phase[i] = next;
      const int32_t grow = static_cast<int32_t>((static_cast<uint64_t>(next) * kRippleScale) >> 32);   // Q10, 0..4096
      const int32_t s = 0x400 + grow;
      SetMatrix(rp_nodes[i], s, s, kHubX * (0x400 - s), kHubY * (0x400 - s));
      const int32_t life = 32767 - static_cast<int32_t>(next >> 17);                                  // Q15, fading
      WriteColour(rp_lines[i], Blend(rp_cold, rp_hot, (life * rp_amp[i]) >> 15));
    }
    if (level > kRippleGate && (rp_young < 0 || rp_amp[rp_young] == 0 || rp_phase[rp_young] > kRippleGap)) {
      for (int32_t i = 0; i < 4; ++i) {
        if (rp_amp[i] == 0) {
          rp_amp[i] = level;
          rp_phase[i] = 0;
          rp_young = i;
          SetMatrix(rp_nodes[i], 0x400, 0x400, 0, 0);
          WriteColour(rp_lines[i], Blend(rp_cold, rp_hot, level));
          SetVisible(rp_nodes[i], true);
          break;
        }
      }
    }
    // Feedback: the arcs, the way the sound climbs.
    const int32_t count = Steps(v[1], 4);
    ShowFirst(fu_nodes, 4, ring ? 0 : count);
    ShowFirst(fr_nodes, 4, ring ? count : 0);
    for (int32_t i = 0; i < 4; ++i) {
      if (f < 0) {
        SetMatrix(fu_nodes[i], -0x400, 0x400, (2 * kHubX) << 10, 0);   // mirrored about the hub
      } else {
        SetTranslate(fu_nodes[i], 0, 0);
      }
    }
    // Gain: the bar scaled -1..1 about the centre; page 2 shows the bracket and the readout.
    const int32_t d = (gain - 16384) >> 4;   // Q10, -1024..1023
    SetMatrix(nodes[N_GBAR], 0x400, d, 0, kCY * (0x400 - d));
    SetVisible(nodes[N_GBOX], page2);
    SetVisible(nodes[N_GDOT], page2);
    const int32_t tenths = (gain * 120 + 16383) / 32767 - 60;
    const int32_t mag = tenths < 0 ? -tenths : tenths;
    ShowOne(sign_nodes, 2, page2 ? (tenths < 0 ? 1 : 0) : -1);
    ShowOne(digit_nodes, 7, page2 ? mag / 10 : -1);
    ShowOne(tenth_nodes, 10, page2 ? mag % 10 : -1);
    // Mix: the bar scaled about its foot.
    const int32_t s = v[3] >> 5;
    SetMatrix(nodes[N_MBAR], 0x400, s < 8 ? 8 : s, 0, kBottom * (0x400 - (s < 8 ? 8 : s)));
  }
  op1_screen_draw(scr, dt);
}

}  // namespace

extern "C" const op1_fx_stereo_t FX_shift = {
  { Init, Render, { OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_NONE, OP1_PRESS_NONE } },
  RenderStereo,
};

extern "C" const op1_screen_t SCREEN_shift = { ScreenInit, ScreenUpdate };
