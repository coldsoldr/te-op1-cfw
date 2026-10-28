// The module mask word (op1/modules.h). It lives in .data with a non-zero
// initialiser, so the ELF holds a file image of it for the builder to
// patch; "used" keeps the linker's section GC from dropping it.

#include "fx/op1/modules.h"

extern "C" {
__attribute__((used)) volatile uint32_t op1_module_mask = 0xffffffffu;
}
