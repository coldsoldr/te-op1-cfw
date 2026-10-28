// Clouds' screen as a kennel plugin (common/clouds_knl.h): the firmware
// build's ports/clouds/op1/clouds_screen.cc with the category dots removed
// and kennel's init/update entry points. Mappings: docs/module-briefs/
// designs/clouds.md. Colours are read from the SVG's own elements in their
// default state (mode0 lit, mode1 not, mode3 disabled; page 1 lit, page 2
// not).

#include "common/clouds_knl.h"
#include "common/knl_fx.h"
#include "fx/op1/fx_screen.h"
#include "src/core/params.h"

namespace {

using namespace op1fx::screen;

const char* const kSizeIds[6] = { "size0", "size1", "size2", "size3", "size4", "size5" };
const char* const kRegIds[8] = { "reg0", "reg1", "reg2", "reg3", "reg4", "reg5", "reg6", "reg7" };
const char* const kRndIds[8] = { "rnd0", "rnd1", "rnd2", "rnd3", "rnd4", "rnd5", "rnd6", "rnd7" };
const char* const kTextureIds[5] = { "texture0", "texture1", "texture2", "texture3", "texture4" };
const char* const kWetIds[5] = { "wet0", "wet1", "wet2", "wet3", "wet4" };
const char* const kFbIds[5] = { "fb0", "fb1", "fb2", "fb3", "fb4" };
const char* const kRvIds[5] = { "rv0", "rv1", "rv2", "rv3", "rv4" };
const char* const kModeIds[4] = { "mode0", "mode1", "mode2", "mode3" };
const char* const kNodeIds[] = {
  "page1", "page2", "position", "pitch", "face", "face_frozen", "freeze_off", "freeze_on",
  "page1ind", "page2ind" };
enum {
  N_PAGE1, N_PAGE2, N_POSITION, N_PITCH, N_FACE, N_FACE_FROZEN, N_FREEZE_OFF, N_FREEZE_ON,
  N_PAGE1IND, N_PAGE2IND, N_COUNT };
static_assert(sizeof(kNodeIds) / sizeof(kNodeIds[0]) == N_COUNT, "node ids");

bool ready;
void* nodes[N_COUNT];
void* size_nodes[6];
void* reg_nodes[8];
void* rnd_nodes[8];
void* texture_nodes[5];
void* wet_nodes[5];
void* fb_nodes[5];
void* rv_nodes[5];
void* mode_nodes[4];
uint8_t* mode_line[4];
uint8_t* mode_fill[4];
uint8_t* page_ind_line[2];
uint8_t* page_ind_fill[2];
Colour mode_line_on, mode_line_off, mode_fill_on, mode_fill_off, mode_line_disabled;
Colour ind_line_on, ind_line_off, ind_fill_on, ind_fill_off;

void ScreenInit(void* scr) {
  ready = false;
  void* document = DocumentOf(scr);
  if (!document) return;
  if (!FindAll(document, kNodeIds, nodes, N_COUNT) || !FindAll(document, kSizeIds, size_nodes, 6) ||
      !FindAll(document, kRegIds, reg_nodes, 8) || !FindAll(document, kRndIds, rnd_nodes, 8) ||
      !FindAll(document, kTextureIds, texture_nodes, 5) ||
      !FindAll(document, kWetIds, wet_nodes, 5) || !FindAll(document, kFbIds, fb_nodes, 5) ||
      !FindAll(document, kRvIds, rv_nodes, 5) || !FindAll(document, kModeIds, mode_nodes, 4)) {
    return;
  }
  for (int32_t i = 0; i < 4; ++i) {
    mode_line[i] = LineStyle(scr, kModeIds[i]);
    mode_fill[i] = FillStyle(scr, kModeIds[i]);
  }
  page_ind_line[0] = LineStyle(scr, "page1ind");
  page_ind_line[1] = LineStyle(scr, "page2ind");
  page_ind_fill[0] = FillStyle(scr, "page1ind");
  page_ind_fill[1] = FillStyle(scr, "page2ind");
  ReadColour(&mode_line_on, mode_line[0]);
  ReadColour(&mode_fill_on, mode_fill[0]);
  ReadColour(&mode_line_off, mode_line[1]);
  ReadColour(&mode_fill_off, mode_fill[1]);
  ReadColour(&mode_line_disabled, mode_line[3]);
  ReadColour(&ind_line_on, page_ind_line[0]);
  ReadColour(&ind_fill_on, page_ind_fill[0]);
  ReadColour(&ind_line_off, page_ind_line[1]);
  ReadColour(&ind_fill_off, page_ind_fill[1]);
  ready = true;
}

void ScreenUpdate(void* scr, int dt) {
  if (ready) {
    csclouds::ScreenInfo s;
    csclouds::GetScreenInfo(&s);
    const int32_t* v = s.values;
    const bool page2 = s.page == 1;
    SetVisible(nodes[N_PAGE1], !page2);
    SetVisible(nodes[N_PAGE2], page2);

    SetTranslate(nodes[N_POSITION], (v[0] - 16384) * 5, 0);
    SetTranslate(nodes[N_PITCH], 0, -(s.pitch_q16 >> 7));
    ShowOne(size_nodes, 6, Steps(v[1], 5));
    const int32_t d = v[3] - 16384;
    const int32_t n = ((d < 0 ? -d : d) * 8 + 8192) / 16384;
    ShowFirst(reg_nodes, 8, d < 0 ? n : 0);
    ShowFirst(rnd_nodes, 8, d > 0 ? n : 0);
    SetVisible(nodes[N_FACE], !s.freeze);
    SetVisible(nodes[N_FACE_FROZEN], s.freeze);

    ShowOne(texture_nodes, 5, Steps(v[4], 4));
    ShowOne(wet_nodes, 5, v[5] == 0 ? -1 : Steps(v[5], 4));
    ShowFirst(fb_nodes, 5, Steps(v[6], 5));
    ShowFirst(rv_nodes, 5, Steps(v[7], 5));

    for (int32_t i = 0; i < 4; ++i) {
      const bool lit = s.mode == i;
      WriteColour(mode_line[i], lit ? mode_line_on
                                    : (i == op1clouds::MODE_SPECTRAL ? mode_line_disabled : mode_line_off));
      WriteColour(mode_fill[i], lit ? mode_fill_on : mode_fill_off);
    }
    SetVisible(nodes[N_FREEZE_OFF], !s.freeze);
    SetVisible(nodes[N_FREEZE_ON], s.freeze);
    WriteColour(page_ind_line[0], page2 ? ind_line_off : ind_line_on);
    WriteColour(page_ind_fill[0], page2 ? ind_fill_off : ind_fill_on);
    WriteColour(page_ind_line[1], page2 ? ind_line_on : ind_line_off);
    WriteColour(page_ind_fill[1], page2 ? ind_fill_on : ind_fill_off);
  }
  op1_screen_draw(scr, dt);
}

}  // namespace

extern "C" const op1_screen_t CS_SCREEN_SYMBOL = { ScreenInit, ScreenUpdate };
