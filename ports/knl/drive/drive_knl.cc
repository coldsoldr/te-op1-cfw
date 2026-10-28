// DRIVE (oversampled soft-clip overdrive) as a kennel effect plugin, id
// drive (was cs_drive; hundekraft has none, the user's choice 2026-10-27). The DSP of fx/src/drive/drive.h at the effect rate (it
// oversamples inside), one instance per position in its scratch buffer
// (common/knl_fx.h). Knobs: blue drive, green tone (centre flat), white
// bias, orange mix. Blue press: page 2, where blue sets the input gain
// (word 5: -6 dB at 0, 0 dB at the centre, +6 dB at the top, saved with the
// sound; shift + blue a tenth of a dB a click, knl::Fine). The gain scales
// the clipper's gain only (DriveParams::gain): the dry side of the mix and
// the auto-level are untouched, so at mix 0 the sound doesn't change.
//
// The screen (gen_drive_knl_svg.py, the canvas "Drive screen" design A,
// "Schematic", 2026-10-27): the circuit left to right.
//   wiper            the pot's blue arrow, translated -13..13 px by the gain
//   clip1..clip7     variant set by drive: the diodes' fill from the tip
//                    (none at 0), conduction rays from 5
//   dio / diodim     the diodes' outlines, blue, or dim while blue is on
//                    page 2
//   gs/gd/gdot/gt    the dB readout above the pot, with its rule `grule`,
//                    while blue is on page 2
//   fil, glow        the triode's filament (line) and the glow under its
//                    cathode (fill), warming with bias
//   tilt             the tilt EQ's S response, scaled about the pivot by
//                    2t - 1 (flat at the centre, flipped for dark)
//   mbar             the mix gauge, scaled about its foot

#include "common/knl_fx.h"
#include "fx/op1/fx_screen.h"
#include "fx/src/drive/drive.h"

