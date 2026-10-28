// HALL (FDN hall reverb) as a kennel effect plugin, id cs_hall. The core of
// fx/src/hall/hall.h at half the effect rate behind fx/op1/half_rate.h, one
// instance per position in its scratch buffer (common/knl_fx.h).
//   page 1: blue size, green decay, white damp, orange mix
//   presses: blue -> pre-delay (word 5), green -> early (word 6),
//            white -> width (word 7, master only); orange latches freeze
//   fixed: motion at its default (the firmware build's fourth page-2 knob)
//
// The screen is the firmware build's hall.svg, category dots removed,
// animated as in fx/op1/hall_fx.cc; page 2 is shown while any of the
// three page knobs is pressed.

#include "common/knl_fx.h"
#include "fx/op1/fx_screen.h"
#include "fx/op1/half_rate.h"
#include "fx/src/hall/hall.h"
#include "op1/op1.h"

namespace {

knl::Fine fine;   // shift + a page-2 knob: quarter clicks

const int32_t kMotion = 9830;
enum { P_SIZE, P_DECAY, P_DAMP, P_MIX, P_PREDELAY, P_EARLY, P_MOTION, P_WIDTH };

struct Instance {
  op1fx::HallReverb hall;
  op1fx::HalfRate half;
};

op1fx::HallParams Params(const int32_t v[8], bool freeze) {
  op1fx::HallParams q;
  q.size = v[P_SIZE];
  q.decay = v[P_DECAY];
  q.damp = v[P_DAMP];
  q.predelay = v[P_PREDELAY];
  q.early = v[P_EARLY];
  q.motion = v[P_MOTION];
  q.width = v[P_WIDTH];
  q.freeze = freeze;
  return q;
}

void Values(const int16_t* p, int32_t v[8]) {
  knl::Page1(p, v);
  v[P_PREDELAY] = knl::Word(p, 5);
  v[P_EARLY] = knl::Word(p, 6);
  v[P_WIDTH] = knl::Word(p, 7);
  v[P_MOTION] = kMotion;
}

void Init(int pos) {
  fine.Reset(pos);
  Instance* in = knl::Instance<Instance>(pos);
  if (!in) return;
  knl::Zero(in, sizeof(*in));
  in->hall.Init();
  in->half.Stop();
}

void Process(int pos, Instance* in, int16_t* l, int16_t* r, const int16_t* p) {
  if (!in->half.Begin(op1::Cycles())) {
    in->hall.Init();
    in->half.End();
    return;
  }
  int32_t v[8];
  Values(p, v);
  in->half.Decimate(l, r);
  in->hall.Process(in->half.in(0), r ? in->half.in(1) : nullptr, in->half.wet(0),
                   r ? in->half.wet(1) : nullptr, op1fx::kHalfN, Params(v, knl::Pressed(pos, 3)));
  in->half.Mix(l, r, v[P_MIX]);
  in->half.End();
}

void Render(int pos, int16_t* buf, const int16_t* p) {
  fine.Apply(pos, 7);   // shift + a page-2 knob: quarter clicks
  Instance* in = knl::Instance<Instance>(pos);
  if (in) Process(pos, in, buf, nullptr, p);
}

void RenderStereo(int pos, int16_t* l, int16_t* r, const int16_t* p) {
  fine.Apply(pos, 7);
  Instance* in = knl::Instance<Instance>(pos);
  if (in) Process(pos, in, l, r, p);
}

// ---- screen ----

using namespace op1fx::screen;

const char* const kNodeIds[] = { "page1", "page2", "size", "room", "refl", "decay", "decayinf", "damp",
                                 "mix", "predelay", "early", "late", "motionL", "motionR", "widthL",
                                 "widthR" };
enum { N_PAGE1, N_PAGE2, N_SIZE, N_ROOM, N_REFL, N_DECAY, N_DECAYINF, N_DAMP, N_MIX, N_PREDELAY,
       N_EARLY, N_LATE, N_MOTIONL, N_MOTIONR, N_WIDTHL, N_WIDTHR, N_COUNT };
static_assert(sizeof(kNodeIds) / sizeof(kNodeIds[0]) == N_COUNT, "node ids");

bool ready;
void* nodes[N_COUNT];
uint8_t* page_line[2];
uint8_t* page_fill[2];
uint8_t* freeze_line;
Colour page_line_on, page_line_off, page_fill_on, page_fill_off, freeze_off, freeze_on;

void ScreenInit(void* scr) {
  void* document = DocumentOf(scr);
  ready = false;
  if (!FindAll(document, kNodeIds, nodes, N_COUNT) || !Find(document, "page1ind") ||
      !Find(document, "page2ind") || !Find(document, "freeze")) {
    return;
  }
  page_line[0] = LineStyle(scr, "page1ind");
  page_line[1] = LineStyle(scr, "page2ind");
  page_fill[0] = FillStyle(scr, "page1ind");
  page_fill[1] = FillStyle(scr, "page2ind");
  freeze_line = LineStyle(scr, "freeze");
  ReadColour(&page_line_on, page_line[0]);
  ReadColour(&page_fill_on, page_fill[0]);
  ReadColour(&page_line_off, page_line[1]);
  ReadColour(&page_fill_off, page_fill[1]);
  ReadColour(&freeze_off, freeze_line);
  ReadColour(&freeze_on, LineStyle(scr, "decay"));
  ready = true;
}

inline int32_t Px(int32_t v15, int32_t k) { return (v15 * k) >> 5; }

void ScreenUpdate(void* scr, int dt) {
  if (ready) {
    const int32_t pos = knl::ScreenPos();
    int32_t v[8];
    knl::ScreenPage1(v);
    v[P_PREDELAY] = knl::ScreenWord(5);
    v[P_EARLY] = knl::ScreenWord(6);
    v[P_WIDTH] = knl::ScreenWord(7);
    v[P_MOTION] = kMotion;
    const bool frozen = knl::Pressed(pos, 3);
    const bool page2 = knl::AnyPage2(pos, 0x7);
    const bool infinite = frozen || v[P_DECAY] >= op1fx::kHallInfinite;

    SetVisible(nodes[N_PAGE1], !page2);
    SetVisible(nodes[N_PAGE2], page2);
    WriteColour(page_line[0], page2 ? page_line_off : page_line_on);
    WriteColour(page_fill[0], page2 ? page_fill_off : page_fill_on);
    WriteColour(page_line[1], page2 ? page_line_on : page_line_off);
    WriteColour(page_fill[1], page2 ? page_fill_on : page_fill_off);
    WriteColour(freeze_line, frozen ? freeze_on : freeze_off);

    const int32_t sz = 410 + ((v[P_SIZE] * 614) >> 15);
    static const int32_t kHallNodes[3] = { N_SIZE, N_ROOM, N_REFL };
    for (int32_t n : kHallNodes) SetMatrix(nodes[n], sz, sz, 150 * (0x400 - sz), 84 * (0x400 - sz));
    const int32_t x0 = (34 << 10) + Px(v[P_PREDELAY], 50);
    const int32_t sd = infinite ? 0x400 : 154 + ((v[P_DECAY] * 870) >> 15);
    SetMatrix(nodes[N_DECAY], sd, 0x400, x0 - 34 * sd, 0);
    SetVisible(nodes[N_DECAYINF], infinite);
    const int32_t sh = (sd * (1024 - ((v[P_DAMP] * 819) >> 15))) >> 10;
    SetMatrix(nodes[N_DAMP], sh, 0x400, x0 - 34 * sh, 0);
    SetMixBar(nodes[N_MIX], v[P_MIX]);

    int32_t sp = (x0 - (30 << 10)) >> 3;
    if (sp < 0) sp = 0;
    ScaleX(nodes[N_PREDELAY], sp, 27);
    const int32_t se = 205 + ((v[P_EARLY] * 819) >> 15);
    SetMatrix(nodes[N_EARLY], 0x400, se, x0 - (34 << 10), 134 * (0x400 - se));
    const int32_t sl = 0x400 - ((v[P_EARLY] * 717) >> 15);
    SetMatrix(nodes[N_LATE], sd, sl, x0 - (34 << 10) + 70 * (0x400 - sd), 134 * (0x400 - sl));
    ScaleX(nodes[N_MOTIONL], v[P_MOTION] >> 5, 30);
    ScaleX(nodes[N_MOTIONR], v[P_MOTION] >> 5, 270);
    const int32_t w = pos == OP1_FX_MASTER ? Px(v[P_WIDTH], 14) : 0;
    SetTranslate(nodes[N_WIDTHL], -w, 0);
    SetTranslate(nodes[N_WIDTHR], w, 0);
  }
  op1_screen_draw(scr, dt);
}

}  // namespace

extern "C" const op1_fx_stereo_t FX_hall = {
  { Init, Render, { OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_LATCH } },
  RenderStereo,
};

extern "C" const op1_screen_t SCREEN_hall = { ScreenInit, ScreenUpdate };
