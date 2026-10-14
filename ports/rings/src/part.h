// Copyright 2015 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Group of voices.

#ifndef OP1RINGS_PART_H_
#define OP1RINGS_PART_H_

#include <algorithm>

#include "stmlib/stmlib.h"
#include "stmlib/dsp/cosine_oscillator.h"
#include "stmlib/dsp/delay_line.h"

#include "src/dsp.h"
#include "src/fm_voice.h"
#include "src/fx/reverb.h"
#include "src/limiter.h"
#include "src/note_filter.h"
#include "src/patch.h"
#include "src/performance_state.h"
#include "src/plucker.h"
#include "src/resonator.h"
#include "src/modal_resonator.h"
#include "src/string.h"

namespace op1rings {

enum ResonatorModel {
  RESONATOR_MODEL_MODAL,
  RESONATOR_MODEL_SYMPATHETIC_STRING,
  RESONATOR_MODEL_STRING,
  
  // Bonus!
  RESONATOR_MODEL_FM_VOICE,
  RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED,
  RESONATOR_MODEL_STRING_AND_REVERB,
  RESONATOR_MODEL_LAST
};

const int32_t kMaxPolyphony = 4;

#if OP1RINGS_RESONATOR == 3
// Float interface over the fixed-point ModalResonator, while the rest of
// Part is still float (host builds only).
class PartResonator {
 public:
  void Init() { r_.Init(); }
  void set_resolution(int32_t n) { r_.set_resolution(n); }
  void set_frequency(float f) { r_.set_frequency(FloatToQ31(f)); }
  void set_structure(float x) { r_.set_structure(FloatToQ31(x)); }
  void set_brightness(float x) { r_.set_brightness(FloatToQ31(x)); }
  void set_damping(float x) { r_.set_damping(FloatToQ31(x)); }
  void set_position(float x) { r_.set_position(FloatToQ31(x)); }
  void Process(const float* in, float* out, float* aux, size_t size) {
    int32_t in_q[kMaxModalBlockSize], out_q[kMaxModalBlockSize], aux_q[kMaxModalBlockSize];
    for (size_t i = 0; i < size; ++i) in_q[i] = FloatToQ27(in[i]);
    r_.Process(in_q, out_q, aux_q, size);
    for (size_t i = 0; i < size; ++i) {
      out[i] = Q27ToFloat(out_q[i]);
      aux[i] = Q27ToFloat(aux_q[i]);
    }
  }
 private:
  ModalResonator r_;
};
#else
typedef Resonator PartResonator;
#endif
const int32_t kNumStrings = kMaxPolyphony * 2;

class Part {
 public:
  Part() { }
  ~Part() { }
  
  void Init(uint16_t* reverb_buffer);
  
  void Process(
      const PerformanceState& performance_state,
      const Patch& patch,
      const float* in,
      float* out,
      float* aux,
      size_t size);

  inline bool bypass() const { return bypass_; }
  inline void set_bypass(bool bypass) { bypass_ = bypass; }

  inline int32_t polyphony() const { return polyphony_; }
  inline void set_polyphony(int32_t polyphony) {
    int32_t old_polyphony = polyphony_;
    polyphony_ = std::min(polyphony, kMaxPolyphony);
    for (int32_t i = old_polyphony; i < polyphony_; ++i) {
      note_[i] = note_[0] + i * 0.05f;
    }
    dirty_ = true;
  }
  
  inline ResonatorModel model() const { return model_; }
  inline void set_model(ResonatorModel model) {
    if (model != model_) {
      model_ = model;
      dirty_ = true;
    }
  }

 private:
  void ConfigureResonators();
  void RenderModalVoice(
      int32_t voice,
      const PerformanceState& performance_state,
      const Patch& patch,
      float frequency,
      float filter_cutoff,
      size_t size);
  void RenderStringVoice(
      int32_t voice,
      const PerformanceState& performance_state,
      const Patch& patch,
      float frequency,
      float filter_cutoff,
      size_t size);
  void RenderFMVoice(
      int32_t voice,
      const PerformanceState& performance_state,
      const Patch& patch,
      float frequency,
      float filter_cutoff,
      size_t size);
  

  inline float Squash(float x) const {
    if (x < 0.5f) {
      x *= 2.0f;
      x *= x;
      x *= x;
      x *= x;
      x *= x;
      x *= 0.5f;
    } else {
      x = 2.0f - 2.0f * x;
      x *= x;
      x *= x;
      x *= x;
      x *= x;
      x = 1.0f - 0.5f * x;
    }
    return x;
  }

  void ComputeSympatheticStringsNotes(
      float tonic,
      float note,
      float parameter,
      float* destination,
      size_t num_strings);
  
  bool bypass_;
  bool dirty_;

  ResonatorModel model_;

  int32_t num_voices_;
  int32_t active_voice_;
  uint32_t step_counter_;
  int32_t polyphony_;
  
  PartResonator resonator_[kMaxPolyphony];
  String string_[kNumStrings];
  stmlib::CosineOscillator lfo_[kNumStrings];
  FMVoice fm_voice_[kMaxPolyphony];
  
  stmlib::Svf excitation_filter_[kMaxPolyphony];
  stmlib::DCBlocker dc_blocker_[kMaxPolyphony];
  Plucker plucker_[kMaxPolyphony];

  float note_[kMaxPolyphony];
  NoteFilter note_filter_;
  
  float resonator_input_[kMaxBlockSize];
  float sympathetic_resonator_input_[kMaxBlockSize];
  float noise_burst_buffer_[kMaxBlockSize];
  
  float out_buffer_[kMaxBlockSize];
  float aux_buffer_[kMaxBlockSize];
  
  Reverb reverb_;
  Limiter limiter_;
  
  static float model_gains_[RESONATOR_MODEL_LAST];
  
  DISALLOW_COPY_AND_ASSIGN(Part);
};

}  // namespace op1rings

#endif  // OP1RINGS_PART_H_
