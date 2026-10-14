// One steady-state RingsRender call between trace markers, for
// instruction-trace profiling (see tests/trace_coeffs.cc).
#include <cstdint>
#include "op1/op1.h"
extern "C" void RingsRender(void* self, int16_t* out, const int16_t* params);
extern "C" void RingsNoteOn(void* self);
namespace op1 { uint32_t sim_pitch_buffer[kBlockSize]; }
extern "C" __attribute__((noinline)) void TraceMarker(int x) { __asm__ __volatile__("" : : "r"(x)); }
int main() {
  for (int i = 0; i < op1::kBlockSize; ++i) op1::sim_pitch_buffer[i] = 21426229;
  const int16_t params[4] = { 8192, 19660, 22937, 9830 };
  int16_t out[op1::kBlockSize];
  void* v0 = reinterpret_cast<void*>(op1::kVoiceObjectBase);
  RingsNoteOn(v0);
  for (int b = 0; b < 4; ++b) RingsRender(v0, out, params);
  TraceMarker(1);
  RingsRender(v0, out, params);
  TraceMarker(2);
  return 0;
}
