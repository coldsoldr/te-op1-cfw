// Checks of the OP-1 voice wrapper (op1/braids_voice.*), host and Blackfin:
//
// 1. PitchFromIncrement inverts Braids' increment table: for every pitch
//    0..16383, the increment of the pitch it returns equals the input
//    increment (several low pitches share an increment: any of them will do).
// 2. The OP-1's notes: for MIDI 0..127 at 88.2 kHz (inc = f / 88200 * 2^32)
//    the pitch is the note x 128 + 187.8 (the retune) within rounding, and
//    (host) the increment it gives is within a fraction of a cent of the
//    OP-1's.
// 3. The FIFO: every shape rendered through Op1Voice (128-sample calls)
//    equals the same oscillator rendered directly in 24-sample blocks,
//    sample for sample (constant settings; a note-on at the start).
// 4. A checksum per shape of the wrapper's output (host = Blackfin), and
//    the lo-fi option against a direct reimplementation of braids.cc's loop.
// 5. (host) The fundamental of the wrapper's output at four notes, for the
//    pitched shapes.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "harness/shapes.h"
#ifndef __bfin__
#include "harness/pitch.h"
#endif
#include "op1/braids_voice.h"
#include "src/stmlib/random.h"

using op1braids::Op1Voice;

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL: %s\n", what);
    ++failures;
  }
}

// The OP-1's phase increment for a MIDI note: f / 88200 * 2^32, rounded.
// Integer-only (the Blackfin build has no FPU): octave 4 from a table,
// shifted for the others.
uint32_t Op1Increment(int note) {
  // 440 / 88200 * 2^32 * 2^((n - 69) / 12) for n = 60..71, rounded.
  static const uint32_t kOctave4[12] = {
    12740059, 13497623, 14300233, 15150569, 16051469, 17005939,
    18017165, 19088521, 20223584, 21426141, 22700205, 24050030,
  };
  int octave = note / 12 - 5;
  uint32_t inc = kOctave4[note % 12];
  if (octave >= 0) return inc << octave;
  return (inc + (1u << (-octave - 1))) >> -octave;
}

void TestPitchInverse() {
  int mismatches = 0;
  int distinct_exact = 0;
  for (int p = 0; p < 16384; ++p) {
    const uint32_t inc = op1braids::IncrementFromPitch(static_cast<int16_t>(p));
    const int16_t q = op1braids::PitchFromIncrement(inc);
    if (op1braids::IncrementFromPitch(q) != inc) ++mismatches;
    if (q == p) ++distinct_exact;
  }
  printf("pitch inverse: %d of 16384 pitches round-trip to the same increment"
         " (%d to the same pitch)\n", 16384 - mismatches, distinct_exact);
  Check(mismatches == 0, "PitchFromIncrement(IncrementFromPitch(p)) gives p's increment");
}

void TestOp1Notes() {
  int worst_q8 = 0;   // |pitch - (note x 128 + 187.8)| x 10
#ifndef __bfin__
  double worst_cents = 0.0;
#endif
  for (int note = 0; note < 128; ++note) {
    const uint32_t inc = Op1Increment(note);
    const int16_t pitch = op1braids::PitchFromIncrement(inc);
    // note x 128 + 187.79, in tenths: compare 10 x pitch with 1280 note + 1878.
    int err = 10 * pitch - (1280 * note + 1878);
    if (err < 0) err = -err;
    if (note >= 12 && err > worst_q8) worst_q8 = err;   // below C0 increments are a few units
#ifndef __bfin__
    const double ideal = harness::MidiFrequency(note) / 88200.0 * 4294967296.0;
    const double got = op1braids::IncrementFromPitch(pitch);
    const double c = harness::Cents(got, ideal);
    if (note >= 12 && (c > worst_cents || -c > worst_cents)) worst_cents = c < 0 ? -c : c;
#endif
  }
  printf("OP-1 notes C0..G9: pitch = note x 128 + 187.8 within %d.%d/128 semitone\n",
         worst_q8 / 10, worst_q8 % 10);
  Check(worst_q8 <= 6, "OP-1 note pitch within 0.6/128 semitone of note x 128 + 187.8");
#ifndef __bfin__
  printf("OP-1 notes C0..G9: Braids' increment within %.3f cents of the OP-1's\n", worst_cents);
  Check(worst_cents < 0.5, "increment within half a cent");
#endif
}

Op1Voice voice;
op1braids::MacroOscillator direct;

uint32_t Checksum(uint32_t c, const int16_t* x, int n) {
  for (int i = 0; i < n; ++i) c = c * 31u + static_cast<uint16_t>(x[i]);
  return c;
}

// 3 and 4: through the wrapper and directly, kCalls OP-1 calls.
const int kCalls = 48;   // 6144 samples, 256 Braids blocks

