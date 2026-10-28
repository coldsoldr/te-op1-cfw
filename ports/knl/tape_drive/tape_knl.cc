// TAPE (tape saturation) as a kennel effect plugin, id tape_drive ("tape drive" in the list, the user's name 2026-10-28; was tapesat, and
// cs_tape; hundekraft ships its own `tape`, so not that: the user's name,
// 2026-10-27). The DSP of fx/src/tape/tape.h at the effect rate, one
// instance per position in its scratch buffer (common/knl_fx.h). Knobs:
// blue saturate, green wobble, white age, orange mix. White turns the
// stock way, clockwise = more: a newer, brighter tape (the user, 2026-10-27),
// so the core's age (the wear) is the knob's word mirrored, in the render
// and on the screen alike; the default is mirrored too. Blue press: page 2,
// where blue sets the input gain (word 5: -6 dB at 0, 0 dB at the centre,
// +6 dB at the top, saved with the sound; shift + blue a tenth of a dB a
// click, knl::Fine). The gain adds to the saturation drive's exponent only
// (TapeParams::gain_oct): the level make-up and the dry side of the mix
// don't see it, so at mix 0 the sound doesn't change.
//
// The screen (gen_tape_drive_knl_svg.py, the canvas "Tape screen" design A,
// "Reels, aligned", 2026-10-27):
//   sat0..sat7         variant set by saturate: the wave on the tape
//   reelL, reelR       the hubs, jittered per frame at the wobble's rate
//   wobble             the marker on the wow -> flutter scale, 0..104 px
//   agef, ages         the low-pass glyph: the flat scaled about its left
//                      end, the slope moved with the corner; both dim with age
//   age0..age5         the hiss specks, a count set
//   gbar, gzero, gbox, gs/gd/gdot/gt   the gain gauge (the bar scaled -1..1
//                      about the centre), its bracket and readout on page 2
//   mbar               the mix gauge, scaled about its foot

#include "common/knl_fx.h"
#include "fx/op1/fx_screen.h"
#include "fx/op1/screen_anim.h"
#include "fx/src/tape/tape.h"

