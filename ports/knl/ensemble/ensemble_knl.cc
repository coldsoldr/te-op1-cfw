// ENSEMBLE (chorus / ensemble) as a kennel effect plugin, id
// ensemble (was cs_ensemble; hundekraft has none, the user's name 2026-10-27).
// The core of fx/src/ensemble/ensemble.h at half the effect rate behind
// fx/op1/half_rate.h, one instance per position in its scratch buffer
// (common/knl_fx.h). Knobs: blue rate, green depth, white voices, orange
// mix. Blue press: page 2, where blue sets the input gain (word 5: -6 dB at
// 0, 0 dB at the centre, +6 dB at the top, saved with the sound; shift +
// blue a tenth of a dB a click, knl::Fine). The gain scales the wet path's
// input only (the decimated signal before the core): the dry side of the
// mix doesn't see it, so at mix 0 the sound doesn't change.
//
// The screen (gen_ensemble_knl_svg.py, the canvas "Ensemble screen" design
// C, "Drift", 2026-10-27):
//   vL1..vL3, vR1..vR3   the voices' strings, a count set per lane (R hidden
//                        on mono positions), each translated by
//                        depth x 28 px x sin(LFO phase + i x 120 deg (+180 R))
//   hL1..hL3, hR1..hR3   the home marks with their depth ticks, the same
//                        count set, each scaled about its mark by depth
//   laneR                the R lane's guide, hidden on mono positions
//   sweep                the dial's dot, circling at the rate
//   rbar                 the rate bar, scaled about its left end
//   gbar, gzero, gbox, gs/gd/gdot/gt   the gain gauge, bracket and readout
//   mbar                 the mix gauge, scaled about its foot

#include "common/knl_fx.h"
#include "fx/op1/fx_screen.h"
#include "fx/op1/half_rate.h"
#include "fx/op1/screen_anim.h"
#include "fx/src/ensemble/ensemble.h"
#include "op1/op1.h"

