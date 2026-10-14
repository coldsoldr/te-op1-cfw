// Simulator smoke test of the OP-1 engine entry points (op1/engine.cc), as
// the OP-1 would call them: voice objects at 0xffb00000 + 28 i (only used as
// numbers), 128-sample blocks. Build with -DOP1_SIM: the pitch buffer is an
// array here instead of 0xff801950.

#include <cstdint>
#include <cstdio>

#include "harness/cycles.h"
#include "op1/op1.h"

extern "C" void RingsRender(void* self, int16_t* out, const int16_t* params);
extern "C" void RingsNoteOn(void* self);

namespace op1 { uint32_t sim_pitch_buffer[kBlockSize]; }

static void* Voice(int v) {
  return reinterpret_cast<void*>(op1::kVoiceObjectBase + v * op1::kPhaseVoiceObjectSize);
}

int main() {
  harness::EnableCycleCounter();
  uint32_t* pitch = op1::sim_pitch_buffer;
  const uint32_t kA3 = 21426229;   // 220 Hz: 220 / 44100 * 2^32
  for (int i = 0; i < op1::kBlockSize; ++i) pitch[i] = kA3;
  const int16_t params[4] = { 8192, 19660, 22937, 9830 };   // .25 .6 .7 .3
  int16_t out[op1::kBlockSize];

  // One voice playing, five idle (all six rendered every block).
  RingsNoteOn(Voice(0));
  harness::CycleStats one, six;
  int32_t peak = 0;
  uint32_t checksum = 0;
  for (int b = 0; b < 100; ++b) {
    uint64_t t0 = harness::ReadCycles();
    for (int v = 0; v < op1::kNumVoices; ++v) {
      RingsRender(Voice(v), out, params);
      for (int i = 0; i < op1::kBlockSize; ++i) {
        if (v == 0) checksum = checksum * 31u + static_cast<uint16_t>(out[i]);
        int32_t a = out[i] < 0 ? -out[i] : out[i];
        if (a > peak) peak = a;
      }
    }
    if (b > 0) one.Add(harness::ReadCycles() - t0);
  }
  printf("1 voice playing: peak %d, checksum %08x\n", static_cast<int>(peak),
         static_cast<unsigned>(checksum));
  one.Print("6 renders/block, 1 active", op1::kBlockSize);

  // All six voices playing.
  for (int v = 0; v < op1::kNumVoices; ++v) RingsNoteOn(Voice(v));
  for (int b = 0; b < 50; ++b) {
    uint64_t t0 = harness::ReadCycles();
    for (int v = 0; v < op1::kNumVoices; ++v) RingsRender(Voice(v), out, params);
    if (b > 0) six.Add(harness::ReadCycles() - t0);
  }
  six.Print("6 renders/block, 6 active", op1::kBlockSize);
  return 0;
}
