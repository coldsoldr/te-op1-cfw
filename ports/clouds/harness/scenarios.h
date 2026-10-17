// Deterministic test scenarios for the Clouds port, modelled on
// clouds/test/clouds_test.cc (a 400 Hz sine through the looping delay at
// pitch -7 with freeze toggling; the granular grid of positions, sizes and
// pitches) plus a synthetic stereo "music" input, knob sweeps and mode
// switches.
//
// The same scenario drives the upstream float reference, the float copy
// and the fixed-point port, in 32-sample blocks (upstream's kMaxBlockSize)
// of int16 stereo frames.

#ifndef PORTS_CLOUDS_HARNESS_SCENARIOS_H_
#define PORTS_CLOUDS_HARNESS_SCENARIOS_H_

#include <cmath>
#include <cstdint>

namespace harness {

enum Mode { GRANULAR = 0, STRETCH = 1, LOOPING = 2, SPECTRAL = 3 };
enum Input { SINE400 = 0, MUSIC = 1 };
enum Freeze { NO_FREEZE = 0, FREEZE_TOGGLE = 1, FREEZE_AFTER_1S = 2 };
enum Sweep {
  NO_SWEEP = 0,
  SWEEP_POSITION = 1,   // position: triangle 0 -> 1 -> 0 over the scenario
  SWEEP_PITCH = 2,      // pitch steps every second: 0, +7, -12, +12
  SWEEP_SIZE = 3,       // size: triangle
  SWEEP_MODE = 4,       // looping delay for 2 s, then granular
  SWEEP_DRYWET = 5,     // dry/wet ramps 0 -> 1
  SWEEP_KNOBS = 6,      // position, size, density, texture all moving
};

struct FloatParams {
  float position, size, pitch, density, texture, dry_wet, stereo_spread,
      feedback, reverb;
  bool freeze, trigger;
};

struct Scenario {
  const char* name;
  int mode;
  int mono;     // 1: Clouds' mono quality (num_channels 1)
  int input;
  float seconds;
  float position, size, pitch, density, texture, feedback, dry_wet, reverb,
      spread;
  int freeze;
  int sweep;
  // 1: the port should match the reference's waveform (SNR is meaningful);
  // 0: compare spectra only (reverb differs by design: the port uses Rings'
  // reverb; granular random grains; float's stuck delay smoothing).
  int waveform;
};

// clang-format off
static const Scenario kScenarios[] = {
  // name               mode     mono input    sec  pos   size  pitch dens  tex   fb    wet   rev   spr  freeze           sweep           wave
  { "ld_sine_m7",        LOOPING,  0, SINE400, 4.0f, 0.0f, 0.5f, -7.0f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, FREEZE_TOGGLE,   NO_SWEEP,       1 },
  { "ld_music_p0",       LOOPING,  0, MUSIC,   4.0f, 0.0f, 0.5f,  0.0f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, NO_FREEZE,       NO_SWEEP,       1 },
  { "ld_music_p12",      LOOPING,  0, MUSIC,   4.0f, 0.0f, 0.3f, 12.0f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, NO_FREEZE,       NO_SWEEP,       1 },
  { "ld_music_lp_diff",  LOOPING,  0, MUSIC,   4.0f, 0.0f, 0.5f,  0.0f, 0.3f, 0.2f, 0.4f, 1.0f, 0.0f, 0.0f, NO_FREEZE,       NO_SWEEP,       1 },
  { "ld_music_hp",       LOOPING,  0, MUSIC,   4.0f, 0.0f, 0.5f, -5.0f, 0.0f, 0.85f, 0.2f, 1.0f, 0.0f, 0.0f, NO_FREEZE,      NO_SWEEP,       1 },
  { "ld_music_freeze",   LOOPING,  0, MUSIC,   4.0f, 0.2f, 0.6f,  5.0f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, FREEZE_AFTER_1S, NO_SWEEP,       1 },
  { "ld_music_drywet",   LOOPING,  0, MUSIC,   4.0f, 0.0f, 0.5f,  0.0f, 0.0f, 0.4f, 0.0f, 0.5f, 0.0f, 0.0f, NO_FREEZE,       SWEEP_DRYWET,   1 },
  { "ld_music_pitchsteps", LOOPING, 0, MUSIC,  4.0f, 0.0f, 0.4f,  0.0f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, NO_FREEZE,       SWEEP_PITCH,    1 },
  { "ld_mono_music",     LOOPING,  1, MUSIC,   4.0f, 0.0f, 0.5f,  7.0f, 0.0f, 0.3f, 0.3f, 1.0f, 0.0f, 0.0f, NO_FREEZE,       NO_SWEEP,       1 },
  { "ld_position_sweep", LOOPING,  0, MUSIC,   4.0f, 0.3f, 0.5f,  0.0f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, NO_FREEZE,       SWEEP_POSITION, 0 },
  { "ld_reverb_fb",      LOOPING,  0, MUSIC,   4.0f, 0.1f, 0.5f,  0.0f, 0.2f, 0.5f, 0.5f, 0.7f, 0.6f, 0.0f, NO_FREEZE,       NO_SWEEP,       0 },
  { "gr_det",            GRANULAR, 0, MUSIC,   4.0f, 0.3f, 0.5f,  0.0f, 0.25f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, NO_FREEZE,      NO_SWEEP,       1 },
  { "gr_rand_p12",       GRANULAR, 0, MUSIC,   4.0f, 0.2f, 0.3f, 12.0f, 0.75f, 0.3f, 0.0f, 1.0f, 0.0f, 0.0f, NO_FREEZE,      NO_SWEEP,       1 },
  { "gr_dense_m12",      GRANULAR, 0, MUSIC,   4.0f, 0.5f, 0.6f, -12.0f, 0.95f, 0.65f, 0.0f, 1.0f, 0.0f, 0.0f, NO_FREEZE,    NO_SWEEP,       1 },
  { "gr_diffuse",        GRANULAR, 0, MUSIC,   4.0f, 0.4f, 0.4f,  0.0f, 0.8f, 0.9f, 0.0f, 1.0f, 0.0f, 0.0f, NO_FREEZE,       NO_SWEEP,       1 },
  { "gr_freeze_sine",    GRANULAR, 0, SINE400, 4.0f, 0.5f, 0.5f,  7.0f, 0.75f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, FREEZE_TOGGLE,  NO_SWEEP,       1 },
  { "gr_mono",           GRANULAR, 1, MUSIC,   4.0f, 0.3f, 0.5f, -5.0f, 0.7f, 0.6f, 0.0f, 1.0f, 0.0f, 0.0f, NO_FREEZE,       NO_SWEEP,       1 },
  { "gr_spread",         GRANULAR, 0, MUSIC,   4.0f, 0.3f, 0.4f,  0.0f, 0.8f, 0.5f, 0.0f, 1.0f, 0.0f, 0.7f, NO_FREEZE,       NO_SWEEP,       1 },
  { "gr_knobs",          GRANULAR, 0, MUSIC,   4.0f, 0.3f, 0.4f,  3.0f, 0.7f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, NO_FREEZE,       SWEEP_KNOBS,    0 },
  { "gr_reverb_fb",      GRANULAR, 0, MUSIC,   4.0f, 0.3f, 0.5f,  0.0f, 0.8f, 0.5f, 0.4f, 0.8f, 0.5f, 0.0f, NO_FREEZE,       NO_SWEEP,       0 },
  { "mode_switch",       LOOPING,  0, MUSIC,   4.0f, 0.0f, 0.5f,  0.0f, 0.7f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, NO_FREEZE,       SWEEP_MODE,     1 },
};
// clang-format on

static const int kNumScenarios = sizeof(kScenarios) / sizeof(kScenarios[0]);

inline float Triangle(float t) {  // t in [0, 1] -> 0 -> 1 -> 0
  return t < 0.5f ? 2.0f * t : 2.0f - 2.0f * t;
}

// Parameters for block `block` of `num_blocks` (32 samples each).
inline void ScenarioParams(const Scenario& s, int block, int num_blocks,
                           int sample_rate, FloatParams* p, int* mode) {
  const float t = static_cast<float>(block) / static_cast<float>(num_blocks);
  const float seconds = static_cast<float>(block) * 32.0f / sample_rate;
  p->position = s.position;
  p->size = s.size;
  p->pitch = s.pitch;
  p->density = s.density;
  p->texture = s.texture;
  p->dry_wet = s.dry_wet;
  p->stereo_spread = s.spread;
  p->feedback = s.feedback;
  p->reverb = s.reverb;
  p->trigger = false;
  p->freeze = false;
  *mode = s.mode;
  if (s.freeze == FREEZE_TOGGLE) {
    p->freeze = (block & 511) > 256;
  } else if (s.freeze == FREEZE_AFTER_1S) {
    p->freeze = seconds >= 1.0f;
  }
  switch (s.sweep) {
    case SWEEP_POSITION:
      p->position = Triangle(t);
      break;
    case SWEEP_PITCH: {
      static const float kSteps[4] = { 0.0f, 7.0f, -12.0f, 12.0f };
      p->pitch = kSteps[static_cast<int>(seconds) & 3];
      break;
    }
    case SWEEP_SIZE:
      p->size = Triangle(t);
      break;
    case SWEEP_MODE:
      if (seconds >= 2.0f) *mode = GRANULAR;
      break;
    case SWEEP_DRYWET:
      p->dry_wet = t;
      break;
    case SWEEP_KNOBS:
      p->position = 0.1f + 0.8f * Triangle(t);
      p->size = 0.2f + 0.6f * Triangle(std::fmod(t * 2.0f, 1.0f));
      p->density = 0.55f + 0.4f * Triangle(std::fmod(t * 3.0f, 1.0f));
      p->texture = 0.2f + 0.75f * t;
      break;
    default:
      break;
  }
}

// Input generators. MUSIC: plucked tones (6 decaying harmonics) every
// 0.25 s, different notes left and right, with a short noise click at each
// onset; about -8 dBFS peak.
class InputGenerator {
 public:
  void Init(int input, int sample_rate) {
    input_ = input;
    sample_rate_ = sample_rate;
    n_ = 0;
    phase_ = 0.0f;
    rng_ = 12345;
  }

