// Rings engine for the OP-1: replaces the phase engine's render and note-on
// (vtable +0x14, +0x1c). Six voices, one RingsVoice each; the OP-1 core
// allocates voices and applies its envelope after render.
//
// Encoders (params[0..3], assumed 0..32767): structure, brightness, damping,
// position. The order is a guess to check on the device; see kParamOrder.

#include <cstdint>

#include "op1/op1.h"
#include "src/fixed.h"
#include "src/op1_voice.h"

#ifndef OP1RINGS_MODES_PER_VOICE
#define OP1RINGS_MODES_PER_VOICE 10
#endif

namespace {

using op1rings::RingsVoice;
using op1rings::VoicePatch;

// params[] index for structure, brightness, damping, position.
const int32_t kParamOrder[4] = { 0, 1, 2, 3 };

RingsVoice voices[op1::kNumVoices];
bool initialized;
int32_t out_buffer[op1::kBlockSize];

void InitAll() {
  for (int32_t v = 0; v < op1::kNumVoices; ++v) {
    voices[v].Init(OP1RINGS_MODES_PER_VOICE);
  }
  initialized = true;
}

inline int32_t ParamToQ31(int16_t p) {
  return p <= 0 ? 0 : static_cast<int32_t>(p) << 16;
}

}  // namespace

extern "C" void RingsRender(void* self, int16_t* out, const int16_t* params) {
  if (!initialized) InitAll();
  RingsVoice& voice = voices[op1::VoiceIndex(self)];
  VoicePatch patch;
  patch.structure = ParamToQ31(params[kParamOrder[0]]);
  patch.brightness = ParamToQ31(params[kParamOrder[1]]);
  patch.damping = ParamToQ31(params[kParamOrder[2]]);
  patch.position = ParamToQ31(params[kParamOrder[3]]);
  voice.RenderMono(op1::PitchBuffer(), patch, out_buffer, op1::kBlockSize);
  // Q27 (limited to about +-1) -> 16 bit.
  for (int32_t i = 0; i < op1::kBlockSize; ++i) {
    out[i] = op1::Saturate16(out_buffer[i] >> 12);
  }
}

extern "C" void RingsNoteOn(void* self) {
  if (!initialized) InitAll();
  voices[op1::VoiceIndex(self)].NoteOn();
}

extern "C" __attribute__((section(".op1header"), used))
const op1::Header op1_header = {
  op1::kHeaderMagic, 1, &RingsRender, &RingsNoteOn, nullptr, { 0, 0, 0 },
};
