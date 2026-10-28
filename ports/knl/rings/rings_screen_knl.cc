// Mutable Instruments Rings as a kennel synth plugin, id rings: the
// descriptor and the screen. The engine itself is the firmware build's
// (ports/rings/op1/engine.cc with -DOP1_KENNEL: the same render and note-on
// the phase voice's vtable called, now kennel's op1_engine_t in the iter
// slot; 22.05 kHz, upsampled 4x, Test 51).
//
// Knobs: blue structure, green brightness, white damping, orange position.
// Presses: blue -> page 2, where blue picks the model (word 5, six steps:
// modal, sympathetic strings, string, FM, quantized sympathetic strings,
// string + reverb); green -> page 2, where green picks the polyphony (word
// 6, four steps: all, 1, 2, 4), one step per encoder click (settings.h).
//
// The screen (gen_rings_knl_svg.py, the canvas "Rings kennel screen"):
//   ring0..ring5   lit from the core out by structure (1..6)
//   ray0..ray7     light: lit by brightness (0..8), flashing white with
//                  the loudest voice
//   pad            pressure: drawn deep in, lifts off the rings as damping rises
//   pos            the strike dot, out along the radius with position
//   rip0..rip5     one ripple per voice: scaled from the strike outwards
//                  with the voice's age, faded with its level (engine.cc
//                  RingsScreenVoices), hidden when quiet or past the rim
//   core           the glow: brightness sets its rest, the voices flare it
//   mlabelN, modelN, mbox / plabelN, voiceN, pbox   the MODEL and POLY rows;
//                  a bracket and a white label while the knob is on page 2
//                  (op1_eng_page), the page-1 element of that knob dimmed.

#include "common/knl_fx.h"
#include "fx/op1/fx_screen.h"
#include "op1/settings.h"

extern "C" void RingsRender(void* self, int16_t* out, const int16_t* params);
extern "C" void RingsNoteOn(void* self, const int16_t* knobs);
extern "C" void RingsNoteOff(void* self);
extern "C" void RingsScreenVoices(int16_t* level, uint16_t* age);

