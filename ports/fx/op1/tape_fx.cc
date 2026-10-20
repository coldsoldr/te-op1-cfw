// TAPE on the OP-1 (op1/tape_fx.h): the DSP at the effect rate, one
// instance per position, restarted when the host stops it there.
//
// The screen (content/display/tapesat.svg: the stock tape.svg is the tape
// screen's; from docs/fx-briefs/designs/
// tape.md via op1/gen_fx_svg.py):
//   sat0..sat7     variant set, round(7 x saturate)
//   wobble         translate(76 x wobble, 0)
//   reelL, reelR   shaken per frame by up to 2 x wobble px, slow (wow) at
//                  the bottom of the range, fast (flutter) at the top
//   age            sx(1 - 0.6 age, 236)
//   age0..age5     count set, round(6 x age)
//   mix            the mix bar
//   eng0..eng4     the dirt engines' dots

#include "fx/op1/tape_fx.h"

#include "fx/op1/two_page.h"
#include "fx/src/tape/tape.h"
#include "op1/op1.h"

#ifndef OP1_SIM
#include "fx/op1/fx_screen.h"
#include "fx/op1/screen_anim.h"
#endif

namespace {

using op1fx::EncoderRange;
using op1fx::EncoderToValue;

// saturate .4, wobble .25, age .3, mix 1.
const int32_t kDefaults[4] = { 13107, 8192, 9830, 32767 };

struct Instance {
  op1fx::TapeSat tape;
  bool running;
};

Instance instances[op1fx::kNumPositions];

op1fx::TapeParams Params(const int16_t* encoders, const EncoderRange* ranges) {
  op1fx::TapeParams p;
  p.saturate = EncoderToValue(ranges[0], encoders[0]);
  p.wobble = EncoderToValue(ranges[1], encoders[1]);
  p.age = EncoderToValue(ranges[2], encoders[2]);
  p.mix = EncoderToValue(ranges[3], encoders[3]);
  return p;
}

void Process(int32_t position, int32_t, int16_t* l, int16_t* r, const int16_t* live,
             const int16_t*, const EncoderRange* ranges) {
  Instance& in = instances[position];
  const uint32_t start = op1::Cycles();
  if (!in.running) {
    in.tape.Init();
    in.running = true;
  }
  in.tape.Process(l, r, op1fx::kFxBlockSize, Params(live, ranges));
  op1fx::measured_cycles += op1::Cycles() - start;
}

void Stop(int32_t position) { instances[position].running = false; }

void Enter(int16_t* values, const EncoderRange* ranges) {
  op1fx::EnterOnePage(ranges, kDefaults, values);
}

#ifndef OP1_SIM
using namespace op1fx::screen;

const char* const kSatIds[8] = { "sat0", "sat1", "sat2", "sat3", "sat4", "sat5", "sat6", "sat7" };
const char* const kAgeIds[6] = { "age0", "age1", "age2", "age3", "age4", "age5" };
const char* const kNodeIds[] = { "wobble", "reelL", "reelR", "age", "mix" };
enum { N_WOBBLE, N_REELL, N_REELR, N_AGE, N_MIX, N_COUNT };

bool ready;
void* sat_nodes[8];
void* age_nodes[6];
void* nodes[N_COUNT];
EngineDots dots;
op1fx::FrameClock clock;
uint32_t phase;

void ScreenInit(void* self) {
  void* document = DocumentOf(self);
  ready = FindAll(document, kSatIds, sat_nodes, 8) && FindAll(document, kAgeIds, age_nodes, 6) &&
          FindAll(document, kNodeIds, nodes, N_COUNT) && dots.Init(self, 5, op1fx::kTapeEngineId);
}

void ScreenUpdate(void* self, int32_t, const int16_t* encoders, const int16_t*,
                  const EncoderRange* ranges) {
  if (!ready) return;
  const op1fx::TapeParams p = Params(encoders, ranges);
  dots.Show(op1fx::EngineDot(op1fx::kTapeEngineId));
  ShowOne(sat_nodes, 8, Steps(p.saturate, 7));
  SetTranslate(nodes[N_WOBBLE], (p.wobble * 76) >> 5, 0);
  // The reels: 0.5 Hz (wow) .. 8 Hz (flutter) as wobble rises, up to
  // 2 x wobble px; the right reel a quarter turn behind.
  const int32_t hz_q16 = 32768 + ((p.wobble * 491520) >> 15);   // 0.5 + 7.5 w Hz
  phase += op1fx::PhaseStep(hz_q16, clock.Tick());
  const int32_t amp = (p.wobble * 2) >> 5;   // Q10 px
  SetTranslate(nodes[N_REELL], (amp * op1fx::SinQ15(phase)) >> 15,
               (amp * op1fx::CosQ15(phase * 3)) >> 16);
  SetTranslate(nodes[N_REELR], (amp * op1fx::SinQ15(phase + 0x40000000u)) >> 15,
               (amp * op1fx::CosQ15(phase * 3 + 0x40000000u)) >> 16);
  ScaleX(nodes[N_AGE], 0x400 - ((p.age * 614) >> 15), 236);
  ShowFirst(age_nodes, 6, Steps(p.age, 6));
  SetMixBar(nodes[N_MIX], p.mix);
}
#endif

}  // namespace

namespace op1fx {

const Engine kTapeEngine = {
  kTapeEngineId, "tapesat.svg", Process, nullptr, Stop, Enter, nullptr,
#ifdef OP1_SIM
  nullptr, nullptr,
#else
  ScreenInit, ScreenUpdate,
#endif
};

}  // namespace op1fx
