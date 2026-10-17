// Plaits' voice (plaits/dsp/voice.cc) in fixed point: trigger handling, the
// decay envelope and its internal modulations, the low-pass gate (LPG) and
// its vactrol envelope, and each channel's post-processing (gain, limiter
// for the loud engines, LPG or bypass, 16-bit output).
//
// The engine is chosen by index (0..15, Plaits' original order); upstream's
// engine hysteresis quantizer on a CV isn't needed (the OP-1 sets an index).

#ifndef OP1PLAITS_FIXED_VOICE_H_
#define OP1PLAITS_FIXED_VOICE_H_

#include "fixed/engines.h"
#include "fixed/fixed_math.h"
#include "fixed/svf.h"

namespace op1plaits {

struct VoicePatch {
  int32_t note;          // Q16 semitones
  int32_t harmonics;     // Q15
  int32_t timbre;        // Q15
  int32_t morph;         // Q15
  int32_t frequency_modulation_amount;   // Q15, -1..1
  int32_t timbre_modulation_amount;      // Q15, -1..1
  int32_t morph_modulation_amount;       // Q15, -1..1
  int engine;            // 0..15
  int32_t decay;         // Q15
  int32_t lpg_colour;    // Q15
  // OP-1 extension (false upstream): bypass the LPG even when the trigger is
  // patched, so the engines still get triggers but the OP-1's ADSR alone
  // shapes the level.
  bool lpg_off;
};

struct VoiceModulations {
  int32_t note;          // Q16
  int32_t frequency;     // Q16 semitones (external FM)
  int32_t harmonics;     // Q15
  int32_t timbre;        // Q15
  int32_t morph;         // Q15
  int32_t trigger;       // Q15 (gate level, 0..1)
  int32_t level;         // Q15
  bool frequency_patched;
  bool timbre_patched;
  bool morph_patched;
  bool trigger_patched;
  bool level_patched;
};

// plaits::LPGEnvelope (vactrol model).
class LPGEnvelopeFixed {
 public:
  void Init() {
    vactrol_state_ = 0;
    gain_ = INT32_MAX;
    frequency_ = 1 << 30;
    hf_bleed_ = 0;
    ramp_up_ = false;
  }
  void Trigger() { ramp_up_ = true; }
  // All Q31.
  void ProcessPing(int32_t attack, int32_t short_decay, int32_t decay_tail, int32_t hf);
  void ProcessLP(int32_t level, int32_t short_decay, int32_t decay_tail, int32_t hf);
  int32_t gain() const { return gain_; }
  int32_t frequency() const { return frequency_; }
  int32_t hf_bleed() const { return hf_bleed_; }

 private:
  int32_t vactrol_state_;
  int32_t gain_;
  int32_t frequency_;
  int32_t hf_bleed_;
  bool ramp_up_;
};

// stmlib::Limiter.
class LimiterFixed {
 public:
  void Init() { peak_ = 1 << 25; }   // 0.5, Q26
  // pre_gain Q28; in_out Q26.
  void Process(int32_t pre_gain, int32_t* in_out, size_t size);
 private:
  int32_t peak_;   // Q26
};

class ChannelPostProcessorFixed {
 public:
  void Init() {
    lpg_previous_gain_ = 0;
    lpg_filter_.Init();
    Reset();
  }
  void Reset() { limiter_.Init(); }
  // gain Q28 (negative: limiter); lpg values Q31; in Q26 (modified); out:
  // int16 with a stride.
  void Process(int32_t gain, bool bypass_lpg, int32_t lpg_gain, int32_t lpg_frequency,
               int32_t lpg_hf_bleed, int32_t* in, int16_t* out, size_t size, size_t stride);
 private:
  LimiterFixed limiter_;
  int32_t lpg_previous_gain_;   // Q28
  SvfFixed lpg_filter_;
};

class VoiceFixed {
 public:
  void Init();
  // frames: interleaved (out, aux) int16, as upstream's Voice::Frame.
  void Render(const VoicePatch& patch, const VoiceModulations& modulations, int16_t* frames,
              size_t size);
  int active_engine() const { return previous_engine_index_; }
  EngineHost* engine() { return &engine_; }
  // Upstream delays the trigger by 5 blocks (sequencers' CV lags their
  // gate); the OP-1 glue sets 0.
  void set_trigger_delay(int blocks) { trigger_delay_ = blocks > 7 ? 7 : blocks; }
  // Which channels get post-processed (bit 0 OUT, bit 1 AUX; default both):
  // a mono host skips the channel it doesn't play (its frames stay as they
  // were).
  void set_channels(int mask) { channels_ = mask; }
  // A new note: the next block with the trigger high is a rising edge (the
  // OP-1 doesn't report note-offs, so the gate never falls by itself).
  void Retrigger() {
    trigger_state_ = false;
    for (int i = 0; i < 8; ++i) trigger_delay_line_[i] = 0;
  }

 private:
  int trigger_delay_;
  int channels_;
  EngineHost engine_;
  int previous_engine_index_;
  int32_t previous_note_;   // Q16
  bool trigger_state_;
  int32_t decay_envelope_;  // Q31
  LPGEnvelopeFixed lpg_envelope_;
  int32_t trigger_delay_line_[8];
  int trigger_delay_write_ptr_;
  ChannelPostProcessorFixed out_post_processor_;
  ChannelPostProcessorFixed aux_post_processor_;
  int32_t out_buffer_[kMaxBlockSize];
  int32_t aux_buffer_[kMaxBlockSize];
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_VOICE_H_
