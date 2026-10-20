// DRIVE on the OP-1 (op1/drive_fx.h): the DSP at the effect rate (it
// oversamples inside), one instance per position, restarted when the host
// stops it there.
//
// The screen (content/display/drive.svg, from docs/fx-briefs/designs/
// drive.md and drive-valve.md via op1/gen_fx_svg.py):
//   curve0..curve7  variant set, round(7 x drive)
//   fil, glow       the valve heats with bias: the filament's line and the
//                   glow's fill blend from cold to hot, per byte, between
//                   colours read from the file (cold: eng0's dim line and
//                   fill; hot: the valve's white envelope and eng0's line)
//   heat0..heat2    count set, min(3, floor(4 x bias))
//   tone            bipolar tilt about y 78: sy(2t - 1, 78)
//   mix             the mix bar
//   eng0..eng4      the dirt engines' dots

#include "fx/op1/drive_fx.h"

#include "fx/op1/two_page.h"
#include "fx/src/drive/drive.h"
#include "op1/op1.h"

#ifndef OP1_SIM
#include "fx/op1/fx_screen.h"
#endif

namespace {

using op1fx::EncoderRange;
using op1fx::EncoderToValue;

// drive .35, tone flat, bias 0 (odd), mix 1.
const int32_t kDefaults[4] = { 11469, 16384, 0, 32767 };

struct Instance {
  op1fx::Drive drive;
  bool running;
};

Instance instances[op1fx::kNumPositions];

op1fx::DriveParams Params(const int16_t* encoders, const EncoderRange* ranges) {
  op1fx::DriveParams p;
  p.drive = EncoderToValue(ranges[0], encoders[0]);
  p.tone = EncoderToValue(ranges[1], encoders[1]);
  p.bias = EncoderToValue(ranges[2], encoders[2]);
  p.mix = EncoderToValue(ranges[3], encoders[3]);
  return p;
}

void Process(int32_t position, int32_t, int16_t* l, int16_t* r, const int16_t* live,
             const int16_t*, const EncoderRange* ranges) {
  Instance& in = instances[position];
  const uint32_t start = op1::Cycles();
  if (!in.running) {
    in.drive.Init();
    in.running = true;
  }
  in.drive.Process(l, r, op1fx::kFxBlockSize, Params(live, ranges));
  op1fx::measured_cycles += op1::Cycles() - start;
}

void Stop(int32_t position) { instances[position].running = false; }

void Enter(int16_t* values, const EncoderRange* ranges) {
  op1fx::EnterOnePage(ranges, kDefaults, values);
}

#ifndef OP1_SIM
using namespace op1fx::screen;

const char* const kCurveIds[8] = { "curve0", "curve1", "curve2", "curve3",
                                   "curve4", "curve5", "curve6", "curve7" };
const char* const kHeatIds[3] = { "heat0", "heat1", "heat2" };
const char* const kValveIds[3] = { "fil", "glow", "envelope" };

// From a to b by v (0..32767), each byte on its own (the byte order of a
// style doesn't matter).
Colour Blend(const Colour& a, const Colour& b, int32_t v) {
  Colour c;
  for (int32_t i = 0; i < 4; ++i) c.rgba[i] = static_cast<uint8_t>(a.rgba[i] + (((b.rgba[i] - a.rgba[i]) * v) >> 15));
  return c;
}

bool ready;
void* curve_nodes[8];
void* heat_nodes[3];
uint8_t* fil_line;
uint8_t* glow_fill;
op1fx::screen::Colour fil_cold, fil_hot, glow_cold, glow_hot;
void* tone_node;
void* mix_node;
EngineDots dots;

void ScreenInit(void* self) {
  void* document = DocumentOf(self);
  void* valve[3];
  ready = FindAll(document, kCurveIds, curve_nodes, 8) && FindAll(document, kHeatIds, heat_nodes, 3) &&
          FindAll(document, kValveIds, valve, 3) && (tone_node = Find(document, "tone")) &&
          (mix_node = Find(document, "mix")) && dots.Init(self, 5, op1fx::kDriveEngineId);
  if (!ready) return;
  // eng0 is drawn dim (drive's own dot is eng1): read before Show rewrites it.
  fil_line = LineStyle(self, "fil");
  glow_fill = FillStyle(self, "glow");
  ReadColour(&fil_cold, LineStyle(self, "eng0"));
  ReadColour(&fil_hot, LineStyle(self, "envelope"));
  ReadColour(&glow_cold, FillStyle(self, "eng0"));
  glow_hot = fil_cold;
}

void ScreenUpdate(void* self, int32_t, const int16_t* encoders, const int16_t*,
                  const EncoderRange* ranges) {
  if (!ready) return;
  const op1fx::DriveParams p = Params(encoders, ranges);
  dots.Show(op1fx::EngineDot(op1fx::kDriveEngineId));
  ShowOne(curve_nodes, 8, Steps(p.drive, 7));
  WriteColour(fil_line, Blend(fil_cold, fil_hot, p.bias));
  WriteColour(glow_fill, Blend(glow_cold, glow_hot, p.bias));
  const int32_t heat = (p.bias * 4) >> 15;
  ShowFirst(heat_nodes, 3, heat < 3 ? heat : 3);
  // d = 2t - 1 (Q10), about y 78: matrix(1, 0, 0, d, 0, 78 (1 - d)).
  const int32_t d = (p.tone >> 4) - 0x400;
  SetMatrix(tone_node, 0x400, d, 0, 78 * (0x400 - d));
  SetMixBar(mix_node, p.mix);
}
#endif

}  // namespace

namespace op1fx {

const Engine kDriveEngine = {
  kDriveEngineId, "drive.svg", Process, nullptr, Stop, Enter, nullptr,
#ifdef OP1_SIM
  nullptr, nullptr,
#else
  ScreenInit, ScreenUpdate,
#endif
};

}  // namespace op1fx
