// Per-call scratch buffers, shared by every voice (the OP-1 renders one
// voice at a time; nothing in them outlives a render call).
//
// On the OP-1 they sit in the L1 scratchpad (single-cycle): the firmware
// uses both cacheable halves of L1 data as SRAM (it references 0xff804xxx-
// 0xff806xxx and loads data B up to 0xff907e00), so it runs with no data
// cache, and every SDRAM access stalls. The mode bank reads and writes its
// output buffer once per mode and sample, and the string kernels their
// mono sum once per string and sample. The scratchpad holds the live synth
// engine's voice objects from 0xffb00000 (stock phase's, 6 x 28 bytes,
// whenever our render runs), the effect objects from 0xffb00600 and the LFO
// objects from 0xffb00a00: we take 0xffb000c0-0xffb005bf. Elsewhere (tests,
// the simulator) a static array stands in.

#ifndef OP1RINGS_SCRATCH_H_
#define OP1RINGS_SCRATCH_H_

#include <cstdint>

namespace op1rings {

const int32_t kScratchBlock = 64;        // samples per render at 44.1 kHz
const int32_t kScratchSubblock = 32;     // the modal sub-block

struct Scratch {
  int32_t render_out[kScratchBlock];     // the voice's block, before upsampling
  int32_t wet_share[kScratchBlock];      // its share of the shared reverb
  union {
    struct {                             // the modal model's sub-block
      int32_t excitation[kScratchSubblock];
      int32_t in_scaled[kScratchSubblock];
      int32_t half_x[kScratchSubblock];
    } modal;
    struct {                             // the string models' block
      int32_t excitation[kScratchBlock];
      int32_t sympathetic[kScratchBlock];
      int32_t mono[kScratchBlock];
    } strings;
    int32_t upsampler[kScratchBlock + 3];   // after the voice has rendered
  };
};

#ifdef OP1RINGS_L1_SCRATCH
const uint32_t kScratchAddress = 0xffb000c0;
static_assert(sizeof(Scratch) <= 0x600 - 0xc0, "scratch runs into the effect objects");
inline Scratch* scratch() { return reinterpret_cast<Scratch*>(kScratchAddress); }
#else
inline Scratch* scratch() {
  static Scratch buffers;   // zero-initialised: no guard
  return &buffers;
}
#endif

}  // namespace op1rings

#endif  // OP1RINGS_SCRATCH_H_
