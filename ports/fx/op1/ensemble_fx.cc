// ENSEMBLE on the OP-1 (op1/ensemble_fx.h). The core runs at half the
// effect rate behind op1/half_rate.h (Clouds' halfband filters and an
// equal-power mix). One instance per position; a restart (the host's stop,
// or a pause over 5 ms) re-initialises it, passing the input through.
//
// The screen (content/display/ensemble.svg, from docs/fx-briefs/designs/
// ensemble.md via op1/gen_fx_svg.py):
//   sweep                 round the dial (30, 80, r 8) at the rate, per frame
//   rate                  sx(max(rate, 0.02), 22)
//   voiceL1..3, voiceR1..3  count set per lane, 1 + round(2 x voices), each
//                         moved by depth x 10 sin(phase + i 2 pi / 3) px; the
//                         R lane hidden on the mono positions
//   depth                 sx(0.1 + 0.9 depth, 160)
//   mix                   the mix bar
//   eng0..eng2            the modulation engines' dots

#include "fx/op1/ensemble_fx.h"

#include "fx/op1/half_rate.h"
#include "fx/op1/two_page.h"
#include "fx/src/ensemble/ensemble.h"
#include "op1/op1.h"

#ifndef OP1_SIM
#include "fx/op1/fx_screen.h"
#include "fx/op1/screen_anim.h"
#endif

namespace {

using op1fx::EncoderRange;
using op1fx::EncoderToValue;

// rate .3, depth .5, voices .5, mix .5.
const int32_t kDefaults[4] = { 9830, 16384, 16384, 16384 };

struct Instance {
  op1fx::Ensemble ensemble;
  op1fx::HalfRate half;
};

Instance instances[op1fx::kNumPositions];

void Values(const int16_t* encoders, const EncoderRange* ranges, int32_t v[4]) {
  for (int32_t k = 0; k < 4; ++k) v[k] = EncoderToValue(ranges[k], encoders[k]);
}

void Process(int32_t position, int32_t, int16_t* l, int16_t* r, const int16_t* live,
             const int16_t*, const EncoderRange* ranges) {
  Instance& in = instances[position];
  const uint32_t start = op1::Cycles();
  if (!in.half.Begin(start)) {
    in.ensemble.Init();
    op1fx::measured_cycles += op1::Cycles() - start;
    return;
  }
  int32_t v[4];
  Values(live, ranges, v);
  op1fx::EnsembleParams p;
  p.rate = v[0];
  p.depth = v[1];
  p.voices = v[2];
  in.half.Decimate(l, r);
  in.ensemble.Process(in.half.in(0), r ? in.half.in(1) : nullptr, in.half.wet(0),
                      r ? in.half.wet(1) : nullptr, op1fx::kHalfN, p);
  in.half.Mix(l, r, v[3]);
  op1fx::measured_cycles += op1::Cycles() - start;
}

void Stop(int32_t position) { instances[position].half.Stop(); }

void Enter(int16_t* values, const EncoderRange* ranges) {
  op1fx::EnterOnePage(ranges, kDefaults, values);
}

#ifndef OP1_SIM
using namespace op1fx::screen;

const char* const kVoiceIds[6] = { "voiceL1", "voiceL2", "voiceL3", "voiceR1", "voiceR2", "voiceR3" };
const char* const kNodeIds[] = { "sweep", "rate", "depth", "mix" };
enum { N_SWEEP, N_RATE, N_DEPTH, N_MIX, N_COUNT };

bool ready;
void* voice_nodes[6];
void* nodes[N_COUNT];
EngineDots dots;
op1fx::FrameClock clock;
uint32_t phase;

void ScreenInit(void* self) {
  void* document = DocumentOf(self);
  ready = FindAll(document, kVoiceIds, voice_nodes, 6) && FindAll(document, kNodeIds, nodes, N_COUNT) &&
          dots.Init(self, 3, op1fx::kEnsembleEngineId);
}

// The rate's speed on the screen, 0.05 .. 8 Hz (log), Q16: 0.05 x 160^u,
// by octaves (7.32 over the range).
int32_t RateHzQ16(int32_t u15) {
  const int32_t oct_q12 = (u15 * 29983) >> 15;   // 7.32 octaves, Q12
  const int32_t whole = oct_q12 >> 12;
  const int32_t frac = oct_q12 & 0xfff;
  // 0.05 Hz x 2^whole x (1 + frac): linear between octaves, within 6% of 2^frac.
  const int32_t base = 3277 << whole;   // 0.05 Hz, Q16
  return base + static_cast<int32_t>((static_cast<int64_t>(base) * frac) >> 12);
}

void ScreenUpdate(void* self, int32_t screen_position, const int16_t* encoders, const int16_t*,
                  const EncoderRange* ranges) {
  if (!ready) return;
  int32_t v[4];
  Values(encoders, ranges, v);
  dots.Show(op1fx::EngineDot(op1fx::kEnsembleEngineId));
  phase += op1fx::PhaseStep(RateHzQ16(v[0]), clock.Tick());
  // The dot starts at the top (30, 72): translate(8 sin p, 8 - 8 cos p).
  SetTranslate(nodes[N_SWEEP], (8 * op1fx::SinQ15(phase)) >> 5, (8 << 10) - ((8 * op1fx::CosQ15(phase)) >> 5));
  ScaleX(nodes[N_RATE], v[0] < 655 ? 20 : v[0] >> 5, 22);
  ScaleX(nodes[N_DEPTH], 102 + ((v[1] * 922) >> 15), 160);
  const int32_t count = 1 + Steps(v[2], 2);
  const bool stereo = screen_position == op1fx::kScreenMaster;
  for (int32_t lane = 0; lane < 2; ++lane) {
    for (int32_t i = 0; i < 3; ++i) {
      void* node = voice_nodes[lane * 3 + i];
      SetVisible(node, i < count && (lane == 0 || stereo));
      // depth x 10 px x sin(phase + i x 2 pi / 3), Q10.
      const uint32_t p = phase + static_cast<uint32_t>(i) * 0x55555555u;
      SetTranslate(node, (((v[1] * 10) >> 5) * op1fx::SinQ15(p)) >> 15, 0);
    }
  }
  SetMixBar(nodes[N_MIX], v[3]);
}
#endif

}  // namespace

namespace op1fx {

const Engine kEnsembleEngine = {
  kEnsembleEngineId, "ensemble.svg", Process, nullptr, Stop, Enter, nullptr,
#ifdef OP1_SIM
  nullptr, nullptr,
#else
  ScreenInit, ScreenUpdate,
#endif
};

}  // namespace op1fx
