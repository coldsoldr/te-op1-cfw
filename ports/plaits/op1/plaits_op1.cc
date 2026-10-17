// The Plaits voice for the OP-1 MULTI engine (op1/plaits_op1.h).

#include "op1/plaits_op1.h"

#include "../../rings/op1/op1.h"
#include "../../rings/op1/param_smoother.h"
#include "fixed/voice.h"

namespace op1plaits {

namespace {

const int kCallSize = op1::kBlockSize / 2;   // 64 samples at 44.1 kHz
static_assert(kCallSize % PLAITS_BLOCK_SIZE == 0, "whole Plaits blocks per OP-1 call");

// log2(55 / (4 * 44100)) in Q24: NoteToFrequency(note) = a0 / 4 *
// 2^((note - 9) / 12) with a0 = 55 / 44100.
const int32_t kLog2QuarterA0 = -195406438;

// 2x upsampler on 16-bit samples: even outputs are the inputs, odd ones the
// 4-point Lagrange midpoint (-x[-1] + 9 x[0] + 9 x[1] - x[2]) / 16 (as Rings'
// op1/engine.cc). 2 input samples of latency.
class Upsampler {
 public:
  void Init() { h_[0] = h_[1] = h_[2] = 0; }
  void Process(const int32_t* in, int16_t* out, int n) {
    int32_t x0 = h_[0], x1 = h_[1], x2 = h_[2];
    for (int k = 0; k < n; ++k) {
      const int32_t x3 = in[k];
      out[2 * k] = op1::Saturate16(x1);
      out[2 * k + 1] = op1::Saturate16((9 * (x1 + x2) - x0 - x3) >> 4);
      x0 = x1;
      x1 = x2;
      x2 = x3;
    }
    h_[0] = x0;
    h_[1] = x1;
    h_[2] = x2;
  }
 private:
  int32_t h_[3];
};

struct PlaitsVoiceState {
  VoiceFixed voice;
  Upsampler upsampler;
  op1::ParamSmoother smoother;
  int model;
  int options;
  bool retrigger;
};

// All in the image's .bss (SDRAM): trivially constructible, zeroed at load;
// InitAll() runs on the first call (the image has no startup code).
PlaitsVoiceState states[op1::kNumVoices];
bool initialized;
int16_t frames[2 * kCallSize];
int32_t mono[kCallSize];

void InitAll() {
  for (int v = 0; v < op1::kNumVoices; ++v) {
    PlaitsVoiceState& s = states[v];
    s.voice.Init();
    s.voice.set_trigger_delay(1);   // Read(1): the latest block, no delay
    s.upsampler.Init();
    s.smoother.Init();
    s.model = 0;
    s.options = 0;
    s.retrigger = false;
  }
  initialized = true;
}

}  // namespace

int32_t IncrementToNote(uint32_t increment) {
  // The internal rate is half the OP-1's: f_44k = 2 inc (Q32).
  if (increment == 0) return -(119 << 16);
  const uint32_t f = increment >= (1u << 31) ? 0xffffffffu : increment << 1;
  const int n = CountLeadingZeros(f);
  const uint32_t x = f << n;   // [2^31, 2^32): f = x 2^-n
  // log2(f / 2^32) = log2(x / 2^31) - 1 - n, with x / 2^31 = 1 + m.
  const uint32_t m = x << 1;   // the mantissa's fraction, Q32
  const uint32_t i = m >> 22;
  const int32_t frac = static_cast<int32_t>((m >> 6) & 0xffff) << 15;   // Q31
  const int32_t l = lut_log2_q30[i] + Mul(lut_log2_q30[i + 1] - lut_log2_q30[i], frac);   // Q30
  const int32_t log2_f = (l >> 6) - ((1 + n) << 24);   // Q24
  // note = 9 + 12 (log2 f - log2(a0 / 4)), Q16.
  return (9 << 16) + ((log2_f - kLog2QuarterA0) * 3 >> 6);   // 12 / 256 = 3 / 64
}

}  // namespace op1plaits

using op1plaits::states;

extern "C" void PlaitsRender(void* self, int16_t* out, const int16_t* params) {
  using namespace op1plaits;
  if (!initialized) InitAll();
  const int32_t v = op1::VoiceIndex(self);
  PlaitsVoiceState& s = states[v];

  int32_t target[op1::ParamSmoother::kNumParams];
  for (int i = 0; i < op1::ParamSmoother::kNumParams; ++i) {
    target[i] = params[i] <= 0 ? 0 : static_cast<int32_t>(params[i]) << 16;
  }
  const int32_t* smoothed = s.smoother.Process(target);

  VoicePatch patch;
  patch.note = IncrementToNote(op1::PitchBuffer()[0]);
  patch.harmonics = smoothed[0] >> 16;   // Q15
  patch.timbre = smoothed[1] >> 16;
  patch.morph = smoothed[2] >> 16;
  patch.decay = smoothed[3] >> 16;
  patch.frequency_modulation_amount = 0;
  patch.timbre_modulation_amount = 0;
  patch.morph_modulation_amount = 0;
  patch.engine = s.model;
  patch.lpg_colour = kQ15One / 2;
  patch.lpg_off = (s.options & kOptionLpgOff) != 0;

  VoiceModulations m;
  m.note = 0;
  m.frequency = 0;
  m.harmonics = 0;
  m.timbre = 0;
  m.morph = 0;
  m.level = 0;
  m.frequency_patched = false;
  m.timbre_patched = false;
  m.morph_patched = false;
  m.trigger_patched = true;   // note-ons trigger the engines (and ping the LPG)
  m.level_patched = false;

  // Post-process only the channel(s) played.
  s.voice.set_channels((s.options & kOptionMix) ? 3 : ((s.options & kOptionAuxOut) ? 2 : 1));
  for (int b = 0; b < kCallSize; b += PLAITS_BLOCK_SIZE) {
    if (s.retrigger) {
      s.voice.Retrigger();
      s.retrigger = false;
    }
    m.trigger = kQ15One;   // the gate stays high: the OP-1 reports no note-off
    s.voice.Render(patch, m, frames + 2 * b, PLAITS_BLOCK_SIZE);
  }
  // The voice's 16-bit output (out, aux) to one mono channel.
  for (int i = 0; i < kCallSize; ++i) {
    const int32_t o = frames[2 * i];
    const int32_t a = frames[2 * i + 1];
    mono[i] = (s.options & kOptionMix) ? (o + a) >> 1 : ((s.options & kOptionAuxOut) ? a : o);
  }
  s.upsampler.Process(mono, out, kCallSize);
}

extern "C" void PlaitsNoteOn(void* self, const int16_t* knobs) {
  using namespace op1plaits;
  if (!initialized) InitAll();
  PlaitsVoiceState& s = states[op1::VoiceIndex(self)];
  if (knobs) {
    s.model = DecodePlaitsSetting(knobs[kModelKnob]);
    s.options = DecodePlaitsSetting(knobs[kOptionsKnob]);
  }
  s.retrigger = true;
  s.smoother.Snap();
}
