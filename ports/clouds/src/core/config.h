// Compile-time profile of the Clouds port.
//
// OP1CLOUDS_SAMPLE_RATE = 32000: upstream's constants, all in samples, so the
// port processes the same sample stream as upstream Clouds and the outputs
// compare sample by sample (the "comparable" build, harness only).
//
// OP1CLOUDS_SAMPLE_RATE = 44100 (default; the OP-1's effect rate): the time
// constants that upstream expresses in samples are scaled by 44100 / 32000,
// so delays, grain sizes, buffers and smoothing keep upstream's durations:
//   record buffers (same seconds), grain sizes, the pitch shifter's window,
//   the diffuser's delays, the looper's delay smoothing and crossfade, the
//   grain gain smoothing and the freeze/feedback smoothing.
// Not scaled: the texture filters' cutoffs (normalised frequencies, so
// their knob-to-Hz map moves up 5.5 semitones), per-block (32-sample)
// smoothing of control values, and Rings' reverb, whose delay lengths are
// already ~1.32x Clouds' (its LFOs follow OP1RINGS_SAMPLE_RATE).

#ifndef OP1CLOUDS_CORE_CONFIG_H_
#define OP1CLOUDS_CORE_CONFIG_H_

#include <cstdint>

#ifndef OP1CLOUDS_SAMPLE_RATE
#define OP1CLOUDS_SAMPLE_RATE 44100
#endif

namespace op1clouds {

const int32_t kSampleRate = OP1CLOUDS_SAMPLE_RATE;
// Upstream's block (kMaxBlockSize). The OP-1 calls effects with 64
// samples: two blocks per call.
const int32_t kBlockSize = 32;

constexpr double kRateScale = static_cast<double>(OP1CLOUDS_SAMPLE_RATE) / 32000.0;

constexpr int32_t Scaled(int32_t samples) {
  return static_cast<int32_t>(samples * kRateScale + 0.5);
}

// Q31 one-pole coefficient upstream applies per sample at 32 kHz, for the
// same time constant at kSampleRate (c' = c / scale; the small-c form).
constexpr int32_t PerSampleQ31(double c) {
  return static_cast<int32_t>(c / kRateScale * 2147483648.0 + 0.5);
}

// Rounded Q-format constants at compile time.
constexpr int32_t Q(double x, int n) {
  return static_cast<int32_t>(x * static_cast<double>(1LL << n) + (x < 0.0 ? -0.5 : 0.5));
}

// Record buffers, in int16 words per channel including the 8-word
// interpolation tail. Upstream (clouds_test.cc): stereo 2 x 32704 words
// (1.02 s at 32 kHz), mono 59392 words (1.86 s).
#if OP1CLOUDS_SAMPLE_RATE == 32000
const int32_t kStereoRecordWords = 32704;
const int32_t kMonoRecordWords = 59392;
#else
const int32_t kStereoRecordWords = (Scaled(32704 - 8) + 8 + 7) & ~7;
const int32_t kMonoRecordWords = (Scaled(59392 - 8) + 8 + 7) & ~7;
#endif

// Looping delay: crossfade (kCrossfadeDuration) and delay smoothing.
const int32_t kLoopCrossfade = Scaled(64);
const int32_t kDelaySmoothingQ31 = PerSampleQ31(0.00005);

// Pitch shifter: window 128..2047 samples at 32 kHz, two delay lines in a
// power-of-two 16-bit buffer.
const int32_t kPitchShifterMinSize = Scaled(128);
const int32_t kPitchShifterMaxSize = Scaled(2047);
const int32_t kPitchShifterBufferBits = kPitchShifterMaxSize < 2048 ? 12 : 13;

// Diffuser: 8 allpasses (4 per side) in a power-of-two 32-bit buffer.
const int32_t kDiffuserBufferBits = OP1CLOUDS_SAMPLE_RATE <= 32000 ? 11 : 12;

// Smoothing per sample: grain gain normalisation (0.01).
const int32_t kGainSmoothingQ31 = PerSampleQ31(0.01);
// Per block: the freeze low-pass (0.0005 per 32-sample block).
const int32_t kFreezeSmoothingQ31 = PerSampleQ31(0.0005);

// Grains: upstream's count for 16-bit quality (32 stereo, 40 mono).
const int32_t kMaxNumGrains = 64;

}  // namespace op1clouds

#endif  // OP1CLOUDS_CORE_CONFIG_H_
