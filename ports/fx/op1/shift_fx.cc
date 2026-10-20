// SHIFT on the OP-1 (op1/shift_fx.h): the DSP at the effect rate, one
// instance per position, restarted when the host stops it there.
//
// The screen (content/display/shift.svg, from docs/fx-briefs/designs/
// shift.md via op1/gen_fx_svg.py), f = shift bipolar -1..1, sp = spread
// (0 on the mono positions):
//   shiftL, shiftR      translate(60 f + 20 sp, 0), translate(60 f - 20 sp, 0)
//   spread              sx(max(sp, 0.05), 160)
//   sweep               round the dial (30, 80, r 8) per frame, at a rate
//                       with |f|, the direction f's sign
//   ringupL/R, ringdnL/R  translate(+-60 |f|, 0) (ring mode)
//   fb1..fb4            count set, round(4 x feedback), mirrored about x 233
//                       when f < 0
//   mode0, mode1        the mode, lit blue; modeshift / modering shown
//   mix                 the mix bar
//   eng0..eng2          the modulation engines' dots

#include "fx/op1/shift_fx.h"

#include "fx/op1/two_page.h"
#include "fx/src/shift/shift.h"
#include "op1/op1.h"

#ifndef OP1_SIM
#include "fx/op1/fx_screen.h"
#include "fx/op1/screen_anim.h"
#endif

