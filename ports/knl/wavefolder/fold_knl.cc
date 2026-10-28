// FOLD (wavefolder) as a kennel effect plugin, id
// wavefolder (was cs_fold; the user's name, 2026-10-27). The DSP of
// fx/src/fold/fold.h at the effect rate, one instance per position in its
// scratch buffer (common/knl_fx.h). Knobs: blue fold, green symmetry,
// white tone, orange mix. Blue press: page 2, where blue sets the input
// gain (word 5: -6 dB at 0, 0 dB at the centre, +6 dB at the top, saved
// with the sound; shift + blue a tenth of a dB a click, knl::Fine). The
// gain scales the folder's gain only (FoldParams::gain): the level make-up
// and the dry side of the mix don't see it, so at mix 0 the sound doesn't
// change.
//
// The screen (gen_fold_knl_svg.py, the canvas "Fold screen" design A,
// "Wave, aligned", 2026-10-27):
//   wave0..wave7       variant set by fold: one output cycle
//   symmetry           the offset line, translated -24 (2v - 1) px
//   tonef, tones       the low-pass glyph: the flat scaled about its left
//                      end, the slope moved with the corner (dark = left)
//   gbar, gzero, gbox, gs/gd/gdot/gt   the gain gauge (the bar scaled -1..1
//                      about the centre), its bracket and readout on page 2
//   mbar               the mix gauge, scaled about its foot

#include "common/knl_fx.h"
#include "fx/op1/fx_screen.h"
#include "fx/src/fold/fold.h"

namespace {

struct Instance {
  op1fx::Fold fold;
};

knl::Fine fine;
const int32_t kTenthDb = 273;   // 32767 / 120: one tenth of a dB of word 5, the readout's unit

op1fx::FoldParams Params(const int16_t* p) {
  int32_t v[4];
  knl::Page1(p, v);
  op1fx::FoldParams q;
  q.fold = v[0];
  q.symmetry = v[1];
  q.tone = v[2];
  q.mix = v[3];
  q.gain = knl::GainQ14(knl::Word(p, 5));
  return q;
}

void Init(int pos) {
  fine.Reset(pos);
  Instance* in = knl::Instance<Instance>(pos);
  if (!in) return;
  knl::Zero(in, sizeof(*in));
  in->fold.Init();
}

void Render(int pos, int16_t* buf, const int16_t* p) {
  fine.Apply(pos, 1, kTenthDb);
  Instance* in = knl::Instance<Instance>(pos);
  if (!in) return;
  in->fold.Process(buf, nullptr, knl::kBlock, Params(p));
}

void RenderStereo(int pos, int16_t* l, int16_t* r, const int16_t* p) {
  fine.Apply(pos, 1, kTenthDb);
  Instance* in = knl::Instance<Instance>(pos);
  if (!in) return;
  in->fold.Process(l, r, knl::kBlock, Params(p));
}

// ---- screen (graphics thread) ----

using namespace op1fx::screen;

const int32_t kCY = 76, kBottom = 124, kToneX = 248, kToneWalk = 20, kToneFlat = 28;

const char* const kWaveIds[8] = { "wave0", "wave1", "wave2", "wave3", "wave4", "wave5", "wave6", "wave7" };
const char* const kSignIds[2] = { "gs0", "gs1" };
const char* const kDigitIds[7] = { "gd0", "gd1", "gd2", "gd3", "gd4", "gd5", "gd6" };
const char* const kTenthIds[10] = { "gt0", "gt1", "gt2", "gt3", "gt4", "gt5", "gt6", "gt7", "gt8", "gt9" };
const char* const kNodeIds[7] = { "symmetry", "tonef", "tones", "gbar", "gbox", "gdot", "mbar" };
enum { N_SYM, N_TONEF, N_TONES, N_GBAR, N_GBOX, N_GDOT, N_MBAR };

bool ready;
void* wave_nodes[8];
void* sign_nodes[2];
void* digit_nodes[7];
void* tenth_nodes[10];
void* nodes[7];

void ScreenInit(void* scr) {
  ready = false;
  void* document = DocumentOf(scr);
  if (!document) return;
  ready = FindAll(document, kWaveIds, wave_nodes, 8) && FindAll(document, kSignIds, sign_nodes, 2) &&
          FindAll(document, kDigitIds, digit_nodes, 7) && FindAll(document, kTenthIds, tenth_nodes, 10) &&
          FindAll(document, kNodeIds, nodes, 7);
}

void ScreenUpdate(void* scr, int dt) {
  if (ready) {
    const int32_t pos = knl::ScreenPos();
    int32_t v[4];
    knl::ScreenPage1(v);   // 0..32767 as the stock screens see them (LFO included)
    const int32_t fold = v[0], symmetry = v[1], tone = v[2], mix = v[3];
    const int32_t gain = knl::ScreenWord(5);
    const bool page2 = knl::Pressed(pos, 0);

    ShowOne(wave_nodes, 8, Steps(fold, 7));
    SetTranslate(nodes[N_SYM], 0, (24 << 10) - ((symmetry * 48) >> 5));   // -24 (2v - 1) px
    // Tone: the glyph's corner walks left as it darkens (the flat scaled
    // about x 248, the slope translated), 20 px at full dark.
    const int32_t walk = ((32767 - tone) * kToneWalk) >> 5;                 // Q10 px, 0..20
    ScaleX(nodes[N_TONEF], 0x400 - walk / kToneFlat, kToneX);
    SetTranslate(nodes[N_TONES], -walk, 0);
    // Gain: the bar scaled -1..1 about the centre; page 2 shows the bracket and the readout.
    const int32_t d = (gain - 16384) >> 4;                                   // Q10, -1024..1023
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
    SetMatrix(nodes[N_MBAR], 0x400, s < 8 ? 8 : s, 0, kBottom * (0x400 - (s < 8 ? 8 : s)));
  }
  op1_screen_draw(scr, dt);
}

}  // namespace

extern "C" const op1_fx_stereo_t FX_fold = {
  { Init, Render, { OP1_PRESS_PAGE, OP1_PRESS_NONE, OP1_PRESS_NONE, OP1_PRESS_NONE } },
  RenderStereo,
};

extern "C" const op1_screen_t SCREEN_fold = { ScreenInit, ScreenUpdate };
