// Constants of the Streams effect core shared by the fixed-point port
// (src/core/streams_core.*) and the float reference model
// (harness/ref_model.h). Not upstream: the analog half of Streams, modelled.

#ifndef OP1STREAMS_CORE_CONFIG_H_
#define OP1STREAMS_CORE_CONFIG_H_

#include <stdint.h>

namespace op1streams {

// The OP-1 calls effects with 64 samples at 44.1 kHz (docs/device-tests.md
// Test 16).
const int32_t kBlockSize = 64;
const int32_t kSampleRate = 44100;

// --- VCA: the 2164 gain cell behind Streams' DAC -----------------------------
// Exponential response: 256 DAC codes = 1.55 dB (compressor.cc's
// kGainConstant), so 991 codes per octave, unity at code 32767 (gain.h). The
// hardware tops out around +18 dB (gain.h's comment on kMaxExponentialGain).
// Linear response: gain = code / 32767 (32767 = unity, 65535 = +6 dB).
// RESPONSE crossfades the two (Streams' response pot: exponential to linear).
const int32_t kVcaUnityCode = 32767;
const int32_t kVcaCodesPerOctave = 991;
const int32_t kVcaMaxLog2Q16 = 195935;     // +18 dB = 2.98974 octaves, Q16
const int32_t kVcaMinLog2Q16 = -16 << 16;  // below -96 dB: silent
const int32_t kVcaMaxGainQ16 = 8 << 16;    // the port's headroom limit

// --- VCF: the filter behind Streams' PWM cutoff ------------------------------
// Code 0..65535 (65535 = open) maps exponentially to 20 Hz .. 20.5 kHz
// (10 octaves), capped at 0.45 fs. Two dampings: k = sqrt(2) (Butterworth,
// "flat") and k = 0.5 (Q = 2, resonant).
const double kCutoffMinHz = 20.0;
const double kCutoffOctaves = 10.0;
const double kCutoffMaxRatio = 0.45;
const int32_t kNumDampings = 2;
// SVF coefficients are looked up for every 4th sample and stepped linearly.
const int32_t kCoefficientStrideShift = 2;
const int32_t kCoefficientStride = 1 << kCoefficientStrideShift;

enum FilterMode {
  FILTER_LP,            // low-pass, k = sqrt(2) (Streams' VCF, LPG-like)
  FILTER_LP_RESONANT,   // low-pass, k = 0.5
  FILTER_BP,            // band-pass (unity peak), k = sqrt(2)
  FILTER_BP_RESONANT,   // band-pass (unity peak), k = 0.5
  FILTER_MODE_LAST
};

// Detector feeding the processors' EXCITE input (the effect's own audio).
enum DetectorMode {
  DETECTOR_RAW,       // the audio itself, as patched into EXCITE
  DETECTOR_ENVELOPE,  // rectified, fast-attack peak envelope (envelope,
                      // vactrol, filter controller and Lorenz only)
  DETECTOR_MODE_LAST
};

// Peak envelope time constants (shifts per sample at 44.1 kHz): attack
// 2^4 samples (0.36 ms), release 2^10 samples (23 ms).
const int32_t kDetectorAttackShift = 4;
const int32_t kDetectorReleaseShift = 10;

// SENSE: detector gain, 0 dB at 0 to +36 dB at 32767 (6 octaves).
const int32_t kSenseOctaves = 6;
// OUTPUT: trim, -12 dB at 0, 0 dB at 16384, +12 dB at 32767.

}  // namespace op1streams

#endif  // OP1STREAMS_CORE_CONFIG_H_