namespace {

using op1fx::EncoderRange;
using op1fx::EncoderToValue;

const int32_t kRingBit = 1;   // values[4] bit 0

// shift a little up, feedback .25, no spread, mix .5.
const int32_t kDefaults[4] = { 19000, 8192, 0, 16384 };

struct Instance {
  op1fx::FreqShift shift;
  bool running;
};

Instance instances[op1fx::kNumPositions];

bool Ring(const int16_t* values) { return (values[4] & kRingBit) != 0; }

op1fx::ShiftParams Params(const int16_t* encoders, const int16_t* values, const EncoderRange* ranges) {
  op1fx::ShiftParams p;
  p.shift = EncoderToValue(ranges[0], encoders[0]);
  p.feedback = EncoderToValue(ranges[1], encoders[1]);
  p.spread = EncoderToValue(ranges[2], encoders[2]);
  p.mix = EncoderToValue(ranges[3], encoders[3]);
  p.ring = Ring(values);
  return p;
}

void Process(int32_t position, int32_t, int16_t* l, int16_t* r, const int16_t* live,
             const int16_t* values, const EncoderRange* ranges) {
  Instance& in = instances[position];
  const uint32_t start = op1::Cycles();
  if (!in.running) {
    in.shift.Init();
    in.running = true;
  }
  in.shift.Process(l, r, op1fx::kFxBlockSize, Params(live, values, ranges));
  op1fx::measured_cycles += op1::Cycles() - start;
}

void Stop(int32_t position) { instances[position].running = false; }

void Enter(int16_t* values, const EncoderRange* ranges) {
  op1fx::EnterOnePage(ranges, kDefaults, values);
}

bool EngineClick(int32_t, int16_t* values, const EncoderRange*, op1fx::Click click, int32_t,
                 bool* changed) {
  *changed = false;
  if (click != op1fx::CLICK_BLUE) return false;
  values[4] = static_cast<int16_t>(values[4] ^ kRingBit);
  *changed = true;
  return true;
}

#ifndef OP1_SIM
using namespace op1fx::screen;

const char* const kFbIds[4] = { "fb1", "fb2", "fb3", "fb4" };
const char* const kNodeIds[] = { "shiftL", "shiftR", "spread", "sweep", "ringupL", "ringdnL",
                                 "ringupR", "ringdnR", "modeshift", "modering", "mix" };
enum { N_SHIFTL, N_SHIFTR, N_SPREAD, N_SWEEP, N_RINGUPL, N_RINGDNL, N_RINGUPR, N_RINGDNR,
       N_MODESHIFT, N_MODERING, N_MIX, N_COUNT };
static_assert(sizeof(kNodeIds) / sizeof(kNodeIds[0]) == N_COUNT, "node ids");

bool ready;
void* fb_nodes[4];
void* nodes[N_COUNT];
EngineDots dots;
uint8_t* mode_line[2];
Colour mode_on, mode_off;
op1fx::FrameClock clock;
uint32_t angle;

void ScreenInit(void* self) {
  void* document = DocumentOf(self);
  ready = false;
  if (!FindAll(document, kFbIds, fb_nodes, 4) || !FindAll(document, kNodeIds, nodes, N_COUNT) ||
      !Find(document, "mode0") || !Find(document, "mode1") ||
      !dots.Init(self, 3, op1fx::kShiftEngineId)) {
    return;
  }
  // The file ships in shift mode: mode0 lit, mode1 not.
  mode_line[0] = LineStyle(self, "mode0");
  mode_line[1] = LineStyle(self, "mode1");
  ReadColour(&mode_on, mode_line[0]);
  ReadColour(&mode_off, mode_line[1]);
  ready = true;
}

void ScreenUpdate(void* self, int32_t screen_position, const int16_t* encoders, const int16_t* values,
                  const EncoderRange* ranges) {
  if (!ready) return;
  const op1fx::ShiftParams p = Params(encoders, values, ranges);
  dots.Show(op1fx::EngineDot(op1fx::kShiftEngineId));
  WriteColour(mode_line[0], p.ring ? mode_off : mode_on);
  WriteColour(mode_line[1], p.ring ? mode_on : mode_off);
  SetVisible(nodes[N_MODESHIFT], !p.ring);
  SetVisible(nodes[N_MODERING], p.ring);

  int32_t f = (p.shift - 16384) * 2;   // Q15, -1..1
  if (f > 32767) f = 32767;
  const int32_t af = f < 0 ? -f : f;
  const int32_t sp = screen_position == op1fx::kScreenMaster ? p.spread : 0;
  // Q10 px: v15 x k >> 5.
  SetTranslate(nodes[N_SHIFTL], ((f * 60) >> 5) + ((sp * 20) >> 5), 0);
  SetTranslate(nodes[N_SHIFTR], ((f * 60) >> 5) - ((sp * 20) >> 5), 0);
  ScaleX(nodes[N_SPREAD], sp < 1638 ? 51 : sp >> 5, 160);
  SetTranslate(nodes[N_RINGUPL], (af * 60) >> 5, 0);
  SetTranslate(nodes[N_RINGUPR], (af * 60) >> 5, 0);
  SetTranslate(nodes[N_RINGDNL], -((af * 60) >> 5), 0);
  SetTranslate(nodes[N_RINGDNR], -((af * 60) >> 5), 0);

  // The sweep dot: up to 1.5 turns a second at |f| = 1, drawn at 40
  // degrees: (30 + 8 cos a, 80 - 8 sin a) - (36.13, 74.86).
  const uint32_t step = op1fx::PhaseStep((af * 98304) >> 15, clock.Tick());
  angle += f < 0 ? -step : step;
  const uint32_t a = angle + 0x1c71c71cu;   // + 40 degrees
  SetTranslate(nodes[N_SWEEP], ((8 * op1fx::CosQ15(a)) >> 5) - 6277,
               -((8 * op1fx::SinQ15(a)) >> 5) + 5263);

  ShowFirst(fb_nodes, 4, Steps(p.feedback, 4));
  for (int32_t i = 0; i < 4; ++i) {
    if (f < 0) {
      SetMatrix(fb_nodes[i], -0x400, 0x400, 466 << 10, 0);
    } else {
      SetTranslate(fb_nodes[i], 0, 0);
    }
  }
  SetMixBar(nodes[N_MIX], p.mix);
}
#endif

}  // namespace

namespace op1fx {

const Engine kShiftEngine = {
  kShiftEngineId, "shift.svg", Process, nullptr, Stop, Enter, EngineClick,
#ifdef OP1_SIM
  nullptr, nullptr,
#else
  ScreenInit, ScreenUpdate,
#endif
};

}  // namespace op1fx
