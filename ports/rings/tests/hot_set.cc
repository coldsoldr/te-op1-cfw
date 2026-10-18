// Rings' per-block code footprint (make hot-set): six modal voices in
// polyphony "all", 30 blocks, a marker call around each of the last three
// blocks, traced with bfin-elf-run --trace-insn; tools/hot_set.py counts
// the distinct 32-byte code lines a block executes (what the OP-1's 16 KB
// instruction cache must hold) and attributes them to functions.

#include <cstdint>

#include "op1/op1.h"
#include "op1/settings.h"

extern "C" void RingsRender(void* self, int16_t* out, const int16_t* params);
extern "C" void RingsNoteOn(void* self, const int16_t* knobs);
extern "C" __attribute__((noinline)) void HotSetMarker() {
  __asm__ __volatile__("NOP;" ::: "memory");
}

namespace op1 { uint32_t sim_pitch_buffer[kBlockSize]; }

static void* Voice(int v) {
  return reinterpret_cast<void*>(op1::kVoiceObjectBase + v * op1::kPhaseVoiceObjectSize);
}

int main() {
  for (int i = 0; i < op1::kBlockSize; ++i) op1::sim_pitch_buffer[i] = 10713115;   // 220 Hz
  const int16_t knobs[8] = { 8192, 19660, 22937, 9830, op1::EncodeSetting(0), 0,
                             op1::EncodeSetting(op1::ENGINE_RINGS), 0 };
  int16_t out[op1::kBlockSize];
  for (int v = 0; v < op1::kNumVoices; ++v) RingsNoteOn(Voice(v), knobs);
  for (int b = 0; b < 30; ++b) {
    if (b >= 27) HotSetMarker();
    for (int v = 0; v < op1::kNumVoices; ++v) RingsRender(Voice(v), out, knobs);
  }
  HotSetMarker();
  return 0;
}
