// Copyright 2016 Emilie Gillet.
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
// Additive synthesis with 24+8 partials.

#ifndef OP1PLAITS_DSP_ENGINE_ADDITIVE_ENGINE_H_
#define OP1PLAITS_DSP_ENGINE_ADDITIVE_ENGINE_H_

#include "src/engine/engine.h"
#include "src/oscillator/harmonic_oscillator.h"

namespace op1plaits {
  
const int kHarmonicBatchSize = 12;
const int kNumHarmonics = 36;
const int kNumHarmonicOscillators = kNumHarmonics / kHarmonicBatchSize;

class AdditiveEngine : public Engine {
 public:
  AdditiveEngine() { }
  ~AdditiveEngine() { }
  
  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
 
 private:
  void UpdateAmplitudes(
      float centroid,
      float slope,
      float bumps,
      float* amplitudes,
      const int* harmonic_indices,
      size_t num_harmonics);
      
  HarmonicOscillator<kHarmonicBatchSize> harmonic_oscillator_[kNumHarmonicOscillators];
  
  float* amplitudes_;
  
  DISALLOW_COPY_AND_ASSIGN(AdditiveEngine);
};

}  // namespace op1plaits

#endif  // OP1PLAITS_DSP_ENGINE_ADDITIVE_ENGINE_H_