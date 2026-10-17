// Braids as an OP-1 synth voice (op1/braids_voice.h).

#include "op1/braids_voice.h"

#include <cstring>

#include "src/resources.h"

namespace op1braids {

namespace {

const int32_t kPitchTableStart = 128 * 128;
const int32_t kOctave = 12 * 128;
const int32_t kTableSize = LUT_OSCILLATOR_INCREMENTS_SIZE;   // 97: one octave in 1/8 semitones

// Braids' block has no sync input on the OP-1.
const uint8_t kNoSync[kBraidsBlockSize] = { 0 };

// Upstream's settings tables (braids.cc).
const uint16_t kBitReductionMasks[7] = { 0xc000, 0xe000, 0xf000, 0xf800, 0xff00, 0xfff0, 0xffff };
const uint8_t kDecimationFactors[7] = { 24, 12, 6, 4, 3, 2, 1 };

// Braids' interpolation inside one table segment (a = lut[j], b = lut[j+1]).
inline uint32_t Segment(uint32_t a, uint32_t b, int32_t f) {
  return a + (static_cast<int32_t>(b - a) * f >> 4);
}

}  // namespace

uint32_t IncrementFromPitch(int16_t midi_pitch) {
  // AnalogOscillator::ComputePhaseIncrement, unchanged.
  if (midi_pitch >= kPitchTableStart) midi_pitch = kPitchTableStart - 1;
  int32_t ref_pitch = midi_pitch;
  ref_pitch -= kPitchTableStart;
  int32_t num_shifts = 0;
  while (ref_pitch < 0) {
    ref_pitch += kOctave;
    ++num_shifts;
  }
  uint32_t a = lut_oscillator_increments[ref_pitch >> 4];
  uint32_t b = lut_oscillator_increments[(ref_pitch >> 4) + 1];
  uint32_t phase_increment = a + (static_cast<int32_t>(b - a) * (ref_pitch & 0xf) >> 4);
  phase_increment >>= num_shifts;
  return phase_increment;
}

int16_t PitchFromIncrement(uint32_t increment) {
  const uint32_t* lut = lut_oscillator_increments;
  if (increment >= lut[0]) return kPitchTableStart - 1;   // Braids' ceiling (MIDI 128)
  if (increment == 0) return 0;
  // Octaves below the table's: x = increment << shifts lands in
  // [lut[0], 2 lut[0]), the table's octave.
  int32_t shifts = 0;
  uint32_t x = increment;
  while (x < lut[0]) {
    x <<= 1;
    ++shifts;
  }
  int32_t pitch;
  if (x >= lut[kTableSize - 1]) {
    pitch = kPitchTableStart - (shifts - 1) * kOctave;   // the top entry: the next octave's start
  } else {
    // Segment j with lut[j] <= x < lut[j + 1].
    int32_t lo = 0, hi = kTableSize - 1;
    while (hi - lo > 1) {
      const int32_t mid = (lo + hi) >> 1;
      if (lut[mid] <= x) lo = mid; else hi = mid;
    }
    const uint32_t a = lut[lo], b = lut[lo + 1];
    // The largest f (0..15) whose interpolated increment is <= x, then the
    // nearer of f and f + 1.
    int32_t f = 0;
    for (int32_t step = 8; step; step >>= 1) {
      if (Segment(a, b, f + step) <= x) f += step;
    }
    const uint32_t below = Segment(a, b, f);
    const uint32_t above = f == 15 ? b : Segment(a, b, f + 1);
    if (above - x < x - below) ++f;
    pitch = kPitchTableStart - shifts * kOctave + lo * 16 + f;
  }
  if (pitch < 0) pitch = 0;
  return static_cast<int16_t>(pitch);
}

void Op1Voice::Init() {
  memset(static_cast<void*>(this), 0, sizeof(*this));
  osc_.Init();
  shape_ = 0;
  osc_.set_shape(MACRO_OSC_SHAPE_CSAW);
  pending_count_ = 0;
  bit_mask_ = 0xffff;
  decimation_ = 1;
}

void Op1Voice::set_shape(int32_t shape) {
  if (shape < 0) shape = 0;
  if (shape >= MACRO_OSC_SHAPE_LAST) shape = MACRO_OSC_SHAPE_LAST - 1;
  shape_ = shape;
  osc_.set_shape(static_cast<MacroOscillatorShape>(shape));
}

void Op1Voice::set_lofi(int32_t resolution, int32_t rate) {
  if (resolution < 0) resolution = 0;
  if (resolution > 6) resolution = 6;
  if (rate < 0) rate = 0;
  if (rate > 6) rate = 6;
  bit_mask_ = kBitReductionMasks[resolution];
  decimation_ = kDecimationFactors[rate];
}

void Op1Voice::NoteOn() {
  pending_count_ = 0;
  osc_.Strike();
}

void Op1Voice::RenderBlock(int16_t* out) {
  osc_.Render(kNoSync, out, kBraidsBlockSize);
  if (bit_mask_ != 0xffff || decimation_ != 1) {
    // braids.cc: sample-and-hold every decimation_ samples (restarting each
    // block), then the bit mask. A counter instead of upstream's i % factor.
    int16_t held = 0;
    int32_t phase = 0;
    const uint16_t mask = bit_mask_;
    for (int32_t i = 0; i < kBraidsBlockSize; ++i) {
      if (phase == 0) held = static_cast<int16_t>(out[i] & mask);
      out[i] = held;
      if (++phase == decimation_) phase = 0;
    }
  }
}

void Op1Voice::Render(uint32_t increment, int16_t timbre, int16_t color, int16_t* out) {
  osc_.set_parameters(timbre, color);
  osc_.set_pitch(PitchFromIncrement(increment));
  int32_t n = 0;
  // What the last call left over.
  while (pending_count_ > 0) {
    out[n++] = pending_[kBraidsBlockSize - pending_count_];
    --pending_count_;
  }
  // Whole blocks straight into the OP-1's buffer.
  while (kOp1BlockSize - n >= kBraidsBlockSize) {
    RenderBlock(out + n);
    n += kBraidsBlockSize;
  }
  // The last part block: render it whole, keep the rest.
  if (n < kOp1BlockSize) {
    RenderBlock(pending_);
    const int32_t take = kOp1BlockSize - n;
    for (int32_t i = 0; i < take; ++i) out[n + i] = pending_[i];
    pending_count_ = kBraidsBlockSize - take;
  }
}

}  // namespace op1braids
