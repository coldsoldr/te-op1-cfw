// Plaits' speech engine (plaits/dsp/engine/speech_engine.cc and
// dsp/speech/*) in fixed point: a naive formant synth, SAM-style formant
// synthesis and LPC (TI 5100-style word banks, 10th order lattice at ~8
// kHz), blended by HARMONICS.

#ifndef OP1PLAITS_FIXED_SPEECH_ENGINE_H_
#define OP1PLAITS_FIXED_SPEECH_ENGINE_H_

#include "fixed/fixed_math.h"
#include "fixed/oscillator.h"
#include "fixed/svf.h"

namespace op1plaits {

const int kNaiveSpeechNumFormants = 5;
const int kNaiveSpeechNumPhonemes = 5;
const int kNaiveSpeechNumRegisters = 5;

class NaiveSpeechSynthFixed {
 public:
  void Init();
  // frequency Q32; phoneme, vocal_register Q31. excitation, output: Q23.
  void Render(bool click, uint32_t frequency, int32_t phoneme, int32_t vocal_register,
              int32_t* excitation, int32_t* output, size_t size);
 private:
  OscillatorFixed pulse_;
  uint32_t click_duration_;
  SvfFixed filter_[kNaiveSpeechNumFormants];
  SvfFixed pulse_coloration_;
};

const int kSAMNumFormants = 3;
const int kSAMNumVowels = 9;
const int kSAMNumConsonants = 8;

class SAMSpeechSynthFixed {
 public:
  void Init();
  // frequency Q32; vowel, formant_shift Q31. excitation, output: Q23.
  void Render(bool consonant, uint32_t frequency, int32_t vowel, int32_t formant_shift,
              int32_t* excitation, int32_t* output, size_t size);
 private:
  uint32_t phase_;            // Q32
  uint32_t f_;                // Q32 (ComputeFrequency)
  int32_t frequency_;         // Q32
  int32_t pulse_next_sample_; // Q28
  int32_t pulse_lp_;          // Q28
  uint32_t formant_phase_[kSAMNumFormants];
  uint32_t consonant_samples_;
  int consonant_index_;
};

const int kLPCOrder = 10;

// LPCFrame (upstream LPCSpeechSynth::Frame, 14 bytes): fixed/tables.h.

class LPCSpeechSynthFixed {
 public:
  void Init();
  // The pitch increment for the current frame. prosody: Q31.
  void ComputeFrequency(int32_t prosody_amount, FFloat pitch_shift);
  // One LPC sample; outputs Q26.
  void Render(int32_t* excitation, int32_t* output);
  // Frame position: integral part and Q31 fraction (0: no interpolation).
  void PlayFrame(const LPCFrame* frames, int integral, int32_t fractional_q31);
 private:
  void PlayFrame(const LPCFrame& f1, const LPCFrame& f2, int32_t blend_q31);
  uint32_t phase_;            // Q32
  uint32_t f_;                // Q32 (ComputeFrequency)
  int32_t frequency_;         // Q32 (cycles per LPC sample)
  int32_t noise_energy_;      // Q26
  int32_t pulse_energy_;      // Q26
  int32_t next_sample_;       // Q26
  int excitation_pulse_sample_index_;
  int32_t k_[kLPCOrder];      // Q31
  int32_t s_[kLPCOrder + 1];  // Q26
};

const int kLPCSpeechSynthMaxWords = 32;
const int kLPCSpeechSynthMaxFrames = 1024;
const int kLPCSpeechSynthNumVowels = 5;
const int kLPCSpeechSynthNumConsonants = 10;
const int kLPCSpeechSynthNumWordBanks = 5;

class LPCSpeechSynthWordBankFixed {
 public:
  void Init(Allocator* allocator);
  void Reset();
  bool Load(int bank);
  int num_frames() const { return num_frames_; }
  const LPCFrame* frames() const { return frames_; }
  // address: Q31.
  void GetWordBoundaries(int32_t address, int* start, int* end) const;
 private:
  int loaded_bank_;
  int num_frames_;
  int num_words_;
  const int16_t* word_boundaries_;
  const LPCFrame* frames_;
};

class LPCSpeechSynthControllerFixed {
 public:
  void Init(LPCSpeechSynthWordBankFixed* word_bank);
  // frequency Q32; prosody, speed, address, formant_shift, gain Q31 (prosody
  // and speed may be negative). Outputs: Q23 (added to nothing: replaced).
  void Render(bool free_running, bool trigger, int bank, uint32_t frequency,
              int32_t prosody_amount, int32_t speed, int32_t address, int32_t formant_shift,
              int32_t gain, int32_t* excitation, int32_t* output, size_t size);
 private:
  uint32_t clock_phase_;      // Q31, [0, 2)
  int32_t sample_[2];         // Q26
  int32_t next_sample_[2];
  int32_t gain_;              // Q31
  LPCSpeechSynthFixed synth_;
  int playback_frame_;
  int last_playback_frame_;
  uint32_t remaining_frame_samples_;
  LPCSpeechSynthWordBankFixed* word_bank_;
};

class SpeechEngineFixed {
 public:
  void Init(Allocator* allocator);
  void Reset() { word_bank_.Reset(); }
  void Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
              size_t size, bool* already_enveloped);
  void set_prosody_amount(int32_t a) { prosody_amount_ = a; }
  void set_speed(int32_t s) { speed_ = s; }
 private:
  int word_bank_quantized_;   // HysteresisQuantizer2 state
  NaiveSpeechSynthFixed naive_speech_synth_;
  SAMSpeechSynthFixed sam_speech_synth_;
  LPCSpeechSynthControllerFixed lpc_speech_synth_controller_;
  LPCSpeechSynthWordBankFixed word_bank_;
  int32_t* temp_buffer_[2];
  int32_t prosody_amount_;    // Q31
  int32_t speed_;             // Q31
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_SPEECH_ENGINE_H_
