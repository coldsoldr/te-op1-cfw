// Grids as an OP-1 drum sequencer: see drum_sequencer.h.
// Part of the Grids port; Grids (c) 2011-2012 Emilie Gillet, GPL v3.

#include "src/drum_sequencer.h"

namespace op1grids {

void DrumSequencer::Init(uint16_t seed) {
  generator_.Init(seed);
  settings_.mode = OUTPUT_MODE_DRUMS;
  settings_.x = 128;
  settings_.y = 128;
  settings_.chaos = 0;
  for (uint8_t i = 0; i < 3; ++i) {
    settings_.density[i] = 128;
    settings_.euclid_length[i] = 15 << 3;  // 16 steps
  }
  settings_.swing = 0;
  settings_.resolution = RESOLUTION_32ND;
  // Default keys: the shipped demo drum pattern (op1.db drumseq, is_synth 0)
  // uses key 0 on the downbeats, key 2 on beats 2 and 4 and key 8 as the
  // hat line [I: to confirm on the device with the factory kits].
  settings_.key[0] = 0;
  settings_.key[1] = 2;
  settings_.key[2] = 8;
  settings_.key[3] = kNoKey;
  settings_.accent_key[0] = 1;
  settings_.accent_key[1] = 3;
  settings_.accent_key[2] = 9;
  settings_.accent_mode = ACCENT_VELOCITY;
  settings_.velocity = 90;
  settings_.accent_velocity = 127;
  settings_.gate_ticks = 6;
  for (uint8_t v = 0; v < kNumVoices; ++v) {
    voice_key_[v] = kNoKey;
    voice_gate_[v] = 0;
  }
  last_state_ = 0;
  step_count_ = 0;
  LatchTiming();
  Start();
}

void DrumSequencer::Start() {
  generator_.Reset();
  next_onset_ = 4;
  waiting_ = true;
}

void DrumSequencer::LatchTiming() {
  // Upstream: swing_amount() = randomness * 43 >> 8 (0..42); clock.Wrap()
  // makes the first two 32nd periods of each eighth (128 + v) / 128 long and
  // the last two (128 - v) / 128.
  uint16_t v = static_cast<uint16_t>((settings_.swing * 43) >> 8);
  onset_[0] = 0;
  onset_[1] = static_cast<uint8_t>((12 * (128 + v) + 64) >> 7);
  onset_[2] = static_cast<uint8_t>((24 * (128 + v) + 64) >> 7);
  onset_[3] = static_cast<uint8_t>(onset_[2] + ((12 * (128 - v) + 64) >> 7));
  resolution_ = settings_.resolution;
}

void DrumSequencer::ApplySettings() {
  Settings* s = generator_.mutable_settings();
  s->x = settings_.x;
  s->y = settings_.y;
  s->randomness = settings_.chaos;
  for (uint8_t i = 0; i < 3; ++i) {
    s->density[i] = settings_.density[i];
    s->euclidean_length[i] = settings_.euclid_length[i];
  }
  generator_.set_output_mode(settings_.mode);
}

void DrumSequencer::Trigger(uint8_t voice, uint8_t key, uint8_t velocity,
                            NoteEvent* out, uint8_t* n) {
  if (key == kNoKey) {
    return;
  }
  if (voice_gate_[voice]) {
    out[*n].key = voice_key_[voice];
    out[*n].velocity = 0;
    out[*n].on = 0;
    ++*n;
  }
  out[*n].key = key;
  out[*n].velocity = velocity;
  out[*n].on = 1;
  ++*n;
  voice_key_[voice] = key;
  voice_gate_[voice] = settings_.gate_ticks ? settings_.gate_ticks : 1;
}

uint8_t DrumSequencer::Tick(uint8_t t, NoteEvent* out) {
  uint8_t n = 0;
  if (t == 0) {
    LatchTiming();
    waiting_ = false;
    next_onset_ = 0;
  }

  for (uint8_t v = 0; v < kNumVoices; ++v) {
    if (voice_gate_[v] && --voice_gate_[v] == 0) {
      out[n].key = voice_key_[v];
      out[n].velocity = 0;
      out[n].on = 0;
      ++n;
    }
  }

  if (waiting_ || next_onset_ >= 4 || t != onset_[next_onset_]) {
    return n;
  }
  uint8_t index = next_onset_++;
  if (resolution_ == RESOLUTION_16TH && (index & 1)) {
    return n;
  }

  ApplySettings();
  generator_.TickClock(resolution_ == RESOLUTION_16TH ? 6 : 3);
  uint8_t state = generator_.state();
  last_state_ = state;
  ++step_count_;

  // Bits 3..5: per-part accents (drums) or cycle starts (Euclidean), the
  // module's accent / reset outputs. Both play as accents.
  for (uint8_t part = 0; part < 3; ++part) {
    if (!(state & (1 << part))) {
      continue;
    }
    bool accent = state & (8 << part);
    uint8_t key = settings_.key[part];
    uint8_t velocity = settings_.velocity;
    if (accent) {
      velocity = settings_.accent_velocity;
      if (settings_.accent_mode == ACCENT_KEY) {
        key = settings_.accent_key[part];
      }
    }
    Trigger(part, key, velocity, out, &n);
  }
  if (state & 0x80) {
    Trigger(3, settings_.key[3], settings_.velocity, out, &n);
  }
  return n;
}

uint8_t DrumSequencer::AllNotesOff(NoteEvent* out) {
  uint8_t n = 0;
  for (uint8_t v = 0; v < kNumVoices; ++v) {
    if (voice_gate_[v]) {
      out[n].key = voice_key_[v];
      out[n].velocity = 0;
      out[n].on = 0;
      ++n;
      voice_gate_[v] = 0;
    }
  }
  return n;
}

}  // namespace op1grids
