// TAPE ECHO on the OP-1 (op1/tape_echo_fx.h). The core runs at half the
// effect rate behind op1/half_rate.h (Clouds' halfband filters and an
// equal-power mix). One instance per position; a restart (the host's stop,
// or a pause over 5 ms) re-initialises it, passing the input through.
//
// The screen (content/display/tapeecho.svg, from docs/fx-briefs/designs/
// tape-echo.md via op1/gen_fx_svg.py), t = time 0..1, fb = feedback x 1.1
// up to 1:
//   mark1..mark4  per frame along the loop's top run (x 90..210, 30 apart),
//                 one spacing per 1.5 x the delay time (at most 10 a second)
//   echo1..echo8  matrix(1, 0, 0, h_i, (D - 20) i, 134 (1 - h_i)), D = 8 + 24 t,
//                 h_i = max(fb^(i-1), 0.02); green while i <= round(8 (1 - age))
//   age           sy(max(age, 0.05), 61)
//   head1..head3  lit per pattern
//   hold, loop    green while holding
//   mix           the mix bar
//   eng0, eng1    the delay engines' dots

#include "fx/op1/tape_echo_fx.h"

#include "fx/op1/half_rate.h"
#include "fx/op1/two_page.h"
#include "fx/src/tape_echo/tape_echo.h"
#include "op1/op1.h"

#ifndef OP1_SIM
#include "fx/op1/fx_screen.h"
#include "fx/op1/screen_anim.h"
#include "fx/src/crush/crush.h"   // Exp2NegQ16
#endif