namespace {

struct Instance {
  op1fx::TapeSat tape;
};

knl::Fine fine;
const int32_t kTenthDb = 273;   // 32767 / 120: one tenth of a dB of word 5, the readout's unit

op1fx::TapeParams Params(const int16_t* p) {
  int32_t v[4];
  knl::Page1(p, v);
  op1fx::TapeParams q;
  q.saturate = v[0];
  q.wobble = v[1];
  q.age = 32767 - v[2];   // clockwise = a newer tape
  q.mix = v[3];
  q.gain_oct = (knl::Word(p, 5) - 16384) * 4;   // Q16 octaves, -1..1 (about -6..+6 dB)
  return q;
}

void Init(int pos) {
  fine.Reset(pos);
  Instance* in = knl::Instance<Instance>(pos);
  if (!in) return;
  knl::Zero(in, sizeof(*in));
  in->tape.Init();
}

void Render(int pos, int16_t* buf, const int16_t* p) {
  fine.Apply(pos, 1, kTenthDb);
  Instance* in = knl::Instance<Instance>(pos);
  if (!in) return;
  in->tape.Process(buf, nullptr, knl::kBlock, Params(p));
}

void RenderStereo(int pos, int16_t* l, int16_t* r, const int16_t* p) {
  fine.Apply(pos, 1, kTenthDb);
  Instance* in = knl::Instance<Instance>(pos);
  if (!in) return;
  in->tape.Process(l, r, knl::kBlock, Params(p));
}

// ---- screen (graphics thread) ----

using namespace op1fx::screen;

const int32_t kCY = 76, kBottom = 124, kScaleSpan = 104, kAgeX = 248, kAgeWalk = 22, kAgeFlat = 28;

const char* const kSatIds[8] = { "sat0", "sat1", "sat2", "sat3", "sat4", "sat5", "sat6", "sat7" };
const char* const kAgeIds[6] = { "age0", "age1", "age2", "age3", "age4", "age5" };
const char* const kSignIds[2] = { "gs0", "gs1" };
const char* const kDigitIds[7] = { "gd0", "gd1", "gd2", "gd3", "gd4", "gd5", "gd6" };
const char* const kTenthIds[10] = { "gt0", "gt1", "gt2", "gt3", "gt4", "gt5", "gt6", "gt7", "gt8", "gt9" };
const char* const kNodeIds[8] = { "wobble", "reelL", "reelR", "agef", "ages", "gbar", "gbox", "gdot" };
const char* const kPaletteIds[7] = { "palwhite", "paldim", "palblue", "palgreen", "palorange", "palblack", "palmid" };
enum { N_WOBBLE, N_REELL, N_REELR, N_AGEF, N_AGES, N_GBAR, N_GBOX, N_GDOT };
enum { C_WHITE, C_DIM, C_BLUE, C_GREEN, C_ORANGE, C_BLACK, C_MID };

bool ready;
void* sat_nodes[8];
void* age_nodes[6];
void* sign_nodes[2];
void* digit_nodes[7];
void* tenth_nodes[10];
void* nodes[8];
void* mix_node;
uint8_t* agef_line;
uint8_t* ages_line;
Colour pal[7];
op1fx::FrameClock clock;
uint32_t phase;

Colour Blend(const Colour& a, const Colour& b, int32_t v) {
  Colour c;
  for (int32_t i = 0; i < 4; ++i) c.rgba[i] = static_cast<uint8_t>(a.rgba[i] + (((b.rgba[i] - a.rgba[i]) * v) >> 15));
  return c;
}

void ScreenInit(void* scr) {
  ready = false;
  void* document = DocumentOf(scr);
  if (!document) return;
  void* tmp[7];
  if (!FindAll(document, kSatIds, sat_nodes, 8) || !FindAll(document, kAgeIds, age_nodes, 6) ||
      !FindAll(document, kSignIds, sign_nodes, 2) || !FindAll(document, kDigitIds, digit_nodes, 7) ||
      !FindAll(document, kTenthIds, tenth_nodes, 10) || !FindAll(document, kNodeIds, nodes, 8) ||
      !FindAll(document, kPaletteIds, tmp, 7) || !(mix_node = Find(document, "mbar"))) {
    return;
  }
  for (int32_t i = 0; i < 7; ++i) ReadColour(&pal[i], LineStyle(scr, kPaletteIds[i]));
  agef_line = LineStyle(scr, "agef");
  ages_line = LineStyle(scr, "ages");
  phase = 0;
  ready = true;
}

void ScreenUpdate(void* scr, int dt) {
  if (ready) {
    const int32_t pos = knl::ScreenPos();
    int32_t v[4];
    knl::ScreenPage1(v);   // 0..32767 as the stock screens see them (LFO included)
    const int32_t saturate = v[0], wobble = v[1], age = 32767 - v[2], mix = v[3];   // age mirrored, as the render
    const int32_t gain = knl::ScreenWord(5);
    const bool page2 = knl::Pressed(pos, 0);

    // Saturation: the wave on the tape.
    ShowOne(sat_nodes, 8, Steps(saturate, 7));
    // Wobble: the marker, and the hubs: 0.5 Hz (wow) .. 8 Hz (flutter) as
    // wobble rises, up to 2 x wobble px; the right hub a quarter turn behind.
    SetTranslate(nodes[N_WOBBLE], (wobble * kScaleSpan) >> 5, 0);
    const int32_t hz_q16 = 32768 + ((wobble * 491520) >> 15);
    phase += op1fx::PhaseStep(hz_q16, clock.Tick());
    const int32_t amp = (wobble * 2) >> 5;
    SetTranslate(nodes[N_REELL], (amp * op1fx::SinQ15(phase)) >> 15, (amp * op1fx::CosQ15(phase * 3)) >> 16);
    SetTranslate(nodes[N_REELR], (amp * op1fx::SinQ15(phase + 0x40000000u)) >> 15,
                 (amp * op1fx::CosQ15(phase * 3 + 0x40000000u)) >> 16);
    // Age: the glyph's corner walks left (the flat scaled about x 248, the
    // slope translated), both dimming toward the mid grey; the specks count up.
    const int32_t walk = (age * kAgeWalk) >> 5;                                    // Q10 px, 0..22
    ScaleX(nodes[N_AGEF], 0x400 - walk / kAgeFlat, kAgeX);
    SetTranslate(nodes[N_AGES], -walk, 0);
    const Colour worn = Blend(pal[C_WHITE], pal[C_MID], age >> 1);
    WriteColour(agef_line, worn);
    WriteColour(ages_line, worn);
    ShowFirst(age_nodes, 6, Steps(age, 6));
    // Gain: the bar scaled -1..1 about the centre; page 2 shows the bracket and the readout.
    const int32_t d = (gain - 16384) >> 4;                                       // Q10, -1024..1023
    SetMatrix(nodes[N_GBAR], 0x400, d, 0, kCY * (0x400 - d));
    SetVisible(nodes[N_GBOX], page2);
    SetVisible(nodes[N_GDOT], page2);
    const int32_t tenths = (gain * 120 + 16383) / 32767 - 60;
    const int32_t mag = tenths < 0 ? -tenths : tenths;
    ShowOne(sign_nodes, 2, page2 ? (tenths < 0 ? 1 : 0) : -1);
    ShowOne(digit_nodes, 7, page2 ? mag / 10 : -1);
    ShowOne(tenth_nodes, 10, page2 ? mag % 10 : -1);
    // Mix: the bar scaled about its foot.
    const int32_t s = mix >> 5;
    SetMatrix(mix_node, 0x400, s < 8 ? 8 : s, 0, kBottom * (0x400 - (s < 8 ? 8 : s)));
  }
  op1_screen_draw(scr, dt);
}

}  // namespace

extern "C" const op1_fx_stereo_t FX_tape = {
  { Init, Render, { OP1_PRESS_PAGE, OP1_PRESS_NONE, OP1_PRESS_NONE, OP1_PRESS_NONE } },
  RenderStereo,
};

extern "C" const op1_screen_t SCREEN_tape = { ScreenInit, ScreenUpdate };
