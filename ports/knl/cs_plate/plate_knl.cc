// PLATE (Dattorro plate reverb) as a kennel effect plugin, id cs_plate.
// The core of fx/src/plate/plate.h at half the effect rate behind
// fx/op1/half_rate.h, one instance per position in its scratch buffer
// (common/knl_fx.h).
//   page 1: blue decay, green damp, white pre-delay, orange mix
//   presses: blue -> size (word 5), green -> diffusion (word 6),
//            white -> motion (word 7); orange latches freeze (not saved)
//   fixed: low cut at its default (the firmware build's fourth page-2
//          knob; kennel's orange press can't be a page knob)
//
// The screen is the firmware build's plate.svg, category dots removed,
// animated as in fx/op1/plate_fx.cc; page 2 is shown while any of the
// three page knobs is pressed.

#include "common/knl_fx.h"
#include "fx/op1/fx_screen.h"
#include "fx/op1/half_rate.h"
#include "fx/src/plate/plate.h"
#include "op1/op1.h"

namespace {

knl::Fine fine;   // shift + a page-2 knob: quarter clicks

const int32_t kLowCut = 3277;
enum { P_DECAY, P_DAMP, P_PREDELAY, P_MIX, P_SIZE, P_DIFFUSION, P_MOTION, P_LOWCUT };

struct Instance {
  op1fx::PlateReverb plate;
  op1fx::HalfRate half;
};

op1fx::PlateParams Params(const int32_t v[8], bool freeze) {
  op1fx::PlateParams q;
  q.decay = v[P_DECAY];
  q.damp = v[P_DAMP];
  q.predelay = v[P_PREDELAY];
  q.size = (v[P_SIZE] * 7 + 16383) / 32767;   // 0..7
  q.diffusion = v[P_DIFFUSION];
  q.motion = v[P_MOTION];
  q.lowcut = v[P_LOWCUT];
  q.freeze = freeze;
  return q;
}

void Values(const int16_t* p, int32_t v[8]) {
  knl::Page1(p, v);
  v[P_SIZE] = knl::Word(p, 5);
  v[P_DIFFUSION] = knl::Word(p, 6);
  v[P_MOTION] = knl::Word(p, 7);
  v[P_LOWCUT] = kLowCut;
}

void Init(int pos) {
  fine.Reset(pos);
  Instance* in = knl::Instance<Instance>(pos);
  if (!in) return;
  knl::Zero(in, sizeof(*in));
  in->plate.Init();
  in->half.Stop();
}

void Process(int pos, Instance* in, int16_t* l, int16_t* r, const int16_t* p) {
  if (!in->half.Begin(op1::Cycles())) {
    in->plate.Init();
    in->half.End();
    return;
  }
  int32_t v[8];
  Values(p, v);
  in->half.Decimate(l, r);
  in->plate.Process(in->half.in(0), r ? in->half.in(1) : nullptr, in->half.wet(0),
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

const char* const kRingIds[4] = { "ring1", "ring2", "ring3", "ring4" };
const char* const kDiffIds[8] = { "diff1", "diff2", "diff3", "diff4", "diff5", "diff6", "diff7", "diff8" };
const char* const kNodeIds[] = { "page1", "page2", "predelay", "decay", "damp", "mix", "size",
                                 "motionL", "motionR", "tail", "lowcut" };
enum { N_PAGE1, N_PAGE2, N_PREDELAY, N_DECAY, N_DAMP, N_MIX, N_SIZE, N_MOTIONL, N_MOTIONR, N_TAIL,
       N_LOWCUT, N_COUNT };
static_assert(sizeof(kNodeIds) / sizeof(kNodeIds[0]) == N_COUNT, "node ids");

bool ready;
void* nodes[N_COUNT];
void* ring_nodes[4];
void* diff_nodes[8];
uint8_t* page_line[2];
uint8_t* page_fill[2];
uint8_t* freeze_line;
Colour page_line_on, page_line_off, page_fill_on, page_fill_off, freeze_off, freeze_on;

void ScreenInit(void* scr) {
  void* document = DocumentOf(scr);
  ready = false;
  if (!FindAll(document, kNodeIds, nodes, N_COUNT) || !FindAll(document, kRingIds, ring_nodes, 4) ||
      !FindAll(document, kDiffIds, diff_nodes, 8) || !Find(document, "page1ind") ||
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
  ReadColour(&freeze_on, LineStyle(scr, "damp"));
  ready = true;
}

inline int32_t Px(int32_t v15, int32_t k) { return (v15 * k) >> 5; }

void ScreenUpdate(void* scr, int dt) {
  if (ready) {
    const int32_t pos = knl::ScreenPos();
    int32_t v[8];
    knl::ScreenPage1(v);
    v[P_SIZE] = knl::ScreenWord(5);
    v[P_DIFFUSION] = knl::ScreenWord(6);
    v[P_MOTION] = knl::ScreenWord(7);
    v[P_LOWCUT] = kLowCut;
    const bool frozen = knl::Pressed(pos, 3);
    const bool page2 = knl::AnyPage2(pos, 0x7);
    const int32_t decay = frozen ? 32767 : v[P_DECAY];

    SetVisible(nodes[N_PAGE1], !page2);
    SetVisible(nodes[N_PAGE2], page2);
    WriteColour(page_line[0], page2 ? page_line_off : page_line_on);
    WriteColour(page_fill[0], page2 ? page_fill_off : page_fill_on);
    WriteColour(page_line[1], page2 ? page_line_on : page_line_off);
    WriteColour(page_fill[1], page2 ? page_fill_on : page_fill_off);
    WriteColour(freeze_line, frozen ? freeze_on : freeze_off);

    const int32_t x0 = (34 << 10) + Px(v[P_PREDELAY], 50);
    const int32_t sd = 154 + ((decay * 870) >> 15);
    SetMatrix(nodes[N_DECAY], sd, 0x400, x0 - 34 * sd, 0);
    SetMatrix(nodes[N_TAIL], sd, 0x400, x0 - 34 * sd, 0);
    const int32_t sh = (sd * (1024 - ((v[P_DAMP] * 819) >> 15))) >> 10;
    SetMatrix(nodes[N_DAMP], sh, 0x400, x0 - 34 * sh, 0);
    int32_t rings = (decay * 4 + 32766) / 32767;
    if (rings < 1) rings = 1;
    ShowFirst(ring_nodes, 4, rings);
    int32_t sp = (x0 - (30 << 10)) >> 3;
    if (sp < 0) sp = 0;
    ScaleX(nodes[N_PREDELAY], sp, 27);
    SetMixBar(nodes[N_MIX], v[P_MIX]);

    const int32_t ss = 614 + 67 * Steps(v[P_SIZE], 7);
    SetMatrix(nodes[N_SIZE], ss, ss, 150 * (0x400 - ss), 54 * (0x400 - ss));
    ShowFirst(diff_nodes, 8, 1 + Steps(v[P_DIFFUSION], 7));
    for (int32_t i = 0; i < 8; ++i) SetTranslate(diff_nodes[i], x0 - (54 << 10), 0);
    ScaleX(nodes[N_MOTIONL], v[P_MOTION] >> 5, 56);
    ScaleX(nodes[N_MOTIONR], v[P_MOTION] >> 5, 244);
    ScaleX(nodes[N_LOWCUT], 1024 - ((v[P_LOWCUT] * 683) >> 15), 288);
  }
  op1_screen_draw(scr, dt);
}

}  // namespace

extern "C" const op1_fx_stereo_t FX_plate = {
  { Init, Render, { OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_LATCH } },
  RenderStereo,
};

extern "C" const op1_screen_t SCREEN_plate = { ScreenInit, ScreenUpdate };
