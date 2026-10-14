// One Rings voice for the OP-1, all fixed point (no floating point at run
// time). It replaces rings::Part for a single voice: the OP-1 core does the
// voice allocation, so there's no note filter, strummer or polyphony here.
//
// Signal path (modal model, internal exciter, as upstream with polyphony 1):
//   note on -> impulse -> excitation low-pass -> ModalResonator -> x1.4 ->
//   limiter -> out (odd modes) / aux (even modes)
//
// Pitch comes from the OP-1's per-sample phase increments (f / fs * 2^32);
// parameters from its four encoders.

#ifndef OP1RINGS_OP1_VOICE_H_
#define OP1RINGS_OP1_VOICE_H_

#include <cstddef>
#include <cstdint>

#include "src/modal_resonator.h"

namespace op1rings {

#ifndef OP1RINGS_SUBBLOCK_SIZE
#define OP1RINGS_SUBBLOCK_SIZE 32
#endif

// Pitch and parameters are latched once per control block (a multiple of
// the sub-block), so the coefficients are recomputed at most once per
// control block. Upstream's control rate is 2 kHz (24 samples at 48 kHz).
#ifndef OP1RINGS_CONTROL_BLOCK_SIZE
#define OP1RINGS_CONTROL_BLOCK_SIZE 64
#endif

const size_t kSubblockSize = OP1RINGS_SUBBLOCK_SIZE;
const size_t kControlBlockSize = OP1RINGS_CONTROL_BLOCK_SIZE;

// Rings' parameters, in Q31 ([0, 1]).
struct VoicePatch {
  int32_t structure;
  int32_t brightness;
  int32_t damping;
  int32_t position;
};

// Low-pass TPT SVF (stmlib::Svf, FREQUENCY_DIRTY) for the exciter, Q27 state.
class ExcitationFilter {
 public:
  void Init();
  // f: cutoff / sample rate (Q31, <= 0.499), resonance 1.5 (internal exciter).
  void SetCutoff(int32_t f);
  // Adds a unit impulse of amplitude x0 (given as g, h-scaled terms) at the
  // next sample: see op1_voice.cc.
  void AddImpulse(int32_t first_output, int32_t s1, int32_t s2);
  void Process(int32_t* out, size_t size);   // zero input, Q27 output

  int32_t g() const { return g_; }
  int32_t h() const { return h_; }

 private:
  int32_t g_;          // Q29
  int32_t k_;          // r + g, Q29
  int32_t h_;          // Q31
  int32_t s1_, s2_;    // Q27
  bool pending_;       // impulse terms to add at the next sample
  int32_t pending_output_;
  int32_t pending_s1_;
  int32_t pending_s2_;
};

// Rings' limiter (rings/dsp/limiter.h), stereo, fixed point.
class Limiter {
 public:
  void Init();
  void Process(int32_t* l, int32_t* r, size_t size, int32_t pre_gain_q28);

 private:
  int32_t peak_;   // Q27
  int32_t gain_;   // 1 / max(peak, 1), Q31
};

class RingsVoice {
 public:
  void Init(int32_t resolution);
  void NoteOn() { strum_ = true; }

  // phase_increment: size values (f / fs * 2^32); out, aux: size Q27 samples
  // (after the limiter, ~[-1, 1]). size must be a multiple of
  // kControlBlockSize.
  void Render(const uint32_t* phase_increment, const VoicePatch& patch,
              int32_t* out, int32_t* aux, size_t size);

 private:
  void RenderSubblock(uint32_t phase_increment, const VoicePatch& patch,
                      int32_t* out, int32_t* aux);

  bool strum_;
  ModalResonator resonator_;
  ExcitationFilter excitation_filter_;
  Limiter limiter_;
};

}  // namespace op1rings

#endif  // OP1RINGS_OP1_VOICE_H_
