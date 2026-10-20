// HALL on the OP-1 (op1/hall_fx.h). The core runs at half the effect rate
// behind op1/half_rate.h (Clouds' halfband filters and an equal-power
// mix). One instance per position; a restart (the host's stop, or a pause
// over 5 ms) re-initialises it, passing the input through.
//
// Settings: two pages (op1/two_page.h); freeze is the instance's.
//
// The screen (content/display/hall.svg, from docs/fx-briefs/designs/
// hall.md via op1/gen_fx_svg.py), mappings as in that note.

#include "fx/op1/hall_fx.h"

#include "fx/op1/half_rate.h"
#include "fx/op1/two_page.h"
#include "fx/src/hall/hall.h"
#include "op1/op1.h"

#ifndef OP1_SIM
#include "fx/op1/fx_screen.h"
#endif

namespace {

using op1fx::EncoderRange;

// Defaults, page-major: size .6, decay .5, damp .3, mix .35; pre-delay .1,
// early .5, motion .3, width 1.
const int32_t kDefaults[8] = { 19661, 16384, 9830, 11469, 3277, 16384, 9830, 32767 };

enum { P_SIZE, P_DECAY, P_DAMP, P_MIX, P_PREDELAY, P_EARLY, P_MOTION, P_WIDTH };

struct Instance {
  op1fx::HallReverb hall;
  op1fx::HalfRate half;
  bool freeze;
};

Instance instances[op1fx::kNumPositions];

op1fx::HallParams Params(const int32_t v[8], bool freeze) {
  op1fx::HallParams p;
  p.size = v[P_SIZE];
  p.decay = v[P_DECAY];
  p.damp = v[P_DAMP];
  p.predelay = v[P_PREDELAY];
  p.early = v[P_EARLY];
  p.motion = v[P_MOTION];
  p.width = v[P_WIDTH];
  p.freeze = freeze;
  return p;
}

void Process(int32_t position, int32_t, int16_t* l, int16_t* r, const int16_t* live,
             const int16_t* values, const EncoderRange* ranges) {
  Instance& in = instances[position];
  const uint32_t start = op1::Cycles();
  if (!in.half.Begin(start)) {
    in.hall.Init();
    op1fx::measured_cycles += op1::Cycles() - start;
    return;
  }
  const op1fx::TwoPage s = op1fx::DecodeTwoPage(values, kDefaults);
  int32_t v[8];
  op1fx::TwoPageValues(ranges, live, s, v);
  in.half.Decimate(l, r);
  in.hall.Process(in.half.in(0), r ? in.half.in(1) : nullptr, in.half.wet(0), r ? in.half.wet(1) : nullptr,
                  op1fx::kHalfN, Params(v, in.freeze));
  in.half.Mix(l, r, v[P_MIX]);
  op1fx::measured_cycles += op1::Cycles() - start;
}

void Stop(int32_t position) { instances[position].half.Stop(); }

void Enter(int16_t* values, const EncoderRange* ranges) {
  op1fx::EnterTwoPage(ranges, kDefaults, values);
}

bool EngineClick(int32_t position, int16_t* values, const EncoderRange* ranges, op1fx::Click click,
                 int32_t, bool* changed) {
  *changed = false;
  if (click == op1fx::CLICK_GREEN) {
    instances[position].freeze = !instances[position].freeze;   // not saved
    return true;
  }
  if (click == op1fx::CLICK_WHITE) {
    op1fx::SwapTwoPage(ranges, kDefaults, values);
    *changed = true;
    return true;
  }
  return false;
}

#ifndef OP1_SIM
using namespace op1fx::screen;

const char* const kNodeIds[] = { "page1", "page2", "size", "room", "refl", "decay", "decayinf", "damp",
                                 "mix", "predelay", "early", "late", "motionL", "motionR", "widthL",
                                 "widthR" };
enum { N_PAGE1, N_PAGE2, N_SIZE, N_ROOM, N_REFL, N_DECAY, N_DECAYINF, N_DAMP, N_MIX, N_PREDELAY,
       N_EARLY, N_LATE, N_MOTIONL, N_MOTIONR, N_WIDTHL, N_WIDTHR, N_COUNT };
static_assert(sizeof(kNodeIds) / sizeof(kNodeIds[0]) == N_COUNT, "node ids");

bool ready;
void* nodes[N_COUNT];
EngineDots dots;
uint8_t* page_line[2];
uint8_t* page_fill[2];
uint8_t* freeze_line;
Colour page_line_on, page_line_off, page_fill_on, page_fill_off, freeze_off, freeze_on;

void ScreenInit(void* self) {
  void* document = DocumentOf(self);
  ready = false;
  if (!FindAll(document, kNodeIds, nodes, N_COUNT) || !Find(document, "page1ind") ||
      !Find(document, "page2ind") || !Find(document, "freeze") ||
      !dots.Init(self, 4, op1fx::kHallEngineId)) {
    return;
  }
  page_line[0] = LineStyle(self, "page1ind");
  page_line[1] = LineStyle(self, "page2ind");
  page_fill[0] = FillStyle(self, "page1ind");
  page_fill[1] = FillStyle(self, "page2ind");
  freeze_line = LineStyle(self, "freeze");
  // The file's defaults: page 1 lit, page 2 not, freeze off; frozen is
  // green, the decay envelope's colour.
  ReadColour(&page_line_on, page_line[0]);
  ReadColour(&page_fill_on, page_fill[0]);
  ReadColour(&page_line_off, page_line[1]);
  ReadColour(&page_fill_off, page_fill[1]);
  ReadColour(&freeze_off, freeze_line);
  ReadColour(&freeze_on, LineStyle(self, "decay"));
  ready = true;
}

// v15 x k / 32768 in Q10 pixels: v15 x k >> 5.
inline int32_t Px(int32_t v15, int32_t k) { return (v15 * k) >> 5; }

void ScreenUpdate(void* self, int32_t screen_position, const int16_t* encoders,
                  const int16_t* values, const EncoderRange* ranges) {
  if (!ready) return;
  const op1fx::TwoPage s = op1fx::DecodeTwoPage(values, kDefaults);
  int32_t v[8];
  op1fx::TwoPageValues(ranges, encoders, s, v);
  const bool frozen =
      instances[screen_position == op1fx::kScreenMaster ? op1fx::POSITION_MASTER : op1fx::POSITION_MONO]
          .freeze;
  const bool page2 = s.page == 1;
  const bool infinite = frozen || v[P_DECAY] >= op1fx::kHallInfinite;

  dots.Show(op1fx::EngineDot(op1fx::kHallEngineId));
  SetVisible(nodes[N_PAGE1], !page2);
  SetVisible(nodes[N_PAGE2], page2);
  WriteColour(page_line[0], page2 ? page_line_off : page_line_on);
  WriteColour(page_fill[0], page2 ? page_fill_off : page_fill_on);
  WriteColour(page_line[1], page2 ? page_line_on : page_line_off);
  WriteColour(page_fill[1], page2 ? page_fill_on : page_fill_off);
  WriteColour(freeze_line, frozen ? freeze_on : freeze_off);

  // The hall: s = 0.4 + 0.6 size about the floor centre (150, 84).
  const int32_t sz = 410 + ((v[P_SIZE] * 614) >> 15);
  static const int32_t kHallNodes[3] = { N_SIZE, N_ROOM, N_REFL };
  for (int32_t n : kHallNodes) SetMatrix(nodes[n], sz, sz, 150 * (0x400 - sz), 84 * (0x400 - sz));
  // The tail starts at x0 = 34 + 50 x pre-delay; decay scales it about x0:
  // s = 0.15 + 0.85 x decay (1 at infinity), matrix(s, 0, 0, 1, x0 - 34 s, 0).
  const int32_t x0 = (34 << 10) + Px(v[P_PREDELAY], 50);
  const int32_t sd = infinite ? 0x400 : 154 + ((v[P_DECAY] * 870) >> 15);
  SetMatrix(nodes[N_DECAY], sd, 0x400, x0 - 34 * sd, 0);
  SetVisible(nodes[N_DECAYINF], infinite);
  // Damp: the highs' envelope, sd x (1 - 0.8 damp).
  const int32_t sh = (sd * (1024 - ((v[P_DAMP] * 819) >> 15))) >> 10;
  SetMatrix(nodes[N_DAMP], sh, 0x400, x0 - 34 * sh, 0);
  SetMixBar(nodes[N_MIX], v[P_MIX]);

  // Page 2. The pre-delay gap: x 27..35 scaled by (x0 - 30) / 8 about 27.
  int32_t sp = (x0 - (30 << 10)) >> 3;
  if (sp < 0) sp = 0;
  ScaleX(nodes[N_PREDELAY], sp, 27);
  // Early: se = 0.2 + 0.8 early, from the floor (y 134), moved to x0.
  const int32_t se = 205 + ((v[P_EARLY] * 819) >> 15);
  SetMatrix(nodes[N_EARLY], 0x400, se, x0 - (34 << 10), 134 * (0x400 - se));
  // Late: drawn from x 70; x by the decay's s, y by 1 - 0.7 early.
  const int32_t sl = 0x400 - ((v[P_EARLY] * 717) >> 15);
  SetMatrix(nodes[N_LATE], sd, sl, x0 - (34 << 10) + 70 * (0x400 - sd), 134 * (0x400 - sl));
  ScaleX(nodes[N_MOTIONL], v[P_MOTION] >> 5, 30);
  ScaleX(nodes[N_MOTIONR], v[P_MOTION] >> 5, 270);
  const int32_t w = screen_position == op1fx::kScreenMaster ? Px(v[P_WIDTH], 14) : 0;
  SetTranslate(nodes[N_WIDTHL], -w, 0);
  SetTranslate(nodes[N_WIDTHR], w, 0);
}
#endif

}  // namespace

namespace op1fx {

const Engine kHallEngine = {
  kHallEngineId, "hall.svg", Process, nullptr, Stop, Enter, EngineClick,
#ifdef OP1_SIM
  nullptr, nullptr,
#else
  ScreenInit, ScreenUpdate,
#endif
};

}  // namespace op1fx
