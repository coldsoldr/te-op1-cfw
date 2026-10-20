// FOLD on the OP-1 (op1/fold_fx.h): the DSP at the effect rate, one
// instance per position, restarted when the host stops it there.
//
// The screen (content/display/fold.svg, from docs/fx-briefs/designs/
// fold.md via op1/gen_fx_svg.py):
//   wave0..wave7  variant set, round(7 x fold)
//   symmetry      bipolar: translate(0, -24 x (2v - 1))
//   tone          sx(0.3 + 0.7 tone, 236)
//   mix           the mix bar
//   eng0..eng4    the dirt engines' dots

#include "fx/op1/fold_fx.h"

#include "fx/op1/two_page.h"
#include "fx/src/fold/fold.h"
#include "op1/op1.h"

#ifndef OP1_SIM
#include "fx/op1/fx_screen.h"
#endif

namespace {

using op1fx::EncoderRange;
using op1fx::EncoderToValue;

// fold .3, symmetric, tone .7, mix 1.
const int32_t kDefaults[4] = { 9830, 16384, 22938, 32767 };

struct Instance {
  op1fx::Fold fold;
  bool running;
};

Instance instances[op1fx::kNumPositions];

op1fx::FoldParams Params(const int16_t* encoders, const EncoderRange* ranges) {
  op1fx::FoldParams p;
  p.fold = EncoderToValue(ranges[0], encoders[0]);
  p.symmetry = EncoderToValue(ranges[1], encoders[1]);
  p.tone = EncoderToValue(ranges[2], encoders[2]);
  p.mix = EncoderToValue(ranges[3], encoders[3]);
  return p;
}

void Process(int32_t position, int32_t, int16_t* l, int16_t* r, const int16_t* live,
             const int16_t*, const EncoderRange* ranges) {
  Instance& in = instances[position];
  const uint32_t start = op1::Cycles();
  if (!in.running) {
    in.fold.Init();
    in.running = true;
  }
  in.fold.Process(l, r, op1fx::kFxBlockSize, Params(live, ranges));
  op1fx::measured_cycles += op1::Cycles() - start;
}

void Stop(int32_t position) { instances[position].running = false; }

void Enter(int16_t* values, const EncoderRange* ranges) {
  op1fx::EnterOnePage(ranges, kDefaults, values);
}

#ifndef OP1_SIM
using namespace op1fx::screen;

const char* const kWaveIds[8] = { "wave0", "wave1", "wave2", "wave3", "wave4", "wave5", "wave6", "wave7" };

bool ready;
void* wave_nodes[8];
void* symmetry_node;
void* tone_node;
void* mix_node;
EngineDots dots;

void ScreenInit(void* self) {
  void* document = DocumentOf(self);
  ready = FindAll(document, kWaveIds, wave_nodes, 8) && (symmetry_node = Find(document, "symmetry")) &&
          (tone_node = Find(document, "tone")) && (mix_node = Find(document, "mix")) &&
          dots.Init(self, 5, op1fx::kFoldEngineId);
}

void ScreenUpdate(void* self, int32_t, const int16_t* encoders, const int16_t*,
                  const EncoderRange* ranges) {
  if (!ready) return;
  const op1fx::FoldParams p = Params(encoders, ranges);
  dots.Show(op1fx::EngineDot(op1fx::kFoldEngineId));
  ShowOne(wave_nodes, 8, Steps(p.fold, 7));
  // -24 (2v - 1) px = 24 - 48 v, Q10.
  SetTranslate(symmetry_node, 0, (24 << 10) - ((p.symmetry * 48) >> 5));
  ScaleX(tone_node, 307 + ((p.tone * 717) >> 15), 236);
  SetMixBar(mix_node, p.mix);
}
#endif

}  // namespace

namespace op1fx {

const Engine kFoldEngine = {
  kFoldEngineId, "fold.svg", Process, nullptr, Stop, Enter, nullptr,
#ifdef OP1_SIM
  nullptr, nullptr,
#else
  ScreenInit, ScreenUpdate,
#endif
};

}  // namespace op1fx
