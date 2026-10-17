// Deterministic test material for the Streams port, synthesised with integer
// arithmetic only, so host and Blackfin builds produce the same samples (and
// the simulator doesn't spend its time in soft-float). Any sample rate:
// copy_check renders at upstream's 31089 Hz, the port at 44.1 kHz.
//
// Materials (120 bpm, 16th-note grid):
//   DRUMS  kick (pitch-swept sine), snare (tone + noise), closed and open hats,
//          velocities from a pattern; peaks about -3 dBFS
//   RIFF   a saw-wave bass/lead line: legato and staccato notes, accents
//   PAD    a three-voice saw chord, slow attack, changing every 2 s
//   MIX    drums + pad
// Stereo: hats panned right, snare a little left, pad detuned per side.

#ifndef OP1STREAMS_HARNESS_SIGNALS_H_
#define OP1STREAMS_HARNESS_SIGNALS_H_

#include <stdint.h>

namespace harness {

enum Material { MAT_DRUMS, MAT_RIFF, MAT_PAD, MAT_MIX, MAT_LAST };

inline const char* MaterialName(Material m) {
  static const char* const kNames[] = { "drums", "riff", "pad", "mix" };
  return kNames[m];
}

// Parabolic sine of a 32-bit phase, about +-32767.
inline int32_t ParabolicSine(uint32_t phase) {
  int32_t x = static_cast<int16_t>(phase >> 16);
  // GCC 14 for Blackfin compiles the abs of an int16-range value to the
  // saturating 16-bit `ABS (V)`: |-32768| = 32767 there, 32768 on the host.
  if (x == -32768) return 0;
  int32_t ax = x < 0 ? -x : x;
  int32_t y = (x * (32768 - ax)) >> 13;
  return y > 32767 ? 32767 : (y < -32767 ? -32767 : y);
}

// Exponential decay per sample, env in Q30: env -= env / (tau * sr).
struct Decay {
  int32_t env;
  int32_t k;  // Q30 fraction removed per sample
  void Set(int32_t sample_rate, int32_t tau_ms) {
    int32_t samples = sample_rate / 1000 * tau_ms;
    if (samples < 1) samples = 1;
    k = (1 << 30) / samples;
  }
  void Trigger(int32_t level_q30) { env = level_q30; }
  int32_t Next() {
    env -= static_cast<int32_t>((static_cast<int64_t>(env) * k) >> 30);
    return env;
  }
};

class Signal {
 public:
  void Init(Material material, int32_t sample_rate, bool stereo) {
    material_ = material;
    sr_ = sample_rate;
    stereo_ = stereo;
    n_ = 0;
    rng_ = 0x12345678u;
    step_length_ = sample_rate / 8;  // 16th notes at 120 bpm
    step_ = -1;
    kick_amp_.Set(sr_, 220); kick_pitch_.Set(sr_, 35);
    snare_tone_.Set(sr_, 60); snare_noise_.Set(sr_, 90);
    hat_.Set(sr_, 18); open_hat_.Set(sr_, 140);
    kick_amp_.env = kick_pitch_.env = snare_tone_.env = snare_noise_.env = 0;
    hat_.env = open_hat_.env = 0;
    kick_phase_ = snare_phase_ = 0;
    kick_base_ = Increment(30);
    snare_increment_ = Increment(54);
    hp_l_ = hp_r_ = 0;
    snare_lp_ = 0;
    riff_phase_ = 0; riff_lp_ = 0; riff_env_ = 0; riff_gate_ = 0;
    riff_increment_ = 0; riff_accent_ = 0;
    for (int i = 0; i < 6; ++i) { pad_phase_[i] = static_cast<uint32_t>(i) * 0x2468ace1u; }
    pad_env_ = 0; pad_lp_l_ = pad_lp_r_ = 0;
  }