namespace {

using op1fx::EncoderRange;
using op1fx::EncoderToValue;

const int32_t kPatternBits = 0xf;   // values[4] bits 0-3

// time .5 (~250 ms), feedback .45, age .3, mix .4.
const int32_t kDefaults[4] = { 16384, 14746, 9830, 13107 };

struct Instance {
  op1fx::TapeEcho echo;
  op1fx::HalfRate half;
  bool hold;
};

Instance instances[op1fx::kNumPositions];

int32_t Pattern(const int16_t* values) {
  const int32_t p = values[4] & kPatternBits;
  return p < op1fx::kTapeEchoNumPatterns ? p : 0;
}

void Values(const int16_t* encoders, const EncoderRange* ranges, int32_t v[4]) {
  for (int32_t k = 0; k < 4; ++k) v[k] = EncoderToValue(ranges[k], encoders[k]);
}

void Process(int32_t position, int32_t, int16_t* l, int16_t* r, const int16_t* live,
             const int16_t* values, const EncoderRange* ranges) {
  Instance& in = instances[position];
  const uint32_t start = op1::Cycles();
  if (!in.half.Begin(start)) {
    in.echo.Init();
    op1fx::measured_cycles += op1::Cycles() - start;
    return;
  }
  int32_t v[4];
  Values(live, ranges, v);
  op1fx::TapeEchoParams p;
  p.time = v[0];
  p.feedback = v[1];
  p.age = v[2];
  p.heads = Pattern(values);
  p.hold = in.hold;
  in.half.Decimate(l, r);
  in.echo.Process(in.half.in(0), r ? in.half.in(1) : nullptr, in.half.wet(0), r ? in.half.wet(1) : nullptr,
                  op1fx::kHalfN, p);
  in.half.Mix(l, r, v[3]);
  op1fx::measured_cycles += op1::Cycles() - start;
}

void Stop(int32_t position) { instances[position].half.Stop(); }

void Enter(int16_t* values, const EncoderRange* ranges) {
  op1fx::EnterOnePage(ranges, kDefaults, values);
}

bool EngineClick(int32_t position, int16_t* values, const EncoderRange*, op1fx::Click click,
                 int32_t direction, bool* changed) {
  *changed = false;
  if (click == op1fx::CLICK_BLUE) {
    const int32_t n = op1fx::kTapeEchoNumPatterns;
    const int32_t next = (Pattern(values) + direction + n) % n;
    values[4] = static_cast<int16_t>((values[4] & ~kPatternBits) | next);
    *changed = true;
    return true;
  }
  if (click == op1fx::CLICK_GREEN) {
    instances[position].hold = !instances[position].hold;   // not saved
    return true;
  }
  return false;
}

#ifndef OP1_SIM
using namespace op1fx::screen;

const char* const kMarkIds[4] = { "mark1", "mark2", "mark3", "mark4" };
const char* const kEchoIds[8] = { "echo1", "echo2", "echo3", "echo4", "echo5", "echo6", "echo7", "echo8" };
const char* const kHeadIds[3] = { "head1", "head2", "head3" };
const char* const kNodeIds[] = { "age", "mix", "hold", "loop" };
enum { N_AGE, N_MIX, N_HOLD, N_LOOP, N_COUNT };

bool ready;
void* mark_nodes[4];
void* echo_nodes[8];
void* nodes[N_COUNT];
EngineDots dots;
uint8_t* echo_line[8];
uint8_t* head_line[3];
uint8_t* head_fill[3];
uint8_t* hold_line;
uint8_t* loop_line;
Colour echo_on, echo_off, head_line_on, head_line_off, head_fill_on, head_fill_off, hold_off, loop_off;
op1fx::FrameClock clock;
uint32_t phase;

void ScreenInit(void* self) {
  void* document = DocumentOf(self);
  ready = false;
  if (!FindAll(document, kMarkIds, mark_nodes, 4) || !FindAll(document, kEchoIds, echo_nodes, 8) ||
      !FindAll(document, kNodeIds, nodes, N_COUNT) || !Find(document, "head1") ||
      !Find(document, "head2") || !Find(document, "head3") ||
      !dots.Init(self, 2, op1fx::kTapeEchoEngineId)) {
    return;
  }
  for (int32_t i = 0; i < 8; ++i) echo_line[i] = LineStyle(self, kEchoIds[i]);
  for (int32_t i = 0; i < 3; ++i) {
    head_line[i] = LineStyle(self, kHeadIds[i]);
    head_fill[i] = FillStyle(self, kHeadIds[i]);
  }
  hold_line = LineStyle(self, "hold");
  loop_line = LineStyle(self, "loop");
  // The file's demo: echoes 1-5 lit, 6-8 dim; heads 1 and 3 lit, 2 not;
  // hold off. Holding: hold and loop take the echoes' green.
  ReadColour(&echo_on, echo_line[0]);
  ReadColour(&echo_off, echo_line[7]);
  ReadColour(&head_line_on, head_line[0]);
  ReadColour(&head_fill_on, head_fill[0]);
  ReadColour(&head_line_off, head_line[1]);
  ReadColour(&head_fill_off, head_fill[1]);
  ReadColour(&hold_off, hold_line);
  ReadColour(&loop_off, loop_line);
  ready = true;
}

void ScreenUpdate(void* self, int32_t screen_position, const int16_t* encoders, const int16_t* values,
                  const EncoderRange* ranges) {
  if (!ready) return;
  int32_t v[4];
  Values(encoders, ranges, v);
  const bool hold =
      instances[screen_position == op1fx::kScreenMaster ? op1fx::POSITION_MASTER : op1fx::POSITION_MONO]
          .hold;
  dots.Show(op1fx::EngineDot(op1fx::kTapeEchoEngineId));

  // The marks: one 30 px spacing per 1.5 x the delay (0.03 x 50^t s):
  // 22.2 x 2^(-5.644 t) Hz, at most 10.
  const int32_t oct_q16 = static_cast<int32_t>((static_cast<int64_t>(v[0]) * 369886) >> 15);
  int32_t hz_q16 = static_cast<int32_t>((static_cast<uint64_t>(1455000) * op1fx::Exp2NegQ16(oct_q16)) >> 32);
  if (hz_q16 > (10 << 16)) hz_q16 = 10 << 16;
  phase += op1fx::PhaseStep(hz_q16, clock.Tick());   // the tape runs while holding too
  const int32_t off = static_cast<int32_t>(((phase >> 16) * 30) >> 6);   // 0..30 px, Q10
  for (int32_t i = 0; i < 4; ++i) {
    // From x 96 + 30 i, wrapping within 90..210.
    int32_t x = ((6 + 30 * i) << 10) + off;
    if (x >= (120 << 10)) x -= 120 << 10;
    SetTranslate(mark_nodes[i], x - ((6 + 30 * i) << 10), 0);
  }

  // The echoes.
  const int32_t delta = (8 << 10) + ((v[0] * 24) >> 5);   // Q10 px
  int32_t fb = v[1] + ((v[1] * 3277) >> 15);              // x 1.1
  if (fb > 32767 || hold) fb = 32768;
  const int32_t lit = 8 - Steps(v[2], 8);
  int32_t h = 32768;   // Q15: fb^(i-1)
  for (int32_t i = 1; i <= 8; ++i) {
    const int32_t hq10 = h < 655 ? 20 : h >> 5;
    SetMatrix(echo_nodes[i - 1], 0x400, hq10, (delta - (20 << 10)) * i, 134 * (0x400 - hq10));
    WriteColour(echo_line[i - 1], i <= lit ? echo_on : echo_off);
    h = (h * fb) >> 15;
  }
  int32_t sa = v[2] >> 5;
  if (sa < 51) sa = 51;
  SetMatrix(nodes[N_AGE], 0x400, sa, 0, 61 * (0x400 - sa));

  const int32_t heads = op1fx::kTapeEchoPatternHeads[Pattern(values)];
  for (int32_t i = 0; i < 3; ++i) {
    const bool on = (heads >> i) & 1;
    WriteColour(head_line[i], on ? head_line_on : head_line_off);
    WriteColour(head_fill[i], on ? head_fill_on : head_fill_off);
  }
  WriteColour(hold_line, hold ? echo_on : hold_off);
  WriteColour(loop_line, hold ? echo_on : loop_off);
  SetMixBar(nodes[N_MIX], v[3]);
}
#endif

}  // namespace

namespace op1fx {

const Engine kTapeEchoEngine = {
  kTapeEchoEngineId, "tapeecho.svg", Process, nullptr, Stop, Enter, EngineClick,
#ifdef OP1_SIM
  nullptr, nullptr,
#else
  ScreenInit, ScreenUpdate,
#endif
};

}  // namespace op1fx
