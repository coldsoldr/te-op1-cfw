// Host simulation of the FX probe (op1/fx_probe.cc built with -DOP1_HOST_SIM
// -DOP1_SIM; make host-fx-probe): a simulated OP-1 with a known clock, free
// CPU, stock phone cost and synth voice count; effect calls of 64 samples at
// 44.1 kHz, jittered by up to 8% of a block. Two 100 ms gaps (the effect
// switched off) come before the blue encoder moves at 6 s; the white encoder
// moves at 1 s (clearing a gap seen before it). Writes the effect's output
// (left) as a WAV at 44.1 kHz, to check tools/decode_probe_beeps.py recovers
// the simulated values.
//
//   host_fx_probe OUT.wav CLOCK_MHZ FREE_PERCENT PHONE_CYCLES VOICES STEREO

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "harness/wav.h"
#include "op1/fx_probe.h"
#include "op1/op1.h"

uint32_t op1::host_cycles;
uint32_t op1::sim_pitch_buffer[op1::kBlockSize];

namespace {
uint32_t phone_cycles;
}  // namespace

void HostPhone(int16_t* buf) {
  (void)buf;
  op1::host_cycles += phone_cycles;
}

int main(int argc, char** argv) {
  if (argc < 7) return 1;
  const double clock = atof(argv[2]) * 1e6;
  const double free_fraction = atof(argv[3]) / 100.0;
  const uint32_t phone = static_cast<uint32_t>(atoi(argv[4]));
  const int voices = atoi(argv[5]);
  const bool stereo = atoi(argv[6]) != 0;
  phone_cycles = stereo ? phone / 2 : phone;   // stereo runs it twice
  const double rate = 44100.0;
  const double period = clock * op1::kFxBlockSize / rate;
  const double seconds = 80.0;
  harness::FloatWavWriter wav;
  wav.Open(argv[1], static_cast<uint32_t>(rate));
  int16_t params[8] = { 1000, 2000, 3000, 4000, 0, 0, 0, 0 };
  int16_t l[op1::kFxBlockSize], r[op1::kFxBlockSize];
  float bl[op1::kFxBlockSize], br[op1::kFxBlockSize];
  double block_start = 1000.0;
  uint32_t random = 1;
  const long blocks = static_cast<long>(seconds * rate / op1::kFxBlockSize);
  const long per_second = static_cast<long>(rate / op1::kFxBlockSize);
  for (long b = 0; b < blocks; ++b) {
    if (b == per_second / 2) {               // a gap before the reset
      block_start += 0.1 * clock;
    }
    if (b == per_second) params[2] += 100;   // white: back to idle
    if (b == 2 * per_second || b == 3 * per_second) {
      block_start += 0.1 * clock;            // two gaps counted
    }
    if (b == 6 * per_second) params[0] += 100;   // blue: start the ramp
    op1::render_calls += voices;
    random = random * 1664525u + 1013904223u;
    const double jitter = (random >> 8) / 16777216.0 * 0.08 * period;
    op1::host_cycles = static_cast<uint32_t>(block_start + jitter);
    const uint32_t before = op1::host_cycles;
    for (int i = 0; i < op1::kFxBlockSize; ++i) l[i] = r[i] = 0;
    if (stereo) {
      FxProbeStereo(nullptr, l, r, params);
    } else {
      FxProbeMono(nullptr, l, params);
    }
    const double work = static_cast<double>(op1::host_cycles - before);
    for (int i = 0; i < op1::kFxBlockSize; ++i) {
      bl[i] = l[i] / 32768.0f;
      br[i] = r[i] / 32768.0f;
    }
    wav.Write(bl, br, op1::kFxBlockSize);
    // The OP-1's other work takes (1 - free) of the period, including stock
    // phone; our extra work beyond that delays the next block.
    const double overrun = work - phone - free_fraction * period;
    block_start += period + (overrun > 0 ? overrun : 0.0);
  }
  return 0;
}
