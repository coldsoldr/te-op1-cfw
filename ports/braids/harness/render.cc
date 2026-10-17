// Renders every Braids shape through MacroOscillator, the way the module
// drives it (braids.cc RenderBlock: 24-sample blocks, set_parameters,
// set_pitch, Strike on a trigger, Render), and prints a checksum per shape.
// Built three ways from the same scenario:
//
//   -DREF   unmodified upstream (mutableinstruments/braids, stmlib)
//   (none)  the port in src/ (namespace op1braids)
//   on Blackfin (bfin-elf-run) from the port, for the same checksums
//
//   render [-r RATE] [-m MS] [-o DIR] [-n] [-p] [-s SHAPE]
//     -r RATE   96000 (upstream's rate, default) or 88200 (the OP-1's voice
//               rate: Braids' pitch is raised by kPitchOffset88200, which
//               retunes every pitch-derived table at once)
//     -m MS     length per shape in milliseconds (default 2000)
//     -o DIR    write DIR/NN_NAME.wav (16-bit mono)
//     -n        at 88200: no retune (shows the pitch error it fixes)
//     -p        pitch check instead: measured fundamental per pitched shape
//               at four notes, in cents from equal temperament (host only)
//     -s SHAPE  one shape only
//
// The scenario (identical at both rates, in seconds): A3 (MIDI 57), a
// trigger (Strike) at 0 s and every 0.5 s, timbre and color swept by slow
// triangles (periods 2 s and 3.1 s). Each shape starts from a zeroed
// oscillator and the random generator seeded with 0x21 (upstream's initial
// value), so every checksum is independent of the others. "check" covers the
// first 200 ms (what the Blackfin run renders), "full" the whole length.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "harness/shapes.h"
#include "harness/wav16.h"
#ifndef __bfin__
#include "harness/pitch.h"
#endif

#ifdef REF
#include "braids/macro_oscillator.h"
#include "stmlib/utils/random.h"
namespace bx = braids;
typedef stmlib::Random BxRandom;
#else
#include "src/macro_oscillator.h"
#include "src/stmlib/random.h"
namespace bx = op1braids;
typedef op1braids::stmlib::Random BxRandom;
#endif

namespace {

const int kBlock = 24;
const int kCheckMs = 200;
const int kNote = 57;   // A3, 220 Hz

// round(128 * 12 * log2(96000 / 88200)) = round(187.8): Braids' tables are
// computed for 96 kHz; at 88.2 kHz a pitch this much higher gives the same
// phase increments as the intended pitch would at 96 kHz.
const int kPitchOffset88200 = 188;

int16_t Tri(uint32_t x) {
  x &= 0xffff;
  return static_cast<int16_t>(x < 32768 ? x : 65535 - x);
}

bx::MacroOscillator osc;   // zero-initialised: upstream's Init leaves some members unset

void ResetOscillator(int shape) {
  memset(static_cast<void*>(&osc), 0, sizeof(osc));
  BxRandom::Seed(0x21);
  osc.Init();
  osc.set_shape(static_cast<bx::MacroOscillatorShape>(shape));
}

struct Options {
  uint32_t rate;
  uint32_t ms;
  const char* out_dir;
  bool retune;
  bool pitch_check;
  int only_shape;
};

int PitchOffset(const Options& o) {
  return (o.rate == 88200 && o.retune) ? kPitchOffset88200 : 0;
}

uint32_t RenderShape(const Options& o, int shape, uint32_t* full_checksum, int32_t* peak) {
  ResetOscillator(shape);
  harness::Wav16Writer wav;
  if (o.out_dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%02d_%s.wav", o.out_dir, shape, harness::kShapeNames[shape]);
    wav.Open(path, o.rate);
  }
  const uint32_t total = static_cast<uint32_t>(static_cast<uint64_t>(o.rate) * o.ms / 1000);
  const uint32_t check = static_cast<uint32_t>(static_cast<uint64_t>(o.rate) * kCheckMs / 1000);
  const uint32_t strike_interval = o.rate / 2;
  uint32_t next_strike = 0;
  uint32_t checksum = 0, full = 0;
  int32_t max_value = 0, min_value = 0;
  uint8_t sync[kBlock];
  memset(sync, 0, sizeof(sync));
  int16_t buffer[kBlock];
  const int16_t pitch = static_cast<int16_t>((kNote << 7) + PitchOffset(o));
  for (uint32_t n = 0; n < total; n += kBlock) {
    const uint32_t t16 = static_cast<uint32_t>(static_cast<uint64_t>(n) * 65536 / o.rate);
    osc.set_parameters(Tri(t16 / 2 + 8192), Tri(t16 * 10 / 31 + 26214));
    osc.set_pitch(pitch);
    if (n >= next_strike) {
      osc.Strike();
      next_strike += strike_interval;
    }
    osc.Render(sync, buffer, kBlock);
    for (int i = 0; i < kBlock; ++i) {
      const uint16_t s = static_cast<uint16_t>(buffer[i]);
      if (n + i < check) checksum = checksum * 31u + s;
      full = full * 31u + s;
      if (buffer[i] > max_value) max_value = buffer[i];
      if (buffer[i] < min_value) min_value = buffer[i];
    }
    wav.Write(buffer, kBlock);
  }
  *full_checksum = full;
  // Not "x < 0 ? -x : x" per sample: GCC 14 compiles that, on an int16,
  // to Blackfin's saturating ABS (V), which gives 32767 for -32768.
  *peak = max_value > -min_value ? max_value : -min_value;
  return checksum;
}

#ifndef __bfin__
// The pitch check: steady settings, a strike at 0, 1 s rendered, the
// fundamental measured from 0.4 s on.
const int kPitchNotes[4] = { 45, 57, 69, 81 };   // 110, 220, 440, 880 Hz

double MeasureShape(const Options& o, int shape, int note, double* aperiodicity) {
  ResetOscillator(shape);
  uint8_t sync[kBlock];
  memset(sync, 0, sizeof(sync));
  const uint32_t total = o.rate;
  std::vector<int16_t> x(total + kBlock);
  const int16_t timbre = 16384;
  const int16_t color = shape == bx::MACRO_OSC_SHAPE_BUZZ ? 0 : 16384;
  for (uint32_t n = 0; n < total; n += kBlock) {
    osc.set_parameters(timbre, color);
    osc.set_pitch(static_cast<int16_t>((note << 7) + PitchOffset(o)));
    if (n == 0) osc.Strike();
    osc.Render(sync, &x[n], kBlock);
  }
  const uint32_t start = o.rate * 4 / 10;
  harness::PitchEstimate e = harness::EstimatePitch(&x[start], total - start, o.rate);
  *aperiodicity = e.aperiodicity;
  return e.frequency;
}

int PitchCheck(const Options& o) {
  printf("pitch check at %u Hz%s: cents from equal temperament (A4 = 440 Hz)\n", static_cast<unsigned>(o.rate),
         o.rate == 88200 ? (o.retune ? ", retuned" : ", NOT retuned") : "");
  printf("%-3s %-7s", "#", "shape");
  for (int k = 0; k < 4; ++k) printf("  %9.1f Hz", harness::MidiFrequency(kPitchNotes[k]));
  printf("\n");
  double worst = 0.0, sum = 0.0;
  int count = 0;
  for (int s = 0; s < harness::kNumShapes; ++s) {
    if (o.only_shape >= 0 && s != o.only_shape) continue;
    if (!harness::kPitched[s]) continue;
    printf("%-3d %-7s", s, harness::kShapeNames[s]);
    for (int k = 0; k < 4; ++k) {
      double ap = 1.0;
      const double f = MeasureShape(o, s, kPitchNotes[k], &ap);
      const double c = harness::Cents(f, harness::MidiFrequency(kPitchNotes[k]));
      printf("  %+8.2f%s", c, ap < 0.15 ? "   " : " ? ");
      if (ap < 0.15) {
        if (c > worst || -c > worst) worst = c < 0 ? -c : c;
        sum += c;
        ++count;
      }
    }
    printf("\n");
  }
  printf("(? = weak periodicity, YIN aperiodicity >= 0.15; left out of the summary)\n");
  printf("summary: %d measurements, mean %+.2f cents, worst |%.2f| cents\n", count,
         count ? sum / count : 0.0, worst);
  return 0;
}
#endif

}  // namespace

