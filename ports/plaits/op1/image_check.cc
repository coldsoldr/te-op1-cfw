// A freestanding OP-1 image of the Plaits glue alone (make image): an OP1X
// header pointing at PlaitsRender / PlaitsNoteOn, linked with the Rings
// image's link script and support code, to check what the real MULTI image
// would get: section sizes, no run-time initialisers (.init_array/.ctors
// must stay empty: the image has no startup code), no soft-float. Not for
// the device: the MULTI dispatcher (ports/rings/op1/engine.cc) owns the
// header and vtable hooks.

#include "../../rings/op1/op1.h"
#include "op1/plaits_op1.h"

extern "C" __attribute__((section(".op1header"), used))
const op1::Header op1_header = {
  op1::kHeaderMagic, 3, &PlaitsRender, &PlaitsNoteOn, nullptr, nullptr, 0, 0,
};
