// The Streams effect core for the OP-1: one channel of Mutable Instruments
// Streams as an insert effect, mono (synth/drum effect) or stereo (master
// effect, linked detection), 44.1 kHz, blocks of up to 64 samples.
//
//   audio --+--> detector (SENSE gain, raw or peak envelope) --> EXCITE
//           |                                                      |
//           |      upstream processor (envelope, vactrol, follower,|
//           |      compressor, filter controller, Lorenz), retimed <
//           |              | gain code            | cutoff code
//           +--> VCA (2164 model: exp/lin) --> SVF (LP/BP) --> OUTPUT trim
//           |                                                      |
//           +-------------------------- dry/wet MIX <--------------+
//
// Streams' audio path is analog (a 2164 VCA and a voltage-controlled filter
// driven by a DAC and a PWM); the processors only compute those two control
// signals. Here the VCA and filter are digital models (docs/streams-port.md).
//
// Fixed point throughout, no division or float at run time. Not copyable;
// no constructor work (the OP-1 image has no startup code): call Init().

#ifndef OP1STREAMS_CORE_STREAMS_CORE_H_
#define OP1STREAMS_CORE_STREAMS_CORE_H_

#include <stdint.h>

#include "src/core/core_config.h"
#include "src/dsp/processor.h"
#include "src/dsp/retime.h"

namespace op1streams {

// The processors run on every sample (OP1STREAMS_SAMPLE_RATE 44100, the
// default) or on every other one (22050: the cheap profile; EXCITE averaged
// over pairs, the outputs interpolated back to 44.1 kHz).
const int32_t kControlDecimation = kSampleRate / OP1STREAMS_SAMPLE_RATE;

// Knob values are 0..32767 (the OP-1 encoders' range for phase; the FX
// ranges for the host slot are still to read: docs/streams-port.md).
struct Controls {
  int16_t shape;      // SHAPE (compressor: threshold)
  int16_t mod;        // MOD (compressor: ratio below 50%, makeup above)
  int16_t sense;      // detector gain, 0 .. +36 dB
  int16_t mix;        // dry/wet, 0 = dry, 32767 = wet
  int16_t attack;     // split times: attack (upstream's linked global 0)
  int16_t decay;      // split times: decay (upstream's linked global 2)
  int16_t response;   // VCA: 0 = exponential .. 32767 = linear
  int16_t output;     // trim: 0 = -12 dB, 16384 = 0 dB, 32767 = +12 dB
  uint8_t function;   // ProcessorFunction 0..5
  uint8_t alternate;  // upstream's alternate bank (long press)
  uint8_t split;      // separate attack/decay knobs (upstream's linked mode)
  uint8_t filter_mode;  // FilterMode
  uint8_t detector;     // DetectorMode
};

void DefaultControls(Controls* c);

struct SvfState {
  int32_t ic1;
  int32_t ic2;
};

class StreamsCore {
 public:
  // Clears all state. `controls` are applied on the next Process call.
  void Init();
  // After a gap in calls (the effect was off): fresh processor and filter.
  void Restart();

  // n <= kBlockSize. In place.
  void ProcessMono(const Controls& c, int16_t* buf, int32_t n);
  void ProcessStereo(const Controls& c, int16_t* l, int16_t* r, int32_t n);

  // Last block's control outputs (for meters and the reference comparison).
  const uint16_t* gain_codes() const { return gain_; }
  const uint16_t* cutoff_codes() const { return frequency_; }
  int32_t gain_reduction() const { return processor_.gain_reduction(); }

#ifdef OP1STREAMS_PROFILE
  // Cycles per stage, accumulated (Blackfin only): apply + detect,
  // processor, gains + coefficients, VCA + filter + mix.
  uint32_t profile[4];
#endif

 private:
  void Apply(const Controls& c);
  void Detect(int32_t n);
  void RunProcessor(int32_t n);
  void ComputeGainsAndCoefficients(int32_t n);
  void RenderChannel(const int16_t* in, int16_t* wet, SvfState* svf, int32_t n);
  void Mix(int16_t* buf, const int16_t* wet, int32_t n);

  Processor processor_;
  Controls applied_;
  bool configured_;
  bool bypass_filter_;     // compressor, follower's cutoff mode: frequency fixed at open
  bool unity_vca_;         // filter controller: gain from the (absent) LEVEL CV
  int32_t response_;       // effective, Q15 (compressor: 0)

  int32_t detector_env_;   // peak envelope, Q15
  // Block ramps (previous block's end value, target).
  int32_t sense_q8_;       // detector gain, Q8
  int32_t sense_target_q8_;
  int32_t trim_q16_;       // output trim, Q16
  int32_t trim_target_q16_;
  int32_t mix_q14_;
  int32_t mix_target_q14_;

  SvfState svf_[2];
  int32_t last_coefficients_[3];  // SVF coefficients at the last group end
  bool coefficients_valid_;

  // Per-block scratch.
  int16_t excite_[kBlockSize];
  uint16_t gain_[kBlockSize];
  uint16_t frequency_[kBlockSize];
  int32_t vca_q16_[kBlockSize];
  int32_t coefficients_[kBlockSize][3];
  int16_t wet_[kBlockSize];
  int16_t detector_in_[kBlockSize];
#if OP1STREAMS_SAMPLE_RATE == 22050
  int16_t excite_decimated_[kBlockSize / 2];
  uint16_t gain_decimated_[kBlockSize / 2];
  uint16_t frequency_decimated_[kBlockSize / 2];
  int32_t last_gain_;       // previous control sample, for the interpolation
  int32_t last_frequency_;
  bool last_valid_;
#endif

  DISALLOW_COPY_AND_ASSIGN(StreamsCore);
 public:
  StreamsCore() { }
};

// Exposed for tests: the VCA's gain (Q16) for a gain code and a response
// (Q15, 0 = exponential, 32767 = linear), clamped to kVcaMaxGainQ16.
int32_t VcaGainQ16(int32_t code, int32_t response_q15);
// 2^(x / 65536) in Q16 for x in [-16, 16) octaves (Q16), from upstream's
// lut_exp2.
int32_t Exp2Q16(int32_t x);

}  // namespace op1streams

#endif  // OP1STREAMS_CORE_STREAMS_CORE_H_