namespace {

struct Instance {
  op1fx::Ensemble ensemble;
  op1fx::HalfRate half;
};

knl::Fine fine;
const int32_t kTenthDb = 273;   // 32767 / 120: one tenth of a dB of word 5, the readout's unit

void Init(int pos) {
  fine.Reset(pos);
  Instance* in = knl::Instance<Instance>(pos);
  if (!in) return;
  knl::Zero(in, sizeof(*in));
  in->ensemble.Init();
  in->half.Stop();
}

// The input gain on the wet path: the decimated input, in place.
void Gain(const int16_t* buf, int32_t gain_q14) {
  if (gain_q14 == 16384) return;
  int16_t* x = const_cast<int16_t*>(buf);
  for (int32_t i = 0; i < op1fx::kHalfN; ++i) x[i] = op1_sat16((x[i] * gain_q14) >> 14);
}

void Process(Instance* in, int16_t* l, int16_t* r, const int16_t* p) {
  if (!in->half.Begin(op1::Cycles())) {   // a restart: the input passes through
    in->ensemble.Init();
    in->half.End();
    return;
  }
  int32_t v[4];
  knl::Page1(p, v);
  op1fx::EnsembleParams q;
  q.rate = v[0];
  q.depth = v[1];
  q.voices = v[2];
  const int32_t gain = knl::GainQ14(knl::Word(p, 5));
  in->half.Decimate(l, r);
  Gain(in->half.in(0), gain);
  if (r) Gain(in->half.in(1), gain);
  in->ensemble.Process(in->half.in(0), r ? in->half.in(1) : nullptr, in->half.wet(0),
                       r ? in->half.wet(1) : nullptr, op1fx::kHalfN, q);
  in->half.Mix(l, r, v[3]);
  in->half.End();
}

void Render(int pos, int16_t* buf, const int16_t* p) {
  fine.Apply(pos, 1, kTenthDb);
  Instance* in = knl::Instance<Instance>(pos);
  if (in) Process(in, buf, nullptr, p);
}

void RenderStereo(int pos, int16_t* l, int16_t* r, const int16_t* p) {
  fine.Apply(pos, 1, kTenthDb);
  Instance* in = knl::Instance<Instance>(pos);
  if (in) Process(in, l, r, p);
}

// ---- screen (graphics thread) ----

using namespace op1fx::screen;

const int32_t kCY = 76, kBottom = 124, kSpan = 28, kDialR = 10, kRateX0 = 252;
const int32_t kHomes[3] = { 80, 140, 200 };

const char* const kVoiceIds[6] = { "vL1", "vL2", "vL3", "vR1", "vR2", "vR3" };
const char* const kHomeIds[6] = { "hL1", "hL2", "hL3", "hR1", "hR2", "hR3" };
const char* const kSignIds[2] = { "gs0", "gs1" };
const char* const kDigitIds[7] = { "gd0", "gd1", "gd2", "gd3", "gd4", "gd5", "gd6" };
const char* const kTenthIds[10] = { "gt0", "gt1", "gt2", "gt3", "gt4", "gt5", "gt6", "gt7", "gt8", "gt9" };
const char* const kNodeIds[7] = { "laneR", "sweep", "rbar", "gbar", "gbox", "gdot", "mbar" };
enum { N_LANER, N_SWEEP, N_RBAR, N_GBAR, N_GBOX, N_GDOT, N_MBAR };

bool ready;
void* voice_nodes[6];
void* home_nodes[6];
void* sign_nodes[2];
void* digit_nodes[7];
void* tenth_nodes[10];
void* nodes[7];
op1fx::FrameClock clock;
uint32_t phase;

// The rate's speed on the screen, 0.05 .. 8 Hz (log), Q16.
int32_t RateHzQ16(int32_t u15) {
  const int32_t oct_q12 = (u15 * 29983) >> 15;   // 7.32 octaves, Q12
  const int32_t whole = oct_q12 >> 12;
  const int32_t frac = oct_q12 & 0xfff;
  const int32_t base = 3277 << whole;   // 0.05 Hz, Q16
  return base + static_cast<int32_t>((static_cast<int64_t>(base) * frac) >> 12);
}

void ScreenInit(void* scr) {
  ready = false;
  void* document = DocumentOf(scr);
  if (!document) return;
  ready = FindAll(document, kVoiceIds, voice_nodes, 6) && FindAll(document, kHomeIds, home_nodes, 6) &&
          FindAll(document, kSignIds, sign_nodes, 2) && FindAll(document, kDigitIds, digit_nodes, 7) &&
          FindAll(document, kTenthIds, tenth_nodes, 10) && FindAll(document, kNodeIds, nodes, 7);
  phase = 0;
}

void ScreenUpdate(void* scr, int dt) {
  if (ready) {
    const int32_t pos = knl::ScreenPos();
    int32_t v[4];
    knl::ScreenPage1(v);   // 0..32767 as the stock screens see them (LFO included)
    const int32_t rate = v[0], depth = v[1], voices = v[2], mix = v[3];
    const int32_t gain = knl::ScreenWord(5);
    const bool page2 = knl::Pressed(pos, 0);
    const bool stereo = pos == OP1_FX_MASTER;

    // Rate: the dot round the dial, and the bar.
    phase += op1fx::PhaseStep(RateHzQ16(rate), clock.Tick());
    SetTranslate(nodes[N_SWEEP], (kDialR * op1fx::SinQ15(phase)) >> 5,
                 (kDialR << 10) - ((kDialR * op1fx::CosQ15(phase)) >> 5));
    ScaleX(nodes[N_RBAR], rate < 655 ? 20 : rate >> 5, kRateX0);
    // The voices: a count set per lane; each string sways about its home by
    // depth x 28 px, the three a third of a turn apart, the R lane opposite;
    // its home mark's ticks spread to the same reach.
    const int32_t count = 1 + Steps(voices, 2);
    const int32_t reach = (depth * kSpan) >> 5;                 // Q10 px
    int32_t scale = depth >> 5;                                   // Q10
    if (scale < 20) scale = 20;
    for (int32_t lane = 0; lane < 2; ++lane) {
      for (int32_t i = 0; i < 3; ++i) {
        const int32_t k = lane * 3 + i;
        const bool on = i < count && (lane == 0 || stereo);
        SetVisible(voice_nodes[k], on);
        SetVisible(home_nodes[k], on);
        const uint32_t ph = phase + static_cast<uint32_t>(i) * 0x55555555u + (lane ? 0x80000000u : 0u);
        SetTranslate(voice_nodes[k], (reach * op1fx::SinQ15(ph)) >> 15, 0);
        ScaleX(home_nodes[k], scale, kHomes[i]);
      }
    }
    SetVisible(nodes[N_LANER], stereo);
    // Gain: the bar scaled -1..1 about the centre; page 2 shows the bracket and the readout.
    const int32_t d = (gain - 16384) >> 4;                        // Q10, -1024..1023
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

extern "C" const op1_fx_stereo_t FX_ensemble = {
  { Init, Render, { OP1_PRESS_PAGE, OP1_PRESS_NONE, OP1_PRESS_NONE, OP1_PRESS_NONE } },
  RenderStereo,
};

extern "C" const op1_screen_t SCREEN_ensemble = { ScreenInit, ScreenUpdate };
