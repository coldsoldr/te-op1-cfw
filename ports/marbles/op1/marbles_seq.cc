// Marbles as an OP-1 sequencer. See op1/marbles_seq.h.

#include "op1/marbles_seq.h"

#include "src/tables_fx.h"

namespace op1marbles {

const int32_t kDivisionTicks[kNumDivisions] = { 384, 192, 128, 96, 64, 48, 32, 24, 16, 12 };

namespace {

const int32_t kNone = 0x7fffffff;
const int32_t kPitchOffset = 64;        // owners_ index = pitch + 64
const int32_t kTicksPerHalfBeat = 48;
const int32_t kRootDefault = 7;         // key 7 = MIDI 60 at octave 0
const int32_t kVelocityMin = 48;
const int32_t kRange = 2 << 16;         // X and Y: 0..2 V (Q16)

// 1.0f / 689.0625f (upstream's one_hertz at the OP-1 block rate) as m x 2^e:
// only the free-running clock uses it; the sequencer's is synced.
const fx::Float24 kOneHertz = { 12466118u, -33 };

int32_t Clamp16(int32_t v) { return v < 0 ? 0 : (v > 65535 ? 65535 : v); }

int32_t DejaVu(int32_t mode, int32_t amount) {
  return mode == DEJA_VU_LOCKED ? fx::kQ16One / 2 : (mode == DEJA_VU_ON ? amount : 0);
}

int32_t PopCount(uint32_t x) {
  int32_t n = 0;
  for (; x; x &= x - 1) ++n;
  return n;
}

}  // namespace

void MarblesSeq::Init(uint32_t seed) {
  settings_.page = PAGE_T;
  settings_.pitch_source = PITCH_SCALE;
  settings_.t_model = fx::T_GENERATOR_MODEL_COMPLEMENTARY_BERNOULLI;
  settings_.scale = 0;
  settings_.t_deja_vu = DEJA_VU_OFF;
  settings_.x_deja_vu = DEJA_VU_OFF;
  settings_.rate = 0x8000;      // 1/8
  settings_.t_bias = 0x8000;
  settings_.jitter = 0;
  settings_.deja_vu = 0x8000;
  settings_.spread = 0x8000;
  settings_.x_bias = 0x8000;
  settings_.steps = 0x8000;     // Marbles' 0.75: the scale's main notes
  settings_.length = 0x7800;    // 8 steps

  random_generator_.Init(seed);
  random_stream_.Init(&random_generator_);
  t_generator_.Init(&random_stream_, kOneHertz);
  xy_generator_.Init(&random_stream_);
  for (int i = 0; i < kNumScales; ++i) xy_generator_.LoadScale(i, fx::preset_scales[i]);
  reset_ = true;

  abs_tick_ = 0xffffffffu;   // the first tick is tick 0 (+ its phase)
  synced_ = false;
  period_ = 0;
  t_offset_ = 0;
  running_ = false;
  held_keys_ = pool_mask_ = 0;
  root_ = kRootDefault;
  latched_ = shift_ = restart_ = false;
  through_on_ = through_off_ = through_sounding_ = 0;
  gates_ = 0;
  for (int i = 0; i < fx::kNumChannels; ++i) voltages_[i] = 0;
  for (int i = 0; i < 3; ++i) sounding_[i] = kNone;
  for (int i = 0; i < 256; ++i) owners_[i] = 0;
  for (int lane = 0; lane < 3; ++lane) {
    history_gates_[lane] = 0;
    for (int c = 0; c < kHistory; ++c) history_height_[lane][c] = 0;
  }
}

int32_t MarblesSeq::DivisionIndex() const {
  const int32_t i = settings_.rate * kNumDivisions >> 16;
  return i > kNumDivisions - 1 ? kNumDivisions - 1 : i;
}

int32_t MarblesSeq::length_steps() const {
  const int32_t i = settings_.length * kMaxLength >> 16;
  return 1 + (i > kMaxLength - 1 ? kMaxLength - 1 : i);
}

void MarblesSeq::KeyDown(int32_t key, bool play_through) {
  if (key < 0 || key >= kNumKeys) return;
  const uint32_t bit = 1u << key;
  if (play_through) {
    through_on_ = through_on_ | bit;
    return;
  }
  // The transport: a press with no key held starts a new chord; it also
  // (re)starts the run unless one is going (held or latched); it always
  // clears the latch, so the chord it starts ends on its last key up.
  const bool was_running = held_keys_ != 0 || latched_;
  latched_ = false;
  if (!held_keys_) {
    pool_mask_ = 0;   // a new chord
    if (!was_running) restart_ = true;
  }
  held_keys_ = held_keys_ | bit;
  pool_mask_ = pool_mask_ | bit;
  root_ = key;
}

void MarblesSeq::KeyUp(int32_t key) {
  if (key < 0 || key >= kNumKeys) return;
  const uint32_t bit = 1u << key;
  held_keys_ = held_keys_ & ~bit;
  if ((through_sounding_ | through_on_) & bit) through_off_ = through_off_ | bit;
}

int32_t MarblesSeq::Pitch(int32_t voltage, int32_t* height) const {
  int32_t v = voltage < 0 ? 0 : (voltage > kRange ? kRange : voltage);
  if (settings_.pitch_source == PITCH_SCALE) {
    const int32_t semitones = (v * 12 + 32768) >> 16;   // 0..24
    *height = semitones * 255 / 24;
    return root_ + semitones;
  }
  const uint32_t pool = pool_mask_;
  const int32_t count = PopCount(pool);
  if (!count) return kNone;
  int32_t index = v * count / kRange;   // v <= 2^17, count <= 24: 32 bits
  if (index > count - 1) index = count - 1;
  *height = count > 1 ? index * 255 / (count - 1) : 0;
  for (int32_t k = 0; k < kNumKeys; ++k) {
    if ((pool >> k) & 1) {
      if (index-- == 0) return k;
    }
  }
  return kNone;
}

void MarblesSeq::Release(int voice, NoteSink* sink) {
  const int32_t pitch = sounding_[voice];
  if (pitch == kNone) return;
  sounding_[voice] = kNone;
  uint8_t& owners = owners_[pitch + kPitchOffset];
  owners &= ~(1 << voice);
  if (!owners) sink->NoteOff(pitch);
}

void MarblesSeq::Tick(int32_t t, NoteSink* sink) {
  // Played-through keys first: their offs, then their ons (they sound
  // whether or not the run is going).
  const uint32_t offs = through_off_;
  if (offs) {
    through_off_ = through_off_ & ~offs;
    for (int32_t k = 0; k < kNumKeys; ++k) {
      if ((offs >> k) & 1 && (through_sounding_ >> k) & 1) sink->NoteOff(k);
    }
    through_sounding_ &= ~offs;
  }
  const uint32_t ons = through_on_;
  if (ons) {
    through_on_ = through_on_ & ~ons;
    for (int32_t k = 0; k < kNumKeys; ++k) {
      if ((ons >> k) & 1) {
        if ((through_sounding_ >> k) & 1) sink->NoteOff(k);
        sink->NoteOn(k, 127);
      }
    }
    through_sounding_ |= ons;
  }

  // The transport: shift pressed while keys are held latches the run.
  if (shift_ && held_keys_) latched_ = true;
  const bool running = held_keys_ != 0 || latched_;
  if (restart_) {
    // The run starts on this tick: the grid, the clock and the loops from
    // here (the tape armed on the first key starts with the sequence).
    restart_ = false;
    t_offset_ = t;
    abs_tick_ = 0xffffffffu;
    synced_ = false;
    reset_ = true;
  }
  if (!running) {
    for (int voice = 0; voice < 3; ++voice) Release(voice, sink);
    gates_ = 0;
    running_ = false;
    return;
  }
  running_ = true;

  // The tick's place on the grid: t is its phase in half a beat, counted
  // from the key press that started the run. Longer divisions (a beat and
  // up, triplets) are counted from that tick too: the firmware's bar
  // position isn't known.
  t = (t - t_offset_ + kTicksPerHalfBeat) % kTicksPerHalfBeat;
  abs_tick_ += 1;
  const int32_t phase = static_cast<int32_t>(abs_tick_ % kTicksPerHalfBeat);
  if (phase != t) abs_tick_ += static_cast<uint32_t>((t - phase + kTicksPerHalfBeat) % kTicksPerHalfBeat);
  const int32_t period = division_ticks();
  if (!synced_ || period != period_) {
    t_generator_.SyncTo(static_cast<int32_t>(abs_tick_ % static_cast<uint32_t>(period)), period);
    period_ = period;
    synced_ = true;
  }

  const Settings& s = settings_;
  const int32_t length = length_steps();
  t_generator_.set_synced_period(period);
  t_generator_.set_model(static_cast<fx::TGeneratorModel>(s.t_model));
  t_generator_.set_bias(s.t_bias);
  t_generator_.set_jitter(s.jitter);
  t_generator_.set_deja_vu(DejaVu(s.t_deja_vu, s.deja_vu));
  t_generator_.set_length(length);
  t_generator_.set_pulse_width_mean(fx::kQ16One / 2);
  t_generator_.set_pulse_width_std(0);

  x_.control_mode = fx::CONTROL_MODE_IDENTICAL;
  x_.voltage_range = fx::VOLTAGE_RANGE_NARROW;
  x_.spread = s.spread;
  x_.bias = s.x_bias;
  // The encoder covers Marbles' quantized half only (0.5..1: all 12 notes
  // -> the scale -> its main notes -> the root): the smooth half glides
  // a voltage, which as notes only repeats one pitch.
  x_.steps = fx::kQ16One / 2 + (s.steps >> 1);
  x_.deja_vu = DejaVu(s.x_deja_vu, s.deja_vu);
  x_.scale_index = s.scale;
  x_.length = length;
  x_.ratio.p = 1;
  x_.ratio.q = 1;
  y_ = x_;
  y_.spread = fx::kQ16One / 2;
  y_.bias = fx::kQ16One / 2;
  y_.steps = 0;
  y_.deja_vu = 0;
  y_.length = 1;

  uint32_t ramp[4];
  bool gate[2];
  fx::Ramps ramps;
  ramps.master = &ramp[0];
  ramps.external = &ramp[1];
  ramps.slave[0] = &ramp[2];
  ramps.slave[1] = &ramp[3];
  bool t_reset = reset_;
  t_generator_.Process(&t_reset, ramps, gate, 1);
  ramps.master = &ramp[0];
  ramps.external = &ramp[1];
  ramps.slave[0] = &ramp[2];
  ramps.slave[1] = &ramp[3];
  bool x_reset = reset_;
  xy_generator_.Process(fx::CLOCK_SOURCE_INTERNAL_T1_T2_T3, x_, y_, &x_reset, ramps, voltages_, 1);
  reset_ = false;

  // Voice i: T1, T2 (the master clock's first half), T3.
  const uint8_t gates = (gate[0] ? 1 : 0) | (ramp[0] < 0x80000000u ? 2 : 0) | (gate[1] ? 4 : 0);
  const uint8_t falling = gates_ & ~gates;
  const uint8_t rising = gates & ~gates_;
  gates_ = gates;

  for (int voice = 0; voice < 3; ++voice) {
    if ((falling | rising) >> voice & 1) Release(voice, sink);
  }
  int32_t y = voltages_[3] < 0 ? 0 : (voltages_[3] > kRange ? kRange : voltages_[3]);
  const int32_t velocity = kVelocityMin + y * (127 - kVelocityMin) / kRange;
  // The history: a new column on each clock (T2) step; this step's notes
  // (and the T1/T3 notes until the next one) are marked in it.
  if (rising & 2) {
    for (int lane = 0; lane < 3; ++lane) {
      history_gates_[lane] = static_cast<uint16_t>(history_gates_[lane] >> 1);
      for (int c = 0; c < kHistory - 1; ++c) history_height_[lane][c] = history_height_[lane][c + 1];
    }
  }
  for (int voice = 0; voice < 3; ++voice) {
    if (!(rising >> voice & 1)) continue;
    int32_t height = 0;
    const int32_t pitch = Pitch(voltages_[voice], &height);
    if (pitch == kNone || pitch < -kPitchOffset || pitch > 255 - kPitchOffset) continue;
    history_height_[voice][kHistory - 1] = static_cast<uint8_t>(height);
    history_gates_[voice] = static_cast<uint16_t>(history_gates_[voice] | (1u << (kHistory - 1)));
    uint8_t& owners = owners_[pitch + kPitchOffset];
    if (!owners) sink->NoteOn(pitch, velocity);
    owners |= 1 << voice;
    sounding_[voice] = pitch;
  }
}

void MarblesSeq::AllNotesOff(NoteSink* sink) {
  for (int voice = 0; voice < 3; ++voice) Release(voice, sink);
  for (int32_t k = 0; k < kNumKeys; ++k) {
    if ((through_sounding_ >> k) & 1) sink->NoteOff(k);
  }
  through_sounding_ = 0;
  through_on_ = through_off_ = 0;
  gates_ = 0;
  latched_ = false;   // the keys still held keep it running; a latch doesn't
  running_ = false;
}

void MarblesSeq::Knob(int32_t k, int32_t delta_q16) {
  Settings& s = settings_;
  int32_t* value;
  if (s.page == PAGE_T) {
    // Green holds the deja vu controls, orange the bias (the user's colour
    // scheme: everything deja vu is green).
    int32_t* const t_values[4] = { &s.rate, &s.deja_vu, &s.jitter, &s.t_bias };
    value = t_values[k & 3];
  } else {
    int32_t* const x_values[4] = { &s.spread, &s.length, &s.steps, &s.x_bias };
    value = x_values[k & 3];
  }
  *value = Clamp16(*value + delta_q16);
}

void MarblesSeq::OnClick(Click click, int32_t direction) {
  Settings& s = settings_;
  switch (click) {
    case CLICK_BLUE:
      if (s.page == PAGE_T) {
        s.t_model = (s.t_model + direction + kNumTModels) % kNumTModels;
      } else {
        s.scale = (s.scale + direction + kNumScales) % kNumScales;
      }
      break;
    case CLICK_GREEN: {
      int32_t& mode = s.page == PAGE_T ? s.t_deja_vu : s.x_deja_vu;
      mode = (mode + (direction < 0 ? 2 : 1)) % 3;
      break;
    }
    case CLICK_WHITE:
      if (direction < 0) {
        s.page ^= 1;
      } else {
        s.pitch_source ^= 1;
      }
      break;
  }
}

}  // namespace op1marbles
