// Drives a Streams Processor (upstream's streams::Processor or the copy's
// op1streams::Processor: same interface) through a deterministic run:
// every function, normal and alternate, single and linked controls, knob
// changes every 4096 samples, inputs from harness/signals.h. Used by
// tests/copy_check.cc (upstream vs copy) and tests/bench.cc (host vs
// Blackfin checksums).

#ifndef OP1STREAMS_HARNESS_DRIVE_H_
#define OP1STREAMS_HARNESS_DRIVE_H_

#include <stdint.h>

#include "harness/signals.h"

namespace harness {

struct DriveCase {
  int function;      // ProcessorFunction 0..5
  bool alternate;
  bool linked;
  Material material;
  int excite_gain;   // excite = signal * gain (saturated)
};

inline uint32_t Fnv(uint32_t h, uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    h ^= (v >> (8 * i)) & 0xff;
    h *= 16777619u;
  }
  return h;
}

// Knob values for change k (deterministic, covering both halves of each
// knob's range, the ends included).
inline uint16_t Knob(uint32_t k, int which) {
  static const uint16_t kValues[] = {
    0, 65535, 32768, 8000, 20000, 30000, 36000, 42000, 49000, 50000,
    56000, 62000, 12345, 45678, 1000, 64000 };
  return kValues[(k * 7 + which * 5 + (k >> 2)) & 15];
}

// Runs `samples` samples; calls sink(i, gain, frequency) per sample.
template<typename ProcessorT, typename Sink>
void Drive(ProcessorT* p, const DriveCase& c, int32_t sample_rate,
           int32_t samples, Sink& sink) {
  Signal signal;
  signal.Init(c.material, sample_rate, false);
  p->Init(0);
  p->set_linked(c.linked);
  p->set_alternate(c.alternate);
  p->set_function(static_cast<decltype(p->function())>(c.function));
  uint32_t change = 0;
  for (int32_t i = 0; i < samples; ++i) {
    if ((i & 4095) == 0) {
      // Upstream's order (Ui::OnPotMoved): set_global writes dirty_ =
      // linked_, so it must come before set_parameter.
      for (int k = 0; k < 4; ++k) p->set_global(k, Knob(change, k + 2));
      for (int k = 0; k < 2; ++k) p->set_parameter(k, Knob(change, k));
      p->Configure();
      ++change;
    }
    int16_t l, r;
    signal.Next(&l, &r);
    int32_t e = l * c.excite_gain;
    if (e > 32767) e = 32767;
    if (e < -32768) e = -32768;
    uint16_t gain = 0;
    uint16_t frequency = 65535;
    p->Process(l, static_cast<int16_t>(e), &gain, &frequency);
    sink(i, gain, frequency);
  }
}

// The same run through the copy's Processor::ProcessBlock, 64 samples at a
// time (knob changes fall on block boundaries: 4096 is a multiple of 64).
template<typename ProcessorT, typename Sink>
void DriveBlocks(ProcessorT* p, const DriveCase& c, int32_t sample_rate,
                 int32_t samples, Sink& sink) {
  Signal signal;
  signal.Init(c.material, sample_rate, false);
  p->Init(0);
  p->set_linked(c.linked);
  p->set_alternate(c.alternate);
  p->set_function(static_cast<decltype(p->function())>(c.function));
  uint32_t change = 0;
  int16_t audio[64], excite[64];
  uint16_t gain[64], frequency[64];
  for (int32_t start = 0; start < samples; start += 64) {
    if ((start & 4095) == 0) {
      for (int k = 0; k < 4; ++k) p->set_global(k, Knob(change, k + 2));
      for (int k = 0; k < 2; ++k) p->set_parameter(k, Knob(change, k));
      p->Configure();
      ++change;
    }
    int32_t n = samples - start < 64 ? samples - start : 64;
    for (int32_t i = 0; i < n; ++i) {
      int16_t l, r;
      signal.Next(&l, &r);
      int32_t e = l * c.excite_gain;
      if (e > 32767) e = 32767;
      if (e < -32768) e = -32768;
      audio[i] = l;
      excite[i] = static_cast<int16_t>(e);
    }
    p->ProcessBlock(audio, excite, gain, frequency, n);
    for (int32_t i = 0; i < n; ++i) sink(start + i, gain[i], frequency[i]);
  }
}

// The cases: 6 functions x {normal, alternate} x {single, linked}, each
// on one material with one excite gain chosen to suit the function.
inline int NumDriveCases() { return 24; }

inline DriveCase GetDriveCase(int index) {
  DriveCase c;
  c.function = index / 4;
  c.alternate = (index & 1) != 0;
  c.linked = (index & 2) != 0;
  static const Material kMaterial[6] = {
    MAT_MIX, MAT_RIFF, MAT_RIFF, MAT_DRUMS, MAT_RIFF, MAT_PAD };
  static const int kGain[6] = { 3, 2, 1, 1, 1, 1 };
  c.material = kMaterial[c.function];
  c.excite_gain = kGain[c.function];
  return c;
}

}  // namespace harness

#endif  // OP1STREAMS_HARNESS_DRIVE_H_
