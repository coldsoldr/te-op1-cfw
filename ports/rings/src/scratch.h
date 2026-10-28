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
//
// The block is what one OP-1 render call (128 samples at 88.2 kHz) needs
// at the Rings rate: 64 samples at 44.1 kHz (upsampled 2x), 32 at 22.05 kHz
// (4x). src/rate_config.h.

#ifndef OP1RINGS_SCRATCH_H_
#define OP1RINGS_SCRATCH_H_

#include <cstdint>

#ifndef OP1RINGS_UPSAMPLE
#define OP1RINGS_UPSAMPLE 2
#endif

namespace op1rings {

const int32_t kScratchBlock = 128 / OP1RINGS_UPSAMPLE;   // samples per render at the Rings rate
const int32_t kScratchSubblock = 32;                     // the modal sub-block
const int32_t kScratchHalfbandHistory = 24;              // the 4x upsampler's first stage (op1/engine.cc)

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
#if OP1RINGS_UPSAMPLE == 4
    struct {                             // after the voice has rendered
      int32_t x[kScratchHalfbandHistory + kScratchBlock];   // history + the block, 22.05 kHz
      int32_t mid[3 + 2 * kScratchBlock];                   // history + the block at 44.1 kHz
    } upsampler;
#else
    int32_t upsampler[kScratchBlock + 3];   // after the voice has rendered
#endif
  };
};

#ifdef OP1RINGS_L1_SCRATCH
#ifdef OP1_KENNEL
// The kennel plugin runs in the iter slot: 6 voices x 36 bytes end at
// 0xffb000d8, and kennel's own code uses nothing else of the scratchpad.
const uint32_t kScratchAddress = 0xffb000e0;
static_assert(sizeof(Scratch) <= 0x600 - 0xe0, "scratch runs into the effect objects");
#else
const uint32_t kScratchAddress = 0xffb000c0;
static_assert(sizeof(Scratch) <= 0x600 - 0xc0, "scratch runs into the effect objects");
#endif
inline Scratch* scratch() { return reinterpret_cast<Scratch*>(kScratchAddress); }
#else
inline Scratch* scratch() {
  static Scratch buffers;   // zero-initialised: no guard
  return &buffers;
}
#endif

}  // namespace op1rings

#endif  // OP1RINGS_SCRATCH_H_
