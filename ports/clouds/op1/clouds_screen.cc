// Clouds' screen on the OP-1, as an engine of the reverb host
// (../fx/op1/fx_host.cc): the host swaps "clouds.svg" (content/display/
// clouds.svg, made by ../fx/op1/gen_fx_svg.py from the design,
// docs/module-briefs/designs/clouds.svg) into spring's Screen object for a
// position that plays Clouds, the way the MULTI screen swaps pd.svg and
// rings.svg (ports/rings/op1/screen.cc):
//   - ScreenInit runs once at boot with the screen's document = clouds.svg
//     and finds its elements;
//   - ScreenUpdate sets them each frame (the host then draws).
//
// The mappings are the design's (docs/module-briefs/designs/clouds.md). The
// top strip is the fx screens' (plate, hall): engine dots, page squares,
// freeze star. Colours are read from the SVG's own elements in their
// default state (mode0 lit, mode1 not, mode3 disabled; page 1 lit, page 2
// not; clouds' engine dot lit).

#include <cstdint>

#include "fx/op1/fx_screen.h"
#include "op1/clouds_fx.h"
#include "src/core/params.h"

namespace {

using op1fx::screen::Colour;
using op1fx::screen::DocumentOf;
using op1fx::screen::EngineDots;
using op1fx::screen::Find;
using op1fx::screen::FindAll;
using op1fx::screen::FillStyle;
using op1fx::screen::LineStyle;
using op1fx::screen::ReadColour;
using op1fx::screen::SetTranslate;
using op1fx::screen::SetVisible;
using op1fx::screen::ShowFirst;
using op1fx::screen::ShowOne;
using op1fx::screen::Steps;
using op1fx::screen::WriteColour;

// Element ids (docs/module-briefs/designs/clouds.md, "Driven ids"). The CPU
// meter (cpu, cpu0..cpu5) stays hidden as the file has it: not driven.
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
EngineDots dots;
uint8_t* mode_line[4];
uint8_t* mode_fill[4];
uint8_t* page_ind_line[2];
uint8_t* page_ind_fill[2];
Colour mode_line_on, mode_line_off, mode_fill_on, mode_fill_off, mode_line_disabled;
Colour ind_line_on, ind_line_off, ind_fill_on, ind_fill_off;

// Our elements, with self's document = clouds.svg.
void CloudsInit(void* self) {
  ready = false;
  void* document = DocumentOf(self);
  if (!document) return;
  // GetLineStyle asserts on a missing id: check them all first, and leave
  // the screen static rather than crash if the SVG is wrong.
  if (!FindAll(document, kNodeIds, nodes, N_COUNT) || !FindAll(document, kSizeIds, size_nodes, 6) ||
      !FindAll(document, kRegIds, reg_nodes, 8) || !FindAll(document, kRndIds, rnd_nodes, 8) ||
      !FindAll(document, kTextureIds, texture_nodes, 5) ||
      !FindAll(document, kWetIds, wet_nodes, 5) || !FindAll(document, kFbIds, fb_nodes, 5) ||
      !FindAll(document, kRvIds, rv_nodes, 5) || !FindAll(document, kModeIds, mode_nodes, 4) ||
      !dots.Init(self, 4, op1clouds::kCloudsEngineId)) {
    return;
  }
  for (int32_t i = 0; i < 4; ++i) {
    mode_line[i] = LineStyle(self, kModeIds[i]);
    mode_fill[i] = FillStyle(self, kModeIds[i]);
  }
  page_ind_line[0] = LineStyle(self, "page1ind");
  page_ind_line[1] = LineStyle(self, "page2ind");
  page_ind_fill[0] = FillStyle(self, "page1ind");
  page_ind_fill[1] = FillStyle(self, "page2ind");   // #010101 (gen_fx_svg.py)

  // The file's defaults: mode0 lit, mode1 not, mode3 disabled (its line
  // only); page 1 lit, page 2 not.
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

void CloudsUpdate(void* self, const op1clouds::ScreenInfo& s) {
  if (ready) {
    const int32_t* v = s.values;
    const bool page2 = s.page == 1;
    SetVisible(nodes[N_PAGE1], !page2);
    SetVisible(nodes[N_PAGE2], page2);

    // Page 1. Position: x = (pos - 0.5) x 160 px; pitch: y = -semitones x
    // 0.5 px (Q10 translates: 160 x 1024 / 32767 = 5, 0.5 x 1024 / 65536 =
    // 1 / 128).
    SetTranslate(nodes[N_POSITION], (v[0] - 16384) * 5, 0);
    SetTranslate(nodes[N_PITCH], 0, -(s.pitch_q16 >> 7));
    ShowOne(size_nodes, 6, Steps(v[1], 5));
    // Density, bipolar: n = round(|d| x 8) drops, regular left, random right.
    const int32_t d = v[3] - 16384;
    const int32_t n = ((d < 0 ? -d : d) * 8 + 8192) / 16384;
    ShowFirst(reg_nodes, 8, d < 0 ? n : 0);
    ShowFirst(rnd_nodes, 8, d > 0 ? n : 0);
    SetVisible(nodes[N_FACE], !s.freeze);
    SetVisible(nodes[N_FACE_FROZEN], s.freeze);

    // Page 2.
    ShowOne(texture_nodes, 5, Steps(v[4], 4));
    ShowOne(wet_nodes, 5, v[5] == 0 ? -1 : Steps(v[5], 4));
    ShowFirst(fb_nodes, 5, Steps(v[6], 5));
    ShowFirst(rv_nodes, 5, Steps(v[7], 5));

    // Both pages: the top strip (engine dots, page squares, freeze star)
    // and the mode dots; spectral's dot is dimmer while not selected.
    dots.Show(op1fx::EngineDot(op1clouds::kCloudsEngineId));
    for (int32_t i = 0; i < 4; ++i) {
      const bool lit = s.mode == i;
      WriteColour(mode_line[i], lit ? mode_line_on
                                    : (i == op1clouds::MODE_SPECTRAL ? mode_line_disabled
                                                                     : mode_line_off));
      WriteColour(mode_fill[i], lit ? mode_fill_on : mode_fill_off);
    }
    SetVisible(nodes[N_FREEZE_OFF], !s.freeze);
    SetVisible(nodes[N_FREEZE_ON], s.freeze);
    WriteColour(page_ind_line[0], page2 ? ind_line_off : ind_line_on);
    WriteColour(page_ind_fill[0], page2 ? ind_fill_off : ind_fill_on);
    WriteColour(page_ind_line[1], page2 ? ind_line_on : ind_line_off);
    WriteColour(page_ind_fill[1], page2 ? ind_fill_on : ind_fill_off);
  }
}

}  // namespace

namespace op1clouds {

void ScreenInit(void* self) { CloudsInit(self); }

void ScreenUpdate(void* self, int32_t screen_position, const int16_t* encoders,
                  const int16_t* values, const op1fx::EncoderRange* ranges) {
  ScreenInfo info;
  GetScreenInfo(screen_position, encoders, values, ranges, &info);
  CloudsUpdate(self, info);
}

}  // namespace op1clouds
