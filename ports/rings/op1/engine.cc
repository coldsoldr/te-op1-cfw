// Rings engine for the OP-1: replaces the phase engine's render and note-on
// (vtable +0x14, +0x1c). Six voices, one RingsVoice each; the OP-1 core
// allocates voices and applies its envelope after render.
//
// The OP-1 renders voices at 88.2 kHz, 128 samples per call. Rings runs at
// half that (44.1 kHz, 64 samples per call; upstream runs at 48 kHz) and is
// upsampled 2x, which halves its CPU cost.
//
// Encoders (params[0..3], 0..32767): blue = structure, green = brightness,
// white = damping, orange = position (confirmed on the device). They're
// smoothed per voice over ~50 ms, as upstream smooths its pots
// (op1/param_smoother.h).

#include <cstdint>

#include "op1/op1.h"
#include "op1/param_smoother.h"
#include "src/fixed.h"
#include "src/op1_voice.h"

// Probe v3 measured ~324 MHz free with one voice playing. At 16 modes, six
// ringing voices simulate at ~164 MHz (more when knobs move), leaving room
// for other tracks, FX and the UI. Rings itself uses 12 modes per voice at
// polyphony 4.
#ifndef OP1RINGS_MODES_PER_VOICE
#define OP1RINGS_MODES_PER_VOICE 16
#endif

namespace {

using op1rings::RingsVoice;
using op1rings::VoicePatch;

const int32_t kRingsBlockSize = op1::kBlockSize / 2;

// params[] index for structure, brightness, damping, position.
const int32_t kParamOrder[4] = { 0, 1, 2, 3 };

// 2x upsampler. Even outputs are the input samples; odd outputs are the
// 4-point Lagrange midpoint (-x[-1] + 9 x[0] + 9 x[1] - x[2]) / 16, which is
// -0.4 dB at 10 kHz and -1.6 dB at 15 kHz. Delays the signal by 2 input
// samples (45 us). Works on 16-bit-scaled samples, so the sums can't
// overflow.
class Upsampler {
 public:
  void Init() { history_[0] = history_[1] = history_[2] = 0; }

  // in: kRingsBlockSize Q27 samples; out: 2 * kRingsBlockSize samples.
  void Process(const int32_t* in, int16_t* out) {
    int32_t* x = extended_;
    x[0] = history_[0];
    x[1] = history_[1];
    x[2] = history_[2];
    for (int32_t i = 0; i < kRingsBlockSize; ++i) x[i + 3] = in[i] >> 12;
    for (int32_t k = 0; k < kRingsBlockSize; ++k) {
      out[2 * k] = op1::Saturate16(x[k + 1]);
      out[2 * k + 1] = op1::Saturate16((9 * (x[k + 1] + x[k + 2]) - x[k] - x[k + 3]) >> 4);
    }
    history_[0] = x[kRingsBlockSize];
    history_[1] = x[kRingsBlockSize + 1];
    history_[2] = x[kRingsBlockSize + 2];
  }

 private:
  int32_t history_[3];   // last three inputs of the previous block
  static int32_t extended_[kRingsBlockSize + 3];   // scratch, kept off the stack
};

int32_t Upsampler::extended_[kRingsBlockSize + 3];

RingsVoice voices[op1::kNumVoices];
Upsampler upsamplers[op1::kNumVoices];
op1::ParamSmoother smoothers[op1::kNumVoices];
bool initialized;
int32_t out_buffer[kRingsBlockSize];

void InitAll() {
  for (int32_t v = 0; v < op1::kNumVoices; ++v) {
    voices[v].Init(OP1RINGS_MODES_PER_VOICE);
    upsamplers[v].Init();
    smoothers[v].Init();
  }
  initialized = true;
}

inline int32_t ParamToQ31(int16_t p) {
  return p <= 0 ? 0 : static_cast<int32_t>(p) << 16;
}

}  // namespace

extern "C" void RingsRender(void* self, int16_t* out, const int16_t* params) {
  if (!initialized) InitAll();
  const int32_t v = op1::VoiceIndex(self);
  int32_t target[op1::ParamSmoother::kNumParams];
  for (int32_t i = 0; i < op1::ParamSmoother::kNumParams; ++i) {
    target[i] = ParamToQ31(params[kParamOrder[i]]);
  }
  const int32_t* smoothed = smoothers[v].Process(target);
  VoicePatch patch;
  patch.structure = smoothed[0];
  patch.brightness = smoothed[1];
  patch.damping = smoothed[2];
  patch.position = smoothed[3];
  // The OP-1's increment is f / 88200 * 2^32; at half the rate it doubles.
  // Pitch is latched at the start of the block (as the stock string engine
  // does).
  const uint32_t inc = op1::PitchBuffer()[0];
  const uint32_t rings_inc = inc < 0x80000000u ? inc << 1 : 0xffffffffu;
  voices[v].RenderMono(&rings_inc, patch, out_buffer, kRingsBlockSize);
  upsamplers[v].Process(out_buffer, out);
}

extern "C" void RingsNoteOn(void* self) {
  if (!initialized) InitAll();
  const int32_t v = op1::VoiceIndex(self);
  voices[v].NoteOn();
  smoothers[v].Snap();
}

extern "C" __attribute__((section(".op1header"), used))
const op1::Header op1_header = {
  op1::kHeaderMagic, 2, &RingsRender, &RingsNoteOn, nullptr, { 0, 0, 0 },
};
