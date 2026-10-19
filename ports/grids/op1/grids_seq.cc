// Grids as the OP-1's drum sequencer: the controls. See op1/grids_seq.h.
// Part of the Grids port; Grids (c) 2011-2012 Emilie Gillet, GPL v3.

#include "op1/grids_seq.h"

namespace op1grids {

namespace {

const int32_t kNumKeys = 24;
const uint8_t kVelocity = 90;

int32_t Clamp16(int32_t v) { return v < 0 ? 0 : (v > 65535 ? 65535 : v); }
uint8_t To8(int32_t q16) { return static_cast<uint8_t>(q16 >> 8); }

}  // namespace

void GridsSeq::Init(uint16_t seed) {
  sequencer_.Init(seed);
  seed_ = seed;
  ui_.x = ui_.y = 0x8000;
  ui_.chaos = 0;
  ui_.swing = 0;
  for (int i = 0; i < 3; ++i) {
    ui_.fill[i] = 0x8000;
    ui_.length[i] = 15 << 11;   // 16 steps: (v >> 3) + 1 on the 8-bit value
    ui_.accent_key[i] = kNoKey;
  }
  ui_.accent = 0xffff;
  ui_.mode = OUTPUT_MODE_DRUMS;
  ui_.resolution = RESOLUTION_16TH;
  ui_.assign = ASSIGN_OFF;
  ui_.random_voice = 0;
  const DrumSeqSettings* d = sequencer_.mutable_settings();
  for (int i = 0; i < 3; ++i) ui_.key[i] = d->key[i];   // 0, 2, 8
  step_base_ = sequencer_.step_count();
  step_ = 0;
  for (int i = 0; i < kPrintSteps; ++i) recording_[i] = bar_[i] = 0;
  held_parts_ = held_keys_ = through_on_ = through_off_ = through_sounding_ = 0;
  latched_ = shift_ = restart_ = false;
  running_ = false;
  t_offset_ = 0;
  for (int i = 0; i < 32; ++i) sources_[i] = 0;
  shown_state_ = 0;
  Apply();
}

void GridsSeq::On(int32_t key, int32_t velocity, NoteOut* out) {
  if (key < 0 || key >= 32) return;
  if (sources_[key]) out->NoteOff(key);   // retrigger
  out->NoteOn(key, velocity);
  if (sources_[key] < 255) ++sources_[key];
}

void GridsSeq::Off(int32_t key, NoteOut* out) {
  if (key < 0 || key >= 32 || !sources_[key]) return;
  if (--sources_[key] == 0) out->NoteOff(key);
}

// The UI settings and held keys onto DrumSequencer's (read at its next step).
void GridsSeq::Apply() {
  DrumSeqSettings* d = sequencer_.mutable_settings();
  d->mode = ui_.mode;
  d->x = To8(ui_.x);
  d->y = To8(ui_.y);
  d->chaos = To8(ui_.chaos);
  d->swing = To8(ui_.swing);
  d->resolution = ui_.resolution;
  for (int i = 0; i < 3; ++i) {
    d->density[i] = To8(ui_.fill[i]);
    d->euclid_length[i] = To8(ui_.length[i]);
    d->key[i] = ui_.key[i];
    // Accents always go through the accent key, which is the part's own
    // key until one is assigned: louder on the same key by default.
    d->accent_key[i] = ui_.accent_key[i] == kNoKey ? ui_.key[i] : ui_.accent_key[i];
  }
  d->key[3] = ui_.random_voice ? kRandomKey : kNoKey;
  d->accent_mode = ACCENT_KEY;
  d->velocity = kVelocity;
  d->accent_velocity = static_cast<uint8_t>(kVelocity + ((127 - kVelocity) * ui_.accent >> 16));
}

void GridsSeq::KeyDown(int32_t key) {
  if (key < 0 || key >= kNumKeys) return;
  const uint8_t k = static_cast<uint8_t>(key);
  switch (ui_.assign) {
    case ASSIGN_BD:
    case ASSIGN_SD:
    case ASSIGN_HH:
      ui_.key[ui_.assign - ASSIGN_BD] = k;
      through_on_ = through_on_ | (1u << key);   // audition the new sound
      return;
    case ASSIGN_BD_ACCENT:
    case ASSIGN_SD_ACCENT:
    case ASSIGN_HH_ACCENT: {
      const int part = ui_.assign - ASSIGN_BD_ACCENT;
      ui_.accent_key[part] = k == ui_.key[part] ? kNoKey : k;
      through_on_ = through_on_ | (1u << key);
      return;
    }
    default:
      break;
  }
  // The transport: a press with no key held starts the run (its bar from
  // this press) unless one is going (held or latched); it always clears
  // the latch, so the run ends on its last key up.
  const bool was_running = held_keys_ != 0 || latched_;
  latched_ = false;
  if (!held_keys_ && !was_running) restart_ = true;
  held_keys_ = held_keys_ | (1u << key);
}

void GridsSeq::KeyUp(int32_t key) {
  if (key < 0 || key >= kNumKeys) return;
  held_keys_ = held_keys_ & ~(1u << key);
  if ((through_sounding_ | through_on_) & (1u << key)) through_off_ = through_off_ | (1u << key);
}

void GridsSeq::Tick(int32_t t, NoteOut* out) {
  // Played-through keys: offs, then ons.
  const uint32_t offs = through_off_;
  if (offs) {
    through_off_ = through_off_ & ~offs;
    for (int32_t k = 0; k < kNumKeys; ++k) {
      if ((offs >> k) & 1 && (through_sounding_ >> k) & 1) Off(k, out);
    }
    through_sounding_ &= ~offs;
  }
  const uint32_t ons = through_on_;
  if (ons) {
    through_on_ = through_on_ & ~ons;
    for (int32_t k = 0; k < kNumKeys; ++k) {
      if ((ons >> k) & 1) {
        if ((through_sounding_ >> k) & 1) Off(k, out);
        On(k, 127, out);
      }
    }
    through_sounding_ |= ons;
  }

  // The transport: shift pressed while keys are held latches the run.
  if (shift_ && held_keys_) latched_ = true;
  const bool running = held_keys_ != 0 || latched_;
  if (restart_) {
    // The bar starts on this tick (the tape armed on the first key starts
    // with it).
    restart_ = false;
    t_offset_ = t;
    sequencer_.Start();
    step_base_ = sequencer_.step_count();
  }
  if (!running) {
    if (running_) AllNotesOff(out);
    running_ = false;
    return;
  }
  running_ = true;
  t = (t - t_offset_ + 48) % 48;

  Apply();
  const uint32_t count = sequencer_.step_count();
  NoteEvent events[kMaxEvents];
  const uint8_t n = sequencer_.Tick(static_cast<uint8_t>(t), events);
  for (uint8_t i = 0; i < n; ++i) {
    if (events[i].on) {
      On(events[i].key, events[i].velocity, out);
    } else {
      Off(events[i].key, out);
    }
  }
  if (sequencer_.step_count() != count) {
    // A step played: record its keys for print (32nds fold into 16ths).
    const int32_t steps = ui_.resolution == RESOLUTION_16TH ? 16 : 32;
    step_ = static_cast<int32_t>((sequencer_.step_count() - 1 - step_base_) % static_cast<uint32_t>(steps));
    const int32_t sixteenth = steps == 16 ? step_ : step_ >> 1;
    shown_state_ = sequencer_.last_state();
    if (step_ == 0) {
      for (int i = 0; i < kPrintSteps; ++i) {
        bar_[i] = recording_[i];
        recording_[i] = 0;
      }
    }
    uint32_t mask = 0;
    for (uint8_t i = 0; i < n; ++i) {
      if (events[i].on && events[i].key < kNumKeys) mask |= 1u << events[i].key;
    }
    recording_[sixteenth] |= mask;
  }
}

void GridsSeq::AllNotesOff(NoteOut* out) {
  NoteEvent events[kMaxEvents];
  sequencer_.AllNotesOff(events);   // its voices end here, with the rest
  for (int32_t k = 0; k < 32; ++k) {
    if (sources_[k]) out->NoteOff(k);
    sources_[k] = 0;
  }
  through_sounding_ = 0;
  through_on_ = through_off_ = 0;
  shown_state_ = 0;
  latched_ = false;   // the keys still held keep it running; a latch doesn't
  running_ = false;
  // The next bar starts at the next t == 0 of the run.
  sequencer_.Start();
  step_base_ = sequencer_.step_count();
}

void GridsSeq::Knob(int32_t k, int32_t delta_q16) {
  int32_t* value;
  const bool euclid = ui_.mode == OUTPUT_MODE_EUCLIDEAN;
  if (k < 4) {
    int32_t* const drums[4] = { &ui_.x, &ui_.y, &ui_.chaos, &ui_.swing };
    int32_t* const euclidean[4] = { &ui_.length[0], &ui_.length[1], &ui_.length[2], &ui_.swing };
    value = euclid ? euclidean[k] : drums[k];
  } else {
    int32_t* const fills[4] = { &ui_.fill[0], &ui_.fill[1], &ui_.fill[2], &ui_.accent };
    value = fills[k & 3];
  }
  *value = Clamp16(*value + delta_q16);
}

void GridsSeq::OnClick(GridsClick click, int32_t direction) {
  switch (click) {
    case GRIDS_BLUE:
      if (direction < 0) {
        ui_.resolution = ui_.resolution == RESOLUTION_16TH ? RESOLUTION_32ND : RESOLUTION_16TH;
      } else {
        ui_.mode = ui_.mode == OUTPUT_MODE_DRUMS ? OUTPUT_MODE_EUCLIDEAN : OUTPUT_MODE_DRUMS;
      }
      break;
    case GRIDS_GREEN:
      if (direction < 0) {
        for (int i = 0; i < 3; ++i) ui_.accent_key[i] = kNoKey;
      } else {
        seed_ = static_cast<uint16_t>(seed_ * 31421u + 6927u);
        sequencer_.Reseed(seed_);
      }
      break;
    case GRIDS_WHITE:
      if (direction < 0) {
        ui_.random_voice ^= 1;
      } else {
        ui_.assign = static_cast<uint8_t>((ui_.assign + 1) % kNumAssign);
      }
      break;
  }
}

}  // namespace op1grids