void TestFifo() {
  const uint32_t inc = Op1Increment(57);
  const int16_t pitch = op1braids::PitchFromIncrement(inc);
  uint32_t all = 0;
  int differing = 0;
  static int16_t a[kCalls * 128];
  static int16_t b[kCalls * 128 + 24];
  static const uint8_t kNoSync[24] = { 0 };
  for (int s = 0; s < harness::kNumShapes; ++s) {
    const int16_t timbre = 12000, color = 21000;
    op1braids::stmlib::Random::Seed(0x21);
    voice.Init();
    voice.set_shape(s);
    voice.NoteOn();
    for (int c = 0; c < kCalls; ++c) voice.Render(inc, timbre, color, &a[c * 128]);

    op1braids::stmlib::Random::Seed(0x21);
    memset(static_cast<void*>(&direct), 0, sizeof(direct));
    direct.Init();
    direct.set_shape(op1braids::MACRO_OSC_SHAPE_CSAW);
    direct.set_shape(static_cast<op1braids::MacroOscillatorShape>(s));
    direct.Strike();
    for (int n = 0; n < kCalls * 128; n += 24) {
      direct.set_parameters(timbre, color);
      direct.set_pitch(pitch);
      direct.Render(kNoSync, &b[n], 24);
    }
    if (memcmp(a, b, sizeof(a)) != 0) {
      printf("  shape %d %s: wrapper differs from direct rendering\n", s, harness::kShapeNames[s]);
      ++differing;
    }
    const uint32_t c = Checksum(0, a, kCalls * 128);
    all = all * 31u + c;
  }
  printf("FIFO: %d of %d shapes: wrapper = direct 24-sample rendering, sample for sample\n",
         harness::kNumShapes - differing, harness::kNumShapes);
  Check(differing == 0, "wrapper output equals direct rendering");
  printf("wrapper checksum (48 shapes, A3, %d calls each): %08x\n", kCalls,
         static_cast<unsigned>(all));
}

void TestLofi() {
  const uint32_t inc = Op1Increment(57);
  const int16_t pitch = op1braids::PitchFromIncrement(inc);
  static const uint16_t kMasks[7] = { 0xc000, 0xe000, 0xf000, 0xf800, 0xff00, 0xfff0, 0xffff };
  static const uint8_t kFactors[7] = { 24, 12, 6, 4, 3, 2, 1 };
  static const uint8_t kNoSync[24] = { 0 };
  static int16_t a[kCalls * 128];
  static int16_t b[kCalls * 128 + 24];
  int differing = 0;
  for (int setting = 0; setting < 7; ++setting) {
    const int res = setting, rate = 6 - setting;
    op1braids::stmlib::Random::Seed(0x21);
    voice.Init();
    voice.set_shape(op1braids::MACRO_OSC_SHAPE_SAW_SWARM);
    voice.set_lofi(res, rate);
    voice.NoteOn();
    for (int c = 0; c < kCalls; ++c) voice.Render(inc, 9000, 20000, &a[c * 128]);

    op1braids::stmlib::Random::Seed(0x21);
    memset(static_cast<void*>(&direct), 0, sizeof(direct));
    direct.Init();
    direct.set_shape(op1braids::MACRO_OSC_SHAPE_SAW_SWARM);
    direct.Strike();
    for (int n = 0; n < kCalls * 128; n += 24) {
      direct.set_parameters(9000, 20000);
      direct.set_pitch(pitch);
      direct.Render(kNoSync, &b[n], 24);
      // braids.cc RenderBlock's loop (without its gain and waveshaper).
      int16_t held = 0;
      for (int i = 0; i < 24; ++i) {
        if ((i % kFactors[rate]) == 0) held = static_cast<int16_t>(b[n + i] & kMasks[res]);
        b[n + i] = held;
      }
    }
    if (memcmp(a, b, sizeof(a)) != 0) ++differing;
  }
  printf("lo-fi: %d of 7 settings equal braids.cc's bit and rate reduction\n", 7 - differing);
  Check(differing == 0, "lo-fi equals upstream's loop");
}

#ifndef __bfin__
void TestWrapperPitch() {
  static const int kNotes[4] = { 45, 57, 69, 81 };
  const int kCallsPitch = 690;   // ~1 s
  std::vector<int16_t> x(kCallsPitch * 128);
  double worst = 0.0;
  int count = 0;
  printf("wrapper pitch (88.2 kHz, cents from equal temperament):\n");
  for (int s = 0; s < harness::kNumShapes; ++s) {
    if (!harness::kPitched[s]) continue;
    printf("  %-3d %-7s", s, harness::kShapeNames[s]);
    for (int k = 0; k < 4; ++k) {
      op1braids::stmlib::Random::Seed(0x21);
      voice.Init();
      voice.set_shape(s);
      voice.NoteOn();
      const int16_t color = s == op1braids::MACRO_OSC_SHAPE_BUZZ ? 0 : 16384;
      for (int c = 0; c < kCallsPitch; ++c) {
        voice.Render(Op1Increment(kNotes[k]), 16384, color, &x[c * 128]);
      }
      const int start = 88200 * 4 / 10;
      harness::PitchEstimate e = harness::EstimatePitch(&x[start], kCallsPitch * 128 - start, 88200.0);
      const double c = harness::Cents(e.frequency, harness::MidiFrequency(kNotes[k]));
      printf("  %+8.2f%s", c, e.aperiodicity < 0.15 ? "  " : " ?");
      if (e.aperiodicity < 0.15) {
        if (c > worst || -c > worst) worst = c < 0 ? -c : c;
        ++count;
      }
    }
    printf("\n");
  }
  printf("wrapper pitch: %d measurements, worst |%.2f| cents\n", count, worst);
}
#endif

}  // namespace

int main() {
  TestPitchInverse();
  TestOp1Notes();
  TestFifo();
  TestLofi();
#ifndef __bfin__
  TestWrapperPitch();
#endif
  printf("%s (%d failure%s)\n", failures ? "FAILED" : "all passed", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
