// Debug: dumps ModalResonator state after a damping change, for diffing the
// host and Blackfin builds.
#include <cstdint>
#include <cstdio>
#include "src/fixed.h"
#include "src/ffloat.h"
#include "src/modal_resonator.h"
using namespace op1rings;
static ModalResonator r;
int main() {
  r.Init();
  r.set_resolution(60);
  r.set_frequency(FFloat(110.0 / 44100.0).ToQ31());
  r.set_structure(FFloat(0.3).ToQ31());
  r.set_brightness(FFloat(0.36).ToQ31());
  r.set_damping(FFloat(0.7).ToQ31());
  r.set_position(FFloat(0.3).ToQ31());
  int32_t in[32] = { kQ27One }, out[32], aux[32];
  r.Process(in, out, aux, 32);
  for (int i = 0; i < 60; i += 7) printf("A %2d %08x %08x %08x %08x\n", i, (unsigned)r.mode(i).lp, (unsigned)r.mode(i).bp, (unsigned)r.mode(i).half_f, (unsigned)r.mode(i).half_q);
  r.set_damping(FFloat(0.7).ToQ31() + 1000000);
  r.UpdateCoefficients();
  for (int i = 0; i < 60; i += 7) printf("B %2d %08x %08x %08x %08x\n", i, (unsigned)r.mode(i).lp, (unsigned)r.mode(i).bp, (unsigned)r.mode(i).half_f, (unsigned)r.mode(i).half_q);
  return 0;
}