namespace {

struct Instance {
  op1fx::Drive drive;
};

knl::Fine fine;
const int32_t kTenthDb = 273;   // 32767 / 120: one tenth of a dB of word 5, the readout's unit

op1fx::DriveParams Params(const int32_t v[4], const int16_t* p) {
  op1fx::DriveParams q;
  q.drive = v[0];
  q.tone = v[1];
  q.bias = v[2];
  q.mix = v[3];
  q.gain = knl::GainQ14(knl::Word(p, 5));
  return q;
}

void Init(int pos) {
  fine.Reset(pos);
  Instance* in = knl::Instance<Instance>(pos);
  if (!in) return;
  knl::Zero(in, sizeof(*in));
  in->drive.Init();
}

void Render(int pos, int16_t* buf, const int16_t* p) {
  fine.Apply(pos, 1, kTenthDb);
  Instance* in = knl::Instance<Instance>(pos);
  if (!in) return;
  int32_t v[4];
  knl::Page1(p, v);
  in->drive.Process(buf, nullptr, knl::kBlock, Params(v, p));
}

void RenderStereo(int pos, int16_t* l, int16_t* r, const int16_t* p) {
  fine.Apply(pos, 1, kTenthDb);
  Instance* in = knl::Instance<Instance>(pos);
  if (!in) return;
  int32_t v[4];
  knl::Page1(p, v);
  in->drive.Process(l, r, knl::kBlock, Params(v, p));
}

// ---- screen (graphics thread) ----

using namespace op1fx::screen;

const int32_t kY = 78, kWiperSwing = 13;

const char* const kClipIds[7] = { "clip1", "clip2", "clip3", "clip4", "clip5", "clip6", "clip7" };
const char* const kSignIds[2] = { "gs0", "gs1" };
const char* const kDigitIds[7] = { "gd0", "gd1", "gd2", "gd3", "gd4", "gd5", "gd6" };
const char* const kTenthIds[10] = { "gt0", "gt1", "gt2", "gt3", "gt4", "gt5", "gt6", "gt7", "gt8", "gt9" };
const char* const kNodeIds[8] = { "wiper", "dio", "diodim", "gdot", "grule", "tilt", "mbar", "static" };
const char* const kPaletteIds[8] = { "palwhite", "paldim", "palblue", "palgreen", "palorange", "palblack", "palmid", "palglow" };
enum { N_WIPER, N_DIO, N_DIODIM, N_GDOT, N_GRULE, N_TILT, N_MBAR, N_STATIC };
enum { C_WHITE, C_DIM, C_BLUE, C_GREEN, C_ORANGE, C_BLACK, C_MID, C_GLOW };

bool ready;
void* clip_nodes[7];
void* sign_nodes[2];
void* digit_nodes[7];
void* tenth_nodes[10];
void* nodes[8];
uint8_t* fil_line;
uint8_t* glow_fill;
Colour pal[8];

Colour Blend(const Colour& a, const Colour& b, int32_t v) {
  Colour c;
  for (int32_t i = 0; i < 4; ++i) c.rgba[i] = static_cast<uint8_t>(a.rgba[i] + (((b.rgba[i] - a.rgba[i]) * v) >> 15));
  return c;
}

void ScreenInit(void* scr) {
  ready = false;
  void* document = DocumentOf(scr);
  if (!document) return;
  void* tmp[8];
  if (!FindAll(document, kClipIds, clip_nodes, 7) || !FindAll(document, kSignIds, sign_nodes, 2) ||
      !FindAll(document, kDigitIds, digit_nodes, 7) || !FindAll(document, kTenthIds, tenth_nodes, 10) ||
      !FindAll(document, kNodeIds, nodes, 8) || !FindAll(document, kPaletteIds, tmp, 8) ||
      !Find(document, "fil") || !Find(document, "glow")) {
    return;
  }
  for (int32_t i = 0; i < 8; ++i) ReadColour(&pal[i], LineStyle(scr, kPaletteIds[i]));
  fil_line = LineStyle(scr, "fil");
  glow_fill = FillStyle(scr, "glow");
  ready = true;
}

void ScreenUpdate(void* scr, int dt) {
  if (ready) {
    const int32_t pos = knl::ScreenPos();
    int32_t v[4];
    knl::ScreenPage1(v);   // 0..32767 as the stock screens see them (LFO included)
    const int32_t drive = v[0], tone = v[1], bias = v[2], mix = v[3];
    const int32_t gain = knl::ScreenWord(5);
    const bool page2 = knl::Pressed(pos, 0);

    // Drive: the diodes fill from the tip; dim outlines while blue is on page 2.
    ShowOne(clip_nodes, 7, page2 ? -1 : Steps(drive, 7) - 1);
    SetVisible(nodes[N_DIO], !page2);
    SetVisible(nodes[N_DIODIM], page2);
    // Gain: the wiper along the pot, -13..13 px.
    SetTranslate(nodes[N_WIPER], ((gain - 16384) * kWiperSwing) >> 4, 0);   // Q10 px
    // Page 2: the readout (tenths of a dB, -60..60).
    const int32_t tenths = (gain * 120 + 16383) / 32767 - 60;
    const int32_t mag = tenths < 0 ? -tenths : tenths;
    ShowOne(sign_nodes, 2, page2 ? (tenths < 0 ? 1 : 0) : -1);
    ShowOne(digit_nodes, 7, page2 ? mag / 10 : -1);
    ShowOne(tenth_nodes, 10, page2 ? mag % 10 : -1);
    SetVisible(nodes[N_GDOT], page2);
    SetVisible(nodes[N_GRULE], page2);
    // Bias: the filament and the glow warm.
    WriteColour(fil_line, Blend(pal[C_DIM], pal[C_WHITE], bias));
    WriteColour(glow_fill, Blend(pal[C_BLACK], pal[C_GLOW], bias));
    // Tone: the S response scaled about the pivot, 2t - 1 (never quite zero: a flat line).
    int32_t d = (tone >> 4) - 0x400;   // Q10
    if (d > -4 && d < 4) d = 4;
    SetMatrix(nodes[N_TILT], 0x400, d, 0, kY * (0x400 - d));
    // Mix: the bar scaled about its foot.
    const int32_t s = mix >> 5;
    SetMatrix(nodes[N_MBAR], 0x400, s < 8 ? 8 : s, 0, 124 * (0x400 - (s < 8 ? 8 : s)));
  }
  op1_screen_draw(scr, dt);
}

}  // namespace

extern "C" const op1_fx_stereo_t FX_drive = {
  { Init, Render, { OP1_PRESS_PAGE, OP1_PRESS_NONE, OP1_PRESS_NONE, OP1_PRESS_NONE } },
  RenderStereo,
};

extern "C" const op1_screen_t SCREEN_drive = { ScreenInit, ScreenUpdate };
