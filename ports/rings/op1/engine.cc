// Rings engine for the OP-1: replaces the phase engine's render and note-on
// (vtable +0x14, +0x1c). Six OP-1 voices, one RingsVoice each; the OP-1 core
// allocates its voices and applies its envelope after render. The header's
// patch table (op1/patches.cc) also puts the Rings screen in place of the
// phase screen (op1/screen.cc) and takes the encoder clicks (op1/input.cc).
//
// The OP-1 renders voices at 88.2 kHz, 128 samples per call. Rings runs at
// half that (44.1 kHz, 64 samples per call; upstream runs at 48 kHz) and is
// upsampled 2x, which halves its CPU cost.
//
// Encoders (params[0..3], 0..32767): blue = structure, green = brightness,
// white = damping, orange = position (confirmed on the device). They're
// smoothed per voice over ~50 ms, as upstream smooths its pots
// (op1/param_smoother.h).
//
// Model and polyphony come from the slot's knobs[4] and knobs[5]
// (op1/settings.h), latched at note-on: a change applies from the next note,
// so it never clicks a ringing voice. 0 (every existing preset) is the modal
// model with polyphony "all", the original behaviour: 6 voices x 16 modes
// (probe v3 measured ~324 MHz free; six ringing voices simulate at ~164 MHz).
// Polyphony 1, 2 and 4 trade voices for modes as Rings does
// (op1/voice_allocator.h).

#include <cstdint>

#include "op1/op1.h"
#include "op1/param_smoother.h"
#include "op1/patches.h"
#include "op1/settings.h"
#include "op1/voice_allocator.h"
#include "src/fixed.h"
#include "src/op1_voice.h"

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
op1::VoiceAllocator allocator;
bool initialized;
int32_t out_buffer[kRingsBlockSize];

void InitAll() {
  for (int32_t v = 0; v < op1::kNumVoices; ++v) {
    voices[v].Init(op1::kPolyModes[op1::POLY_ALL]);
    upsamplers[v].Init();
    smoothers[v].Init();
  }
  allocator.Init();
  initialized = true;
}

inline int32_t ParamToQ31(int16_t p) {
  return p <= 0 ? 0 : static_cast<int32_t>(p) << 16;
}

// The slot's knobs[8], from note-on's R1 if it points into the knob array,
// else from the slot the core plays. Null if neither is usable.
const int16_t* SettingsKnobs(const int16_t* knobs) {
#ifdef OP1_SIM
  return knobs;   // tests/sim_engine.cc passes its own array
#else
  const uint32_t p = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(knobs));
  if (p >= op1::kKnobs && p < op1::kKnobs + op1::kNumSlots * 16 && ((p - op1::kKnobs) & 15) == 0) {
    return knobs;
  }
  const int32_t slot = *reinterpret_cast<const volatile int32_t*>(op1::kCoreSlot);
  if (slot >= 0 && slot < op1::kNumSlots) {
    return reinterpret_cast<const int16_t*>(op1::kKnobs + slot * 16);
  }
  return nullptr;
#endif
}

// The models the voice implements; the others play modal for now.
op1rings::ResonatorModel VoiceModel(int32_t setting) {
  return setting == op1rings::MODEL_FM_VOICE ? op1rings::MODEL_FM_VOICE : op1rings::MODEL_MODAL;
}

}  // namespace

extern "C" void RingsRender(void* self, int16_t* out, const int16_t* params) {
  if (!initialized) InitAll();
  const int32_t v = op1::VoiceIndex(self);
  if (allocator.silent(v)) {
    // Its note was taken over by another voice and has faded out.
    for (int32_t i = 0; i < op1::kBlockSize; ++i) out[i] = 0;
    return;
  }
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
  allocator.Rendered(v);
  if (allocator.fading(v)) {
    // Linear fade-out (Q15 gain) of a voice whose note was taken over.
    int32_t gain = allocator.fade_gain(v);
    const int32_t step = allocator.fade_step(v);
    for (int32_t i = 0; i < op1::kBlockSize; ++i) {
      out[i] = static_cast<int16_t>((out[i] * (gain > 0 ? gain : 0)) >> 15);
      gain -= step;
    }
    allocator.AdvanceFade(v, op1::kBlockSize);
  }
}

extern "C" void RingsNoteOn(void* self, const int16_t* knobs) {
  if (!initialized) InitAll();
  const int32_t v = op1::VoiceIndex(self);
  const int16_t* settings = SettingsKnobs(knobs);
  const int32_t model = settings ? op1::ModelSetting(settings) : 0;
  const op1::PolyState poly = settings ? op1::PolySetting(settings) : op1::POLY_ALL;

  const op1::VoiceAllocator::NoteOnAction action = allocator.NoteOn(v, poly);
  if (action.copy_from >= 0) {
    // Take over the stolen voice's ringing state; it fades out on its own.
    voices[v] = voices[action.copy_from];
    upsamplers[v] = upsamplers[action.copy_from];
  } else if (action.reset) {
    voices[v].Init(op1::kPolyModes[poly]);
    upsamplers[v].Init();
  }
  voices[v].set_resolution(op1::kPolyModes[poly]);
  voices[v].set_model(VoiceModel(model));
  voices[v].NoteOn();
  smoothers[v].Snap();
}

extern "C" __attribute__((section(".op1header"), used))
const op1::Header op1_header = {
  op1::kHeaderMagic, 3, &RingsRender, &RingsNoteOn, nullptr,
  op1::kPatches, op1::kNumPatches, 0,
};
