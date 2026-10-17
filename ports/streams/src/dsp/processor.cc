// Copyright 2014 Emilie Gillet.
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
// Dynamics processor

#include "src/dsp/processor.h"

#include <algorithm>

namespace op1streams {

using namespace stmlib;
using namespace std;

#define REGISTER_PROCESSOR(ClassName) \
  { &Processor::ClassName ## Init, \
    &Processor::ClassName ## Process, \
    &Processor::ClassName ## Configure },

/* static */
const Processor::ProcessorCallbacks 
Processor::callbacks_table_[PROCESSOR_FUNCTION_LAST] = {
  REGISTER_PROCESSOR(Envelope)
  REGISTER_PROCESSOR(Vactrol)
  REGISTER_PROCESSOR(Follower)
  REGISTER_PROCESSOR(Compressor)
  REGISTER_PROCESSOR(FilterController)
  REGISTER_PROCESSOR(LorenzGenerator)
};

namespace {

template<typename P>
inline void RunBlock(P* p, const int16_t* audio, const int16_t* excite,
                     uint16_t* gain, uint16_t* frequency, int32_t n) {
  for (int32_t i = 0; i < n; ++i) {
    gain[i] = 0;
    frequency[i] = 65535;
    p->Process(audio[i], excite[i], &gain[i], &frequency[i]);
  }
}

}  // namespace

void Processor::ProcessBlock(
    const int16_t* audio,
    const int16_t* excite,
    uint16_t* gain,
    uint16_t* frequency,
    int32_t n) {
  switch (function_) {
    case PROCESSOR_FUNCTION_ENVELOPE:
      RunBlock(&envelope_, audio, excite, gain, frequency, n);
      break;
    case PROCESSOR_FUNCTION_VACTROL:
      RunBlock(&vactrol_, audio, excite, gain, frequency, n);
      break;
    case PROCESSOR_FUNCTION_FOLLOWER:
      RunBlock(&follower_, audio, excite, gain, frequency, n);
      break;
    case PROCESSOR_FUNCTION_COMPRESSOR:
      RunBlock(&compressor_, audio, excite, gain, frequency, n);
      break;
    case PROCESSOR_FUNCTION_FILTER_CONTROLLER:
      RunBlock(&filter_controller_, audio, excite, gain, frequency, n);
      break;
    default:
      RunBlock(&lorenz_generator_, audio, excite, gain, frequency, n);
      break;
  }
  if (n > 0) {
    last_gain_value_ = gain[n - 1];
    last_frequency_value_ = frequency[n - 1];
  }
}

void Processor::Init(uint8_t index) {
  for (uint8_t i = 0; i < PROCESSOR_FUNCTION_LAST; ++i) {
    (this->*callbacks_table_[i].init)();
  }
  dirty_ = true;
  alternate_ = false;
  linked_ = false;
  
  fill(&parameters_[0], &parameters_[2], 32768);
  fill(&globals_[0], &globals_[4], 32768);
  
  set_function(PROCESSOR_FUNCTION_ENVELOPE);
  
  lorenz_generator_.set_index(index);
}

}  // namespace op1streams