  // One sample, int16 range.
  void Next(int16_t* l, int16_t* r) {
    if (n_ % step_length_ == 0) {
      ++step_;
      Step(step_);
    }
    int32_t sl = 0, sr = 0;
    switch (material_) {
      case MAT_DRUMS: Drums(&sl, &sr); break;
      case MAT_RIFF: Riff(&sl, &sr); break;
      case MAT_PAD: Pad(&sl, &sr); break;
      default: {
        int32_t dl, dr, pl, pr;
        Drums(&dl, &dr);
        Pad(&pl, &pr);
        sl = (dl * 5 >> 3) + (pl * 3 >> 3);
        sr = (dr * 5 >> 3) + (pr * 3 >> 3);
      }
    }
    if (!stereo_) sr = sl;
    *l = Clip(sl);
    *r = Clip(sr);
    ++n_;
  }

 private:
  static int16_t Clip(int32_t x) {
    return x > 32767 ? 32767 : (x < -32768 ? -32768 : x);
  }

  uint32_t Rand() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return rng_;
  }

  int32_t Noise() { return static_cast<int16_t>(Rand() >> 16); }

  // Phase increment for MIDI note (12..108) at this rate.
  uint32_t Increment(int32_t note) {
    // 2^32 * 8.1758 Hz (MIDI 0) / sr, times 2^(note/12) from a semitone table.
    static const uint32_t kSemitoneQ16[12] = {
      65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032,
      110218, 116772, 123715 };
    int32_t octave = note / 12;
    uint64_t f_q16 = static_cast<uint64_t>(535809) * kSemitoneQ16[note % 12] >> 16;
    // 535809 = 8.1758 Hz in Q16.
    f_q16 <<= octave;
    return static_cast<uint32_t>((f_q16 << 16) / static_cast<uint32_t>(sr_));
  }

  void Step(int32_t step) {
    int32_t s = step & 15;
    int32_t bar = (step >> 4) & 3;
    // Drums.
    static const uint8_t kKick[16] = { 9,0,0,0, 0,0,0,0, 8,0,6,0, 0,0,0,0 };
    static const uint8_t kSnare[16] = { 0,0,0,0, 9,0,0,0, 0,0,0,0, 9,0,0,3 };
    static const uint8_t kHat[16] = { 6,0,4,0, 6,0,4,0, 6,0,4,0, 6,0,4,5 };
    if (kKick[s]) { kick_amp_.Trigger(kKick[s] << 26); kick_pitch_.Trigger(1 << 30); }
    if (kSnare[s]) { snare_tone_.Trigger(kSnare[s] << 26); snare_noise_.Trigger(kSnare[s] << 26); }
    if (kHat[s]) {
      if (s == 14 && (bar & 1)) open_hat_.Trigger(kHat[s] << 26);
      else hat_.Trigger(kHat[s] << 26);
    }
    // Riff: note (0 = rest), gate length in 16ths/4, accent.
    static const int8_t kNotes[16] = { 36,36,48,0, 43,0,46,48, 36,0,39,41, 43,43,46,51 };
    static const uint8_t kGate[16] = { 1,1,3,0, 2,0,1,4, 3,0,1,1, 4,1,2,3 };
    int32_t note = kNotes[s];
    if (note) {
      note += (bar == 2 ? 5 : (bar == 3 ? -2 : 0));
      riff_increment_ = Increment(note);
      riff_gate_ = step_length_ * kGate[s] / 4;
      riff_accent_ = (s % 4 == 0) ? 1 : 0;
    }
    // Pad: chord every 2 s (16 steps).
    if (s == 0) {
      static const int8_t kChords[4][3] = { {48,52,55}, {45,48,52}, {41,45,48}, {43,47,50} };
      for (int i = 0; i < 3; ++i) {
        uint32_t inc = Increment(kChords[bar][i]);
        pad_increment_[i] = inc + (inc >> 9);        // +3.4 cents
        pad_increment_[i + 3] = inc - (inc >> 9);    // -3.4 cents
      }
    }
  }

