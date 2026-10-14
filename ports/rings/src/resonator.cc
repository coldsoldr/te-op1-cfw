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
// Resonator.

#include "src/resonator.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/cosine_oscillator.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "src/fixed.h"
#include "src/mode_bank.h"
#include "src/resources.h"

namespace op1rings {

using namespace std;
using namespace stmlib;

void Resonator::Init() {
  for (int32_t i = 0; i < kMaxModes; ++i) {
    f_[i].Init();
  }

  set_frequency(220.0f / kSampleRate);
  set_structure(0.25f);
  set_brightness(0.5f);
  set_damping(0.3f);
  set_position(0.999f);
  previous_position_ = 0.0f;
  previous_input_ = 0.0f;
#if OP1RINGS_RESONATOR == 2
  for (int32_t i = 0; i < kMaxModes; ++i) {
    Mode& m = modes_[i];
    m.lp = m.bp = m.half_f = m.half_q = m.weight = m.weight_increment = 0;
  }
  previous_input_q_ = 0;
#endif
  set_resolution(kMaxModes);
}

int32_t Resonator::ComputeFilters() {
  float stiffness = Interpolate(lut_stiffness, structure_, 256.0f);
  float harmonic = frequency_;
  float stretch_factor = 1.0f; 
  float q = 500.0f * Interpolate(
      lut_4_decades,
      damping_,
      256.0f);
  float brightness_attenuation = 1.0f - structure_;
  // Reduces the range of brightness when structure is very low, to prevent
  // clipping.
  brightness_attenuation *= brightness_attenuation;
  brightness_attenuation *= brightness_attenuation;
  brightness_attenuation *= brightness_attenuation;
  float brightness = brightness_ * (1.0f - 0.2f * brightness_attenuation);
  float q_loss = brightness * (2.0f - brightness) * 0.85f + 0.15f;
  float q_loss_damping_rate = structure_ * (2.0f - structure_) * 0.1f;
  int32_t num_modes = 0;
  for (int32_t i = 0; i < min(kMaxModes, resolution_); ++i) {
    float partial_frequency = harmonic * stretch_factor;
    if (partial_frequency >= 0.49f) {
      partial_frequency = 0.49f;
    } else {
      num_modes = i + 1;
    }
    f_[i].set_f_q_fast(
        partial_frequency,
        1.0f + partial_frequency * q,
        previous_input_);
    if (partial_frequency < 0.49f) {
      OP1RINGS_RANGE("res.coef.g", f_[i].g);
      OP1RINGS_RANGE("res.coef.r", f_[i].r);
      OP1RINGS_RANGE("res.coef.h", f_[i].h);
      OP1RINGS_RANGE("res.coef.1-h", 1.0f - f_[i].h);
      OP1RINGS_RANGE("res.coef.r*g", f_[i].r * f_[i].g);
    }
    stretch_factor += stiffness;
    if (stiffness < 0.0f) {
      // Make sure that the partials do not fold back into negative frequencies.
      stiffness *= 0.93f;
    } else {
      // This helps adding a few extra partials in the highest frequencies.
      stiffness *= 0.98f;
    }
    // This prevents the highest partials from decaying too fast.
    q_loss += q_loss_damping_rate * (1.0f - q_loss);
    harmonic += frequency_;
    q *= q_loss;
  }
  
  return num_modes;
}

#if OP1RINGS_RESONATOR == 2

// Per-mode amplitudes for a pickup position: the same cosine recurrence as
// stmlib::CosineOscillator (approximate mode), across modes.
static void ComputeAmplitudes(float position, float* amplitudes, int32_t n) {
  CosineOscillator osc;
  osc.Init<COSINE_OSCILLATOR_APPROXIMATE>(position);
  osc.Start();
  for (int32_t i = 0; i < n; ++i) {
    amplitudes[i] = osc.Next();
  }
}

void Resonator::ProcessFixed(
    int32_t num_modes, const float* in, float* out, float* aux, size_t size) {
  // Modes are processed in pairs (odd -> out, even -> aux), like upstream.
  int32_t n = (num_modes + 1) & ~1;

  // Weights (amplitude * gain) move linearly from the previous pickup
  // position to the new one over the block, as upstream interpolates the
  // position per sample. Both ends use this block's gains.
  float start_amplitudes[kMaxModes];
  float end_amplitudes[kMaxModes];
  ComputeAmplitudes(previous_position_, start_amplitudes, n);
  ComputeAmplitudes(position_, end_amplitudes, n);
  const float step = 1.0f / static_cast<float>(size);
  for (int32_t i = 0; i < n; ++i) {
    float start = start_amplitudes[i] * f_[i].gain_c;
    float end = end_amplitudes[i] * f_[i].gain_c;
    modes_[i].weight_increment = FloatToQ31((end - start) * step);
    modes_[i].weight = FloatToQ31(start);
  }
  // Amplitude isn't linear in position, so a large jump in one block (the
  // first block after Init, or a big knob jump) uses exact per-sample
  // weights instead of the linear ramp.
  const bool exact_weights = fabsf(position_ - previous_position_) > 0.02f;
  const float position_increment = (position_ - previous_position_) * step;
  float position = previous_position_;
  previous_position_ = position_;

  int32_t in_q[kMaxBlockSize];
  int32_t out_q[kMaxBlockSize];
  int32_t aux_q[kMaxBlockSize];
  for (size_t s = 0; s < size; ++s) {
    in_q[s] = FloatToQ27(in[s] * 0.125f);
  }
  if (!exact_weights) {
    ModeBankProcess(modes_, n, in_q, &previous_input_q_, out_q, aux_q, size);
  } else {
    for (int32_t i = 0; i < n; ++i) {
      modes_[i].weight_increment = 0;
    }
    for (size_t s = 0; s < size; ++s) {
      position += position_increment;
      float amplitudes[kMaxModes];
      ComputeAmplitudes(position, amplitudes, n);
      for (int32_t i = 0; i < n; ++i) {
        modes_[i].weight = FloatToQ31(amplitudes[i] * f_[i].gain_c);
      }
      ModeBankProcess(modes_, n, &in_q[s], &previous_input_q_, &out_q[s],
                      &aux_q[s], 1);
    }
  }
  for (size_t s = 0; s < size; ++s) {
    out[s] = Q27ToFloat(out_q[s]);
    aux[s] = Q27ToFloat(aux_q[s]);
  }
}

#endif  // OP1RINGS_RESONATOR == 2

void Resonator::Process(const float* in, float* out, float* aux, size_t size) {
#if OP1RINGS_RESONATOR == 2
  {
  // The coefficient code (still float, host only for now) works on the float
  // Chamberlin state: sync it from the fixed-point bank and back.
  for (int32_t i = 0; i < kMaxModes; ++i) {
    f_[i].lp_c = 2.0f * Q27ToFloat(modes_[i].lp);
    f_[i].bp_c = Q27ToFloat(modes_[i].bp);
  }
  previous_input_ = Q27ToFloat(previous_input_q_);
  int32_t num_modes = ComputeFilters();
  for (int32_t i = 0; i < kMaxModes; ++i) {
    modes_[i].lp = FloatToQ27(0.5f * f_[i].lp_c);
    modes_[i].bp = FloatToQ27(f_[i].bp_c);
    modes_[i].half_f = FloatToQ31(0.5f * f_[i].f_c);
    modes_[i].half_q = FloatToQ31(0.5f * f_[i].q_c);
  }
  ProcessFixed(num_modes, in, out, aux, size);
  return;
  }
#endif
  int32_t num_modes = ComputeFilters();

  ParameterInterpolator position(&previous_position_, position_, size);
  while (size--) {
    CosineOscillator amplitudes;
    amplitudes.Init<COSINE_OSCILLATOR_APPROXIMATE>(position.Next());
    
    float input = *in++ * 0.125f;
    float odd = 0.0f;
    float even = 0.0f;
    amplitudes.Start();
#if OP1RINGS_RESONATOR == 0
    for (int32_t i = 0; i < num_modes;) {
      odd += amplitudes.Next() * f_[i++].ProcessBandPass(input);
      even += amplitudes.Next() * f_[i++].ProcessBandPass(input);
    }
#else
    float shared_input = input + previous_input_;
    previous_input_ = input;
    for (int32_t i = 0; i < num_modes;) {
      ModeSvf& a = f_[i++];
      odd += amplitudes.Next() * a.gain_c * a.ProcessChamberlin(shared_input);
      ModeSvf& b = f_[i++];
      even += amplitudes.Next() * b.gain_c * b.ProcessChamberlin(shared_input);
    }
#endif
    OP1RINGS_RANGE("res.in", input);
    OP1RINGS_RANGE("res.out", odd);
    OP1RINGS_RANGE("res.aux", even);
    *out++ = odd;
    *aux++ = even;
  }
}

}  // namespace op1rings
