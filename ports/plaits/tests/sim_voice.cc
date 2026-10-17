// The OP-1 glue (op1/plaits_op1.cc) called as the MULTI engine would: voice
// objects at 0xffb00000 + 28 i, 128-sample calls at 88.2 kHz, pitch from
// the (simulated) increment buffer. On Blackfin (bfin-elf-run, -DOP1_SIM)
// and on the host: the "checksum" lines must match.
//
// Per engine: the fundamental of voice 0 (A3, 220 Hz expected; measured by
// zero crossings of a low-passed copy, for the tonal engines), a checksum,
// and simulated cycles per call with 1 and with 6 voices sounding.
//
// Usage: sim_voice [engine] [OUT.raw]

#include <cstdio>
#include <cstdlib>

#include "../../rings/harness/cycles.h"
#include "../../rings/op1/op1.h"
#include "op1/plaits_op1.h"

namespace op1 { uint32_t sim_pitch_buffer[kBlockSize]; }

namespace {

void* Voice(int v) {
  return reinterpret_cast<void*>(op1::kVoiceObjectBase + v * op1::kPhaseVoiceObjectSize);
}

int16_t out[op1::kBlockSize];

}  // namespace

int main(int argc, char** argv) {
  harness::EnableCycleCounter();
  int first = 0, last = 15;
  if (argc >= 2 && atoi(argv[1]) >= 0) first = last = atoi(argv[1]);
  FILE* raw = argc > 2 ? fopen(argv[2], "wb") : nullptr;
  const uint32_t kA3 = 10713115;   // 220 Hz: 220 / 88200 * 2^32
  for (int i = 0; i < op1::kBlockSize; ++i) op1::sim_pitch_buffer[i] = kA3;
  const double calls_per_second = 88200.0 / op1::kBlockSize;

  for (int engine = first; engine <= last; ++engine) {
    // Encoders: harmonics .5, timbre .5, morph .5, decay .7.
    const int16_t params[4] = { 16384, 16384, 16384, 22937 };
    int16_t knobs[8] = { 16384, 16384, 16384, 22937,
                         op1plaits::EncodePlaitsSetting(engine), 0, 3 << 12, 0 };
    PlaitsNoteOn(Voice(0), knobs);
    uint32_t checksum = 2166136261u;
    uint64_t one = 0;
    int crossings = 0;
    int32_t lp = 0, previous = 0;
    const int kCalls = 689;   // ~1 s
    for (int c = 0; c < kCalls; ++c) {
      const uint64_t t0 = harness::ReadCycles();
      PlaitsRender(Voice(0), out, params);
      one += harness::ReadCycles() - t0;
      if (raw && engine == first) fwrite(out, sizeof(out), 1, raw);
      for (int i = 0; i < op1::kBlockSize; ++i) {
        checksum = (checksum ^ static_cast<uint16_t>(out[i])) * 16777619u;
        lp += (out[i] * 256 - lp) >> 4;   // a gentle low-pass for the count
        if (c >= 345) {   // the last half second
          if (previous < 0 && lp >= 0) ++crossings;
          previous = lp;
        }
      }
    }
    // Six voices sounding (the same note), cycles per block of six calls.
    for (int v = 1; v < op1::kNumVoices; ++v) PlaitsNoteOn(Voice(v), knobs);
    uint64_t six = 0, worst = 0;
    const int kSixCalls = 172;   // 0.25 s
    for (int c = 0; c < kSixCalls; ++c) {
      const uint64_t t0 = harness::ReadCycles();
      for (int v = 0; v < op1::kNumVoices; ++v) PlaitsRender(Voice(v), out, params);
      const uint64_t t = harness::ReadCycles() - t0;
      six += t;
      if (t > worst) worst = t;
      for (int i = 0; i < op1::kBlockSize; ++i) {
        checksum = (checksum ^ static_cast<uint16_t>(out[i])) * 16777619u;
      }
    }
    printf("checksum %2d %08x  f0 %.1f Hz (zero crossings, last 0.5 s)\n", engine,
           static_cast<unsigned>(checksum), crossings * 2.0);
#ifdef __bfin__
    printf("cycles   %2d 1 voice %6.0f per call (%5.1f MHz); 6 voices %5.1f MHz avg, %5.1f peak\n",
           engine, static_cast<double>(one) / kCalls,
           static_cast<double>(one) / kCalls * calls_per_second / 1e6,
           static_cast<double>(six) / kSixCalls * calls_per_second / 1e6,
           static_cast<double>(worst) * calls_per_second / 1e6);
#endif
  }
  if (raw) fclose(raw);
  return 0;
}