  void Drums(int32_t* l, int32_t* r) {
    // Kick: 45 Hz + 110 Hz * pitch envelope.
    int32_t pe = kick_pitch_.Next();
    uint32_t base = kick_base_;  // ~46 Hz
    kick_phase_ += base + static_cast<uint32_t>((static_cast<int64_t>(base) * 2 * pe) >> 30);
    int32_t kick = static_cast<int32_t>(
        (static_cast<int64_t>(ParabolicSine(kick_phase_)) * kick_amp_.Next()) >> 30);
    // Snare: 190 Hz tone + band-limited noise.
    snare_phase_ += snare_increment_;
    int32_t noise = Noise();
    snare_lp_ += (noise - snare_lp_) >> 1;
    int32_t snare = static_cast<int32_t>(
        ((static_cast<int64_t>(ParabolicSine(snare_phase_)) * snare_tone_.Next()) >> 31) +
        ((static_cast<int64_t>(snare_lp_) * snare_noise_.Next()) >> 30));
    // Hats: high-passed noise.
    int32_t n2 = Noise();
    int32_t hp = n2 - hp_l_;
    hp_l_ = n2;
    int32_t hat = static_cast<int32_t>(
        (static_cast<int64_t>(hp >> 1) * (hat_.Next() + (open_hat_.Next() >> 1))) >> 30);
    int32_t center = (kick * 3 >> 2) + (snare * 9 >> 4);
    if (stereo_) {
      *l = center + (snare >> 4) + (hat >> 2);
      *r = center - (snare >> 4) + (hat >> 1);
    } else {
      *l = *r = center + (hat >> 1);
    }
  }

  void Riff(int32_t* l, int32_t* r) {
    // Envelope: attack ~2 ms, sustain 70%, release ~25 ms. Q15.
    int32_t target = 0;
    if (riff_gate_ > 0) {
      --riff_gate_;
      target = riff_accent_ ? 32767 : 24000;
    }
    int32_t attack_k = 32768 / (sr_ / 500);   // ~2 ms
    int32_t release_k = 32768 / (sr_ / 40);   // ~25 ms
    if (target > riff_env_) riff_env_ += ((target - riff_env_) * attack_k) >> 15;
    else riff_env_ += ((target - riff_env_) * release_k) >> 15;
    riff_phase_ += riff_increment_;
    int32_t saw = static_cast<int16_t>(riff_phase_ >> 16);
    riff_lp_ += (saw - riff_lp_) >> 1;
    int32_t y = (riff_lp_ * riff_env_ >> 15) * 3 >> 2;
    *l = *r = y;
  }

  void Pad(int32_t* l, int32_t* r) {
    if (pad_env_ < (1 << 30)) pad_env_ += (1 << 30) / (sr_ / 2);  // 0.5 s swell
    int32_t sl = 0, sr = 0;
    for (int i = 0; i < 3; ++i) {
      pad_phase_[i] += pad_increment_[i];
      pad_phase_[i + 3] += pad_increment_[i + 3];
      sl += static_cast<int16_t>(pad_phase_[i] >> 16);
      sr += static_cast<int16_t>(pad_phase_[i + 3] >> 16);
    }
    if (!stereo_) sl = sr = (sl + sr) >> 1;
    pad_lp_l_ += (sl - pad_lp_l_) >> 3;
    pad_lp_r_ += (sr - pad_lp_r_) >> 3;
    int32_t g = pad_env_ >> 15;  // Q15
    *l = (pad_lp_l_ * g >> 15) >> 1;
    *r = (pad_lp_r_ * g >> 15) >> 1;
  }

  Material material_;
  int32_t sr_;
  bool stereo_;
  int32_t n_;
  uint32_t rng_;
  int32_t step_length_;
  int32_t step_;
  Decay kick_amp_, kick_pitch_, snare_tone_, snare_noise_, hat_, open_hat_;
  uint32_t kick_phase_, snare_phase_, kick_base_, snare_increment_;
  int32_t hp_l_, hp_r_, snare_lp_;
  uint32_t riff_phase_, riff_increment_;
  int32_t riff_lp_, riff_env_, riff_gate_, riff_accent_;
  uint32_t pad_phase_[6], pad_increment_[6];
  int32_t pad_env_, pad_lp_l_, pad_lp_r_;
};

}  // namespace harness

#endif  // OP1STREAMS_HARNESS_SIGNALS_H_
