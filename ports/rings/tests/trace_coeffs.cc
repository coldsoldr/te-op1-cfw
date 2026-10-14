// Runs one coefficient update with state translation, bracketed by marker
// calls, for instruction-trace profiling in bfin-elf-run.
#include <cstdint>
#include "src/fixed.h"
#include "src/ffloat.h"
#include "src/modal_resonator.h"
using namespace op1rings;
static ModalResonator r;
extern "C" __attribute__((noinline)) void TraceMarker(int x) { __asm__ __volatile__("" : : "r"(x)); }
int main() {
  r.Init();
  r.set_resolution(60);
  r.set_frequency(FFloat(110.0 / 44100.0).ToQ31());
  r.set_structure(FFloat(0.3).ToQ31());
  r.set_brightness(FFloat(0.36).ToQ31());
  r.set_damping(FFloat(0.7).ToQ31());
  r.UpdateCoefficients();
  r.set_frequency(FFloat(110.0 / 44100.0).ToQ31() + 200000);
  TraceMarker(1);
  r.UpdateCoefficients();
  TraceMarker(2);
  return 0;
}