int main(int argc, char** argv) {
  Options o = { 96000, 2000, NULL, true, false, -1 };
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "-r") && i + 1 < argc) o.rate = static_cast<uint32_t>(atoi(argv[++i]));
    else if (!strcmp(argv[i], "-m") && i + 1 < argc) o.ms = static_cast<uint32_t>(atoi(argv[++i]));
    else if (!strcmp(argv[i], "-o") && i + 1 < argc) o.out_dir = argv[++i];
    else if (!strcmp(argv[i], "-n")) o.retune = false;
    else if (!strcmp(argv[i], "-p")) o.pitch_check = true;
    else if (!strcmp(argv[i], "-s") && i + 1 < argc) o.only_shape = atoi(argv[++i]);
    else {
      fprintf(stderr, "usage: render [-r RATE] [-m MS] [-o DIR] [-n] [-p] [-s SHAPE]\n");
      return 2;
    }
  }
  if (o.rate != 96000 && o.rate != 88200) {
    fprintf(stderr, "rate must be 96000 or 88200\n");
    return 2;
  }
#ifndef __bfin__
  if (o.pitch_check) return PitchCheck(o);
#endif
  if (o.ms < kCheckMs) o.ms = kCheckMs;
#ifdef REF
  const char* build = "upstream";
#elif defined(__bfin__)
  const char* build = "port (Blackfin)";
#else
  const char* build = "port";
#endif
  printf("%s, %u Hz, pitch %d%s, %u ms per shape\n", build, static_cast<unsigned>(o.rate),
         (kNote << 7) + PitchOffset(o), PitchOffset(o) ? " (retuned)" : "", static_cast<unsigned>(o.ms));
  uint32_t all = 0;
  for (int s = 0; s < harness::kNumShapes; ++s) {
    if (o.only_shape >= 0 && s != o.only_shape) continue;
    uint32_t full = 0;
    int32_t peak = 0;
    const uint32_t check = RenderShape(o, s, &full, &peak);
    all = all * 31u + check;
    printf("shape %2d %-7s check %08x  full %08x  peak %5d\n", s, harness::kShapeNames[s],
           static_cast<unsigned>(check), static_cast<unsigned>(full), static_cast<int>(peak));
  }
  printf("all check %08x\n", static_cast<unsigned>(all));
  return 0;
}
