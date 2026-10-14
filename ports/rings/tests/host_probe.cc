// Host simulation of the probe (op1/probe.cc built with -DOP1_HOST_SIM):
// a simulated OP-1 with a known clock, free CPU and call pattern. Writes the
// audible voice's output as a WAV, to check tools/decode_probe_beeps.py
// recovers the simulated values.
//
//   host_probe OUT.wav CLOCK_MHZ FREE_PERCENT CALLS_PER_BLOCK [STRAY_EVERY]

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "harness/wav.h"
#include "op1/op1.h"

uint32_t op1::host_cycles;
extern "C" char __image_end[1];
char __image_end[1];
extern "C" void ProbeRender(void* self, int16_t* out, const int16_t* params);

int main(int argc, char** argv) {
  if (argc < 5) return 1;
  const double clock = atof(argv[2]) * 1e6;
  const double free_fraction = atof(argv[3]) / 100.0;
  const int calls = atoi(argv[4]);
  const int stray_every = argc > 5 ? atoi(argv[5]) : 0;
  const double period = clock * 128.0 / 44100.0;
  const double seconds = 75.0;
  harness::FloatWavWriter wav;
  wav.Open(argv[1], 44100);
  const int16_t params[4] = { 0, 0, 0, 0 };
  int16_t out[128];
  float buffer[128];
  double block_start = 1000.0;
  const int audible = calls > 2 ? 2 : 0;
  for (long b = 0; b < static_cast<long>(seconds * 44100 / 128); ++b) {
    double work = 0.0;   // cycles our calls burned in this block
    for (int v = 0; v < calls; ++v) {
      op1::host_cycles = static_cast<uint32_t>(block_start + 20000.0 * v + work);
      uint32_t before = op1::host_cycles;
      void* self = reinterpret_cast<void*>(static_cast<uintptr_t>(
          op1::kVoiceObjectBase + v * op1::kPhaseVoiceObjectSize));
      ProbeRender(self, out, params);
      work += static_cast<double>(op1::host_cycles - before);
      if (v == audible) {
        for (int i = 0; i < 128; ++i) buffer[i] = out[i] / 32768.0f;
        wav.Write(buffer, buffer, 128);
      }
    }
    if (stray_every && b % stray_every == 0) {
      op1::host_cycles = static_cast<uint32_t>(block_start + period * 0.6);
      ProbeRender(reinterpret_cast<void*>(static_cast<uintptr_t>(0x01234560)), out, params);
    }
    // The OP-1's own work takes (1 - free) of the period; overrun delays the
    // next block.
    double overrun = work - free_fraction * period;
    block_start += period + (overrun > 0 ? overrun : 0.0);
  }
  return 0;
}
