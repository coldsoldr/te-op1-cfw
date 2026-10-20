// CRUSH on the OP-1 (op1/crush_fx.h): the DSP at the effect rate, one
// instance per position, restarted when the host stops it there.
//
// The screen (content/display/crush.svg, from the design in
// docs/fx-briefs/designs/crush.md via op1/gen_fx_svg.py):
//   rate0..rate7  variant set, round(7 x rate)
//   bits0..bits7  count set, 1 + round(7 x (bits - 2) / 14) (16 bits: 8)
//   jit0..jit5    the clock ticks, x offset 4 x jitter x (d_i + noise)
//   mix           the mix bar
//   eng0..eng4    the dirt engines' dots

#include "fx/op1/crush_fx.h"

#include "fx/src/crush/crush.h"
#include "op1/op1.h"

#ifndef OP1_SIM
#include "fx/op1/fx_screen.h"
#endif

namespace {

using op1fx::EncoderRange;
using op1fx::EncoderToValue;

struct Instance {
  op1fx::Crush crush;
  bool running;
};

Instance instances[op1fx::kNumPositions];

op1fx::CrushParams Params(const int16_t* encoders, const EncoderRange* ranges) {
  op1fx::CrushParams p;
  p.rate = EncoderToValue(ranges[0], encoders[0]);
  p.bits = EncoderToValue(ranges[1], encoders[1]);
  p.jitter = EncoderToValue(ranges[2], encoders[2]);
  p.mix = EncoderToValue(ranges[3], encoders[3]);
  return p;
}

void Process(int32_t position, int32_t, int16_t* l, int16_t* r, const int16_t* live,
             const int16_t*, const EncoderRange* ranges) {
  Instance& in = instances[position];
  if (!in.running) {
    in.crush.Init();
    in.running = true;
  }
  const uint32_t start = op1::Cycles();
  in.crush.Process(l, r, op1fx::kFxBlockSize, Params(live, ranges));
  op1fx::measured_cycles += op1::Cycles() - start;
}

void Stop(int32_t position) { instances[position].running = false; }

#ifndef OP1_SIM
using namespace op1fx::screen;

const char* const kRateIds[8] = { "rate0", "rate1", "rate2", "rate3", "rate4", "rate5", "rate6", "rate7" };
const char* const kBitsIds[8] = { "bits0", "bits1", "bits2", "bits3", "bits4", "bits5", "bits6", "bits7" };
const char* const kJitIds[6] = { "jit0", "jit1", "jit2", "jit3", "jit4", "jit5" };
// The ticks' fixed offsets (the design's d_i), x 8.
const int32_t kJitPattern8[6] = { 0, 3, -2, 4, -3, 2 };

bool ready;
void* rate_nodes[8];
void* bits_nodes[8];
void* jit_nodes[6];
void* mix_node;
EngineDots dots;
uint32_t frame_rng = 1;

void ScreenInit(void* self) {
  void* document = DocumentOf(self);
  ready = FindAll(document, kRateIds, rate_nodes, 8) && FindAll(document, kBitsIds, bits_nodes, 8) &&
          FindAll(document, kJitIds, jit_nodes, 6) && (mix_node = Find(document, "mix")) != nullptr &&
          dots.Init(self, 5, op1fx::kCrushEngineId);
}

void ScreenUpdate(void* self, int32_t, const int16_t* encoders, const int16_t*,
                  const EncoderRange* ranges) {
  if (!ready) return;
  const op1fx::CrushParams p = Params(encoders, ranges);
  dots.Show(op1fx::EngineDot(op1fx::kCrushEngineId));
  ShowOne(rate_nodes, 8, Steps(p.rate, 7));
  // bits = 16 - round(14 x v): 16 bits -> 8 ticks, 2 -> 1.
  ShowFirst(bits_nodes, 8, 8 - Steps(p.bits, 7));
  // x offset = 4 x jitter x (d + noise), noise in -0.5..0.5 per frame:
  // in Q10 pixels, 4 x j15 / 32768 x (d8 / 8 + n) x 1024 = j15 x (d8 + 8n) / 64.
  for (int32_t i = 0; i < 6; ++i) {
    frame_rng = frame_rng * 1664525u + 1013904223u;
    const int32_t noise8 = static_cast<int32_t>(frame_rng >> 29) - 4;   // -4..3: 8 x noise
    SetTranslate(jit_nodes[i], p.jitter * (kJitPattern8[i] + noise8) / 64, 0);
  }
  SetMixBar(mix_node, p.mix);
}
#endif

}  // namespace

namespace op1fx {

const Engine kCrushEngine = {
  kCrushEngineId, "crush.svg", Process, nullptr, Stop, nullptr, nullptr,
#ifdef OP1_SIM
  nullptr, nullptr,
#else
  ScreenInit, ScreenUpdate,
#endif
};

}  // namespace op1fx