namespace {

using namespace op1fx::screen;

const int32_t kVoices = 6;
const int32_t kRays = 8;
const int32_t kCX = 104, kCY = 72;
const int32_t kRippleDrawn = 30;      // the ripple circle's radius in the file
const int32_t kRippleEnd = 58;        // hidden past this radius
const int32_t kRippleTravel = 54;     // the core to the rim
const int32_t kLevelThreshold = 160;  // a voice below this (peak, int16) is quiet

const char* const kRingIds[6] = { "ring0", "ring1", "ring2", "ring3", "ring4", "ring5" };
const char* const kRayIds[kRays] = { "ray0", "ray1", "ray2", "ray3", "ray4", "ray5", "ray6", "ray7" };
const char* const kRippleIds[kVoices] = { "rip0", "rip1", "rip2", "rip3", "rip4", "rip5" };
const char* const kModelIds[6] = { "model0", "model1", "model2", "model3", "model4", "model5" };
const char* const kModelLabelIds[6] = { "mlabel0", "mlabel1", "mlabel2", "mlabel3", "mlabel4", "mlabel5" };
const char* const kVoiceIds[6] = { "voice0", "voice1", "voice2", "voice3", "voice4", "voice5" };
const char* const kPolyLabelIds[4] = { "plabel0", "plabel1", "plabel2", "plabel3" };
const char* const kNodeIds[5] = { "pad", "pos", "core", "mbox", "pbox" };
const char* const kPaletteIds[7] = { "palwhite", "paldim", "palblue", "palgreen", "paldimgreen", "palgrey", "palblack" };
enum { N_PAD, N_POS, N_CORE, N_MBOX, N_PBOX };
enum { C_WHITE, C_DIM, C_BLUE, C_GREEN, C_DIMGREEN, C_GREY, C_BLACK };

bool ready;
uint8_t* ring_styles[6];
uint8_t* ray_styles[kRays];
uint8_t* ripple_styles[kVoices];
uint8_t* model_line_styles[6];
uint8_t* model_fill_styles[6];
uint8_t* model_label_styles[6];
uint8_t* voice_styles[6];
uint8_t* poly_label_styles[4];
uint8_t* core_fill;
void* ripple_nodes[kVoices];
void* model_label_nodes[6];
void* poly_label_nodes[4];
void* nodes[5];
Colour pal[7];

// From a to b by v (0..32767), each byte on its own.
Colour Blend(const Colour& a, const Colour& b, int32_t v) {
  Colour c;
  for (int32_t i = 0; i < 4; ++i) c.rgba[i] = static_cast<uint8_t>(a.rgba[i] + (((b.rgba[i] - a.rgba[i]) * v) >> 15));
  return c;
}

inline int32_t Knob(int16_t k) { return k < 0 ? 0 : k; }
inline int32_t Clamp15(int32_t v) { return v > 32767 ? 32767 : v; }

// Scale by s (Q10) about the centre.
void ScaleAbout(void* node, int32_t s) { SetMatrix(node, s, s, kCX * (0x400 - s), kCY * (0x400 - s)); }

void ScreenInit(void* scr) {
  ready = false;
  void* document = DocumentOf(scr);
  if (!document) return;
  void* tmp[kRays];
  if (!FindAll(document, kRingIds, tmp, 6) || !FindAll(document, kRayIds, tmp, kRays) ||
      !FindAll(document, kRippleIds, ripple_nodes, kVoices) || !FindAll(document, kModelIds, tmp, 6) ||
      !FindAll(document, kModelLabelIds, model_label_nodes, 6) || !FindAll(document, kVoiceIds, tmp, 6) ||
      !FindAll(document, kPolyLabelIds, poly_label_nodes, 4) || !FindAll(document, kNodeIds, nodes, 5) ||
      !FindAll(document, kPaletteIds, tmp, 7)) {
    return;
  }
  for (int32_t i = 0; i < 7; ++i) ReadColour(&pal[i], LineStyle(scr, kPaletteIds[i]));
  for (int32_t i = 0; i < 6; ++i) {
    ring_styles[i] = LineStyle(scr, kRingIds[i]);
    model_line_styles[i] = LineStyle(scr, kModelIds[i]);
    model_fill_styles[i] = FillStyle(scr, kModelIds[i]);
    model_label_styles[i] = LineStyle(scr, kModelLabelIds[i]);
    voice_styles[i] = LineStyle(scr, kVoiceIds[i]);
    ripple_styles[i] = LineStyle(scr, kRippleIds[i]);
  }
  for (int32_t i = 0; i < kRays; ++i) ray_styles[i] = LineStyle(scr, kRayIds[i]);
  for (int32_t i = 0; i < 4; ++i) poly_label_styles[i] = LineStyle(scr, kPolyLabelIds[i]);
  core_fill = FillStyle(scr, "core");
  ready = true;
}

void ScreenUpdate(void* scr, int dt) {
  if (ready) {
    const int32_t slot = OP1_SCREEN_SLOT;
    int16_t knobs[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    if (slot >= 0 && slot < 8) {
      op1_synth_knobs_live(slot, knobs);   // 0..3 after the LFO
      const int16_t* stored = op1_synth_knobs(slot);
      for (int32_t k = 4; k < 8; ++k) knobs[k] = stored[k];
    }
    const int32_t structure = Knob(knobs[0]);
    const int32_t brightness = Knob(knobs[1]);
    const int32_t damping = Knob(knobs[2]);
    const int32_t position = Knob(knobs[3]);
    const bool blue_page = op1_eng_page[0] != 0;
    const bool green_page = op1_eng_page[1] != 0;

    int16_t level[kVoices];
    uint16_t age[kVoices];
    RingsScreenVoices(level, age);
    int32_t loudest = 0, sum = 0, sounding = 0;
    for (int32_t v = 0; v < kVoices; ++v) {
      const int32_t l = level[v] < 0 ? 0 : level[v];
      if (l > loudest) loudest = l;
      sum += l;
      if (l >= kLevelThreshold) ++sounding;
    }
    const int32_t loud15 = Clamp15(loudest * 4);   // a voice at a quarter of full scale is "full"
    const int32_t sum15 = Clamp15(sum * 4);

    // Structure: the rings, lit from the core; all dim while blue is on page 2.
    const int32_t lit = 1 + ((structure * 5 + 16384) >> 15);
    for (int32_t i = 0; i < 6; ++i) WriteColour(ring_styles[i], i < lit && !blue_page ? pal[C_BLUE] : pal[C_DIM]);

    // Brightness: the rays (0..8 lit), flashing towards white with the loudest voice.
    const int32_t rays = (brightness * kRays + 16383) / 32767;
    const Colour ray_lit = Blend(pal[C_GREEN], pal[C_WHITE], loud15);
    for (int32_t i = 0; i < kRays; ++i) WriteColour(ray_styles[i], i < rays && !green_page ? ray_lit : pal[C_DIMGREEN]);

    // Damping: the pad is drawn deep in and lifts 25.5 px out along the
    // diagonal as damping rises (Q10: 25.5 x 1024 / 32767 = 0.797; a
    // clockwise turn lifts the damper off, and in Rings more damping is a
    // longer ring).
    const int32_t pad = (damping * 816) >> 10;
    SetTranslate(nodes[N_PAD], pad, pad);
    // Position: the strike dot, 48 px out along the radius (Q10: 1.5).
    SetTranslate(nodes[N_POS], (position * 1536) >> 10, 0);

    // The core's glow: brightness sets its rest, the voices flare it.
    WriteColour(core_fill, Blend(Blend(pal[C_BLACK], pal[C_GREY], brightness), pal[C_WHITE], sum15));

    // The ripples: r = 4 + age x 54 / 256 (the rim at 0.37 s), faded with
    // the voice and with the distance left to the edge.
    for (int32_t v = 0; v < kVoices; ++v) {
      const int32_t l = level[v] < 0 ? 0 : level[v];
      const int32_t r = 4 + ((age[v] > 255 ? 255 : age[v]) * kRippleTravel) / 256;
      const bool shown = l >= kLevelThreshold && r < kRippleEnd;
      SetVisible(ripple_nodes[v], shown);
      if (!shown) continue;
      ScaleAbout(ripple_nodes[v], (r * 0x400) / kRippleDrawn);
      const int32_t k = (Clamp15(l * 4) * (kRippleEnd - r)) / kRippleTravel;
      WriteColour(ripple_styles[v], Blend(pal[C_BLACK], pal[C_WHITE], k));
    }

    // MODEL row.
    const int32_t model = op1::ModelSetting(knobs);
    ShowOne(model_label_nodes, 6, model);
    WriteColour(model_label_styles[model], blue_page ? pal[C_WHITE] : pal[C_BLUE]);
    for (int32_t i = 0; i < 6; ++i) {
      const bool on = i == model;
      WriteColour(model_line_styles[i], on || blue_page ? pal[C_BLUE] : pal[C_DIM]);
      WriteColour(model_fill_styles[i], on ? pal[C_BLUE] : pal[C_BLACK]);
    }
    SetVisible(nodes[N_MBOX], blue_page);

    // POLY row: a tick per voice the setting allows, bright green while that
    // many voices sound (one per sounding voice, from the left), dim green at
    // rest, near black beyond the setting. On page 2 the setting itself: the
    // allowed ticks bright, the rest grey.
    const int32_t poly = op1::PolySetting(knobs);
    const int32_t voices = op1::kPolyVoices[poly];
    ShowOne(poly_label_nodes, 4, poly);
    WriteColour(poly_label_styles[poly], green_page ? pal[C_WHITE] : pal[C_GREEN]);
    for (int32_t i = 0; i < 6; ++i) {
      const Colour& c = green_page ? (i < voices ? pal[C_GREEN] : pal[C_GREY])
                        : i < voices ? (i < sounding ? pal[C_GREEN] : pal[C_DIMGREEN])
                                     : pal[C_DIM];
      WriteColour(voice_styles[i], c);
    }
    SetVisible(nodes[N_PBOX], green_page);
  }
  op1_screen_draw(scr, dt);
}

}  // namespace

extern "C" const op1_engine_t ENGINE_rings = {
  RingsNoteOn, RingsRender, RingsNoteOff,   // note_off only tells the screen (the stock envelope does the release)
  { OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_NONE, OP1_PRESS_NONE },
};

extern "C" const op1_screen_t SCREEN_rings = { ScreenInit, ScreenUpdate };