  void Render(int16_t* l, int16_t* r, int size) {
    for (int i = 0; i < size; ++i, ++n_) {
      if (input_ == SINE400) {
        phase_ += 400.0f / sample_rate_;
        while (phase_ >= 1.0f) phase_ -= 1.0f;
        const int16_t v = static_cast<int16_t>(16384.0f * sinf(phase_ * 6.283185307f));
        l[i] = v;
        r[i] = v;
      } else {
        l[i] = static_cast<int16_t>(Voice(0) * 32767.0f);
        r[i] = static_cast<int16_t>(Voice(1) * 32767.0f);
      }
    }
  }

 private:
  float Voice(int side) {
    static const float kNotesL[8] = { 220.0f, 261.6f, 329.6f, 392.0f, 440.0f, 392.0f, 329.6f, 293.7f };
    static const float kNotesR[8] = { 110.0f, 164.8f, 146.8f, 196.0f, 130.8f, 174.6f, 220.0f, 98.0f };
    const int note_length = sample_rate_ / 4;
    const int note = (n_ / note_length) & 7;
    const int k = n_ % note_length;
    const float f = side == 0 ? kNotesL[note] : kNotesR[(note + 3) & 7];
    const float tt = static_cast<float>(k) / sample_rate_;
    float v = 0.0f;
    for (int h = 1; h <= 6; ++h) {
      const float decay = expf(-tt * (3.0f + 2.0f * h));
      v += decay / h * sinf(6.283185307f * f * h * tt);
    }
    v *= 0.22f;
    if (k < 64) {
      rng_ = rng_ * 1664525u + 1013904223u;
      v += (static_cast<int32_t>(rng_) / 2147483648.0f) * 0.15f * (1.0f - k / 64.0f);
    }
    return v;
  }

  int input_;
  int sample_rate_;
  int n_;
  float phase_;
  uint32_t rng_;
};

}  // namespace harness

#endif  // PORTS_CLOUDS_HARNESS_SCENARIOS_H_
