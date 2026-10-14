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

#ifndef OP1RINGS_RESONATOR_H_
#define OP1RINGS_RESONATOR_H_

#include "stmlib/stmlib.h"

#include <algorithm>
#include <cmath>

#include "src/dsp.h"
#include "src/mode_bank.h"
#include "src/range_stats.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/delay_line.h"

// Resonator implementation: 0 = upstream TPT SVF (float), 1 = Chamberlin SVF
// (float), 2 = Chamberlin SVF with a fixed-point audio loop.
#ifndef OP1RINGS_RESONATOR
#define OP1RINGS_RESONATOR 1
#endif

namespace op1rings {

const int32_t kMaxModes = 64;

// Band-pass filter for one resonator mode.
//
// ProcessBandPass() is the upstream topology-preserving SVF (same arithmetic
// as stmlib::Svf). Its band-pass response is
//   H(z) = g h (1 - z^-2) / (1 + a1 z^-1 + a2 z^-2).
// ProcessChamberlin() gives the same response with a Chamberlin SVF:
//   lp += f bp;  hp = x' - lp - q bp;  bp += f hp
// with f = 2 g sqrt(h), q = r sqrt(h), fed by x' = x[n] + x[n-1] (shared by
// all modes), and output scaled by sqrt(h) / 2. Its damping lives in its own
// coefficient q, so short fixed-point coefficients keep the decay accurate,
// and it needs 3 multiplies per sample instead of 5.
struct ModeSvf {
  float g, r, h;
  float s1, s2;
  float f_c, q_c, gain_c;
  float lp_c, bp_c;

  void Init() {
    s1 = s2 = 0.0f;
    lp_c = bp_c = 0.0f;
    gain_c = 0.0f;
  }

  // u0 is the previous resonator input (the shared x[n-1] term), needed to
  // translate the Chamberlin state when the coefficients change.
  inline void set_f_q_fast(float f, float resonance, float u0) {
#if OP1RINGS_RESONATOR >= 1
    // Upstream keeps the TPT state (s1, s2) when the coefficients change.
    // Translate the Chamberlin state to that TPT state with the old
    // coefficients, and back with the new ones (closed form, derived with
    // sympy: see docs/rings-port.md).
    float t1 = 0.0f, t2 = 0.0f;
    if (gain_c > 0.0f) {
      t1 = gain_c * (1.0f - g * g - g * r) * bp_c - 0.5f * g * lp_c + g * u0;
      t2 = g * 2.0f * gain_c * bp_c + 0.5f * lp_c;
    }
#endif
    g = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(f);
    r = 1.0f / resonance;
    h = 1.0f / (1.0f + r * g + g * g);
    float sqrt_h = sqrtf(h);
    f_c = 2.0f * g * sqrt_h;
    q_c = r * sqrt_h;
    gain_c = 0.5f * sqrt_h;
#if OP1RINGS_RESONATOR >= 1
    float e = 1.0f + g * g - g * r;
    lp_c = 2.0f * (t2 * (1.0f - g * g - g * r) - 2.0f * g * t1 + 2.0f * g * g * u0) / e;
    bp_c = 2.0f * (t1 + g * t2 - g * u0) / (e * sqrt_h);
#endif
  }

  inline float ProcessChamberlin(float x) {
    lp_c += f_c * bp_c;
    float hp = x - lp_c - q_c * bp_c;
    bp_c += f_c * hp;
    OP1RINGS_RANGE("cham.lp", lp_c);
    OP1RINGS_RANGE("cham.bp", bp_c);
    OP1RINGS_RANGE("cham.hp", hp);
    return bp_c;
  }

  inline float ProcessBandPass(float in) {
    float hp, bp, lp;
    hp = (in - r * s1 - g * s1 - s2) * h;
    bp = g * hp + s1;
    s1 = g * hp + bp;
    lp = g * bp + s2;
    s2 = g * bp + lp;
    OP1RINGS_RANGE("res.hp", hp);
    OP1RINGS_RANGE("res.bp", bp);
    OP1RINGS_RANGE("res.s1", s1);
    OP1RINGS_RANGE("res.s2", s2);
    return bp;
  }
};

class Resonator {
 public:
  Resonator() { }
  ~Resonator() { }
  
  void Init();
  void Process(
      const float* in,
      float* out,
      float* aux,
      size_t size);
  
  inline void set_frequency(float frequency) {
    frequency_ = frequency;
  }
  
  inline void set_structure(float structure) {
    structure_ = structure;
  }
  
  inline void set_brightness(float brightness) {
    brightness_ = brightness;
  }
  
  inline void set_damping(float damping) {
    damping_ = damping;
  }
  
  inline void set_position(float position) {
    position_ = position;
  }
  
  inline void set_resolution(int32_t resolution) {
    resolution -= resolution & 1; // Must be even!
    resolution_ = std::min(resolution, kMaxModes);
  }
  
 private:
  int32_t ComputeFilters();
  float frequency_;
  float structure_;
  float brightness_;
  float position_;
  float previous_position_;
  float damping_;
  
  int32_t resolution_;
  float previous_input_;

#if OP1RINGS_RESONATOR == 2
  void ProcessFixed(int32_t num_modes, const float* in, float* out, float* aux,
                    size_t size);

  Mode modes_[kMaxModes];
  int32_t previous_input_q_;       // Q27
#endif

  ModeSvf f_[kMaxModes];
  
  DISALLOW_COPY_AND_ASSIGN(Resonator);
};

}  // namespace op1rings

#endif  // OP1RINGS_RESONATOR_H_
