// The image's patch table (op1/patches.cc), applied to the app by
// tools/inject_app.py after checking each stock value.

#ifndef OP1RINGS_OP1_PATCHES_H_
#define OP1RINGS_OP1_PATCHES_H_

#include <cstdint>

#include "op1/op1.h"

namespace op1 {

#if defined(OP1_FX_PROBE)
const uint32_t kNumPatches = 9;   // + the phone effect's process (op1/fx_probe.cc)
#elif defined(OP1_CLOUDS) && defined(OP1_MARBLES) && defined(OP1_GRIDS)
const uint32_t kNumPatches = 51;  // + Grids on pattern for drums (../grids/op1/grids_op1.cc)
#elif defined(OP1_CLOUDS) && defined(OP1_MARBLES)
const uint32_t kNumPatches = 40;  // the Clouds image + Marbles on tombola (../marbles/op1/marbles_op1.cc)
#elif defined(OP1_CLOUDS) && defined(OP1_SEQ_PROBE)
const uint32_t kNumPatches = 39;  // the Clouds image + the tombola sequencer probe (op1/seq_probe.cc)
#elif defined(OP1_CLOUDS)
const uint32_t kNumPatches = 30;  // + the four categories' process and screen (Clouds, crush,
                                  //   ../fx/op1/), the patch check and the master and drum pages' key handlers
#else
const uint32_t kNumPatches = 7;
#endif
extern const Patch kPatches[kNumPatches];

}  // namespace op1

#endif  // OP1RINGS_OP1_PATCHES_H_
