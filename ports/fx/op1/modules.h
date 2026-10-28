// The module switch (docs/librarian-handover.md Part 0): one data word in
// the image, a bit per switchable module, all set by default. The builder
// app patches it in the ELF before injection (tools/inject_app.py
// --set-word op1_module_mask=0x...), so every firmware the app makes runs
// the same compiled code and only the mask differs.
//
// A module whose bit is clear behaves as if it weren't in the image: an
// effect engine is skipped by the orange click and a slot saved on it
// plays stock; Rings' clicks are ignored and a MULTI slot plays phase;
// Marbles and Grids stay stock tombola and pattern. Every screen file still
// ships (a missing one could assert at boot); the ones of disabled modules
// just aren't loaded.
//
// The bit layout is part of the app's manifest (app/manifests): keep it.

#ifndef OP1FX_OP1_MODULES_H_
#define OP1FX_OP1_MODULES_H_

#include <cstdint>

extern "C" {
extern volatile uint32_t op1_module_mask;
}

namespace op1 {

enum Module {
  MODULE_PLATE = 0,
  MODULE_HALL = 1,
  MODULE_CLOUDS = 2,
  MODULE_DRIVE = 3,
  MODULE_TAPE = 4,
  MODULE_FOLD = 5,
  MODULE_CRUSH = 6,
  MODULE_ENSEMBLE = 7,
  MODULE_SHIFT = 8,
  MODULE_TAPE_ECHO = 9,
  MODULE_RINGS = 10,
  MODULE_MARBLES = 11,
  MODULE_GRIDS = 12,
  kNumModules = 13
};

const uint32_t kAllModules = (1u << kNumModules) - 1;

inline bool ModuleEnabled(Module m) { return (op1_module_mask >> m) & 1u; }

}  // namespace op1

#endif  // OP1FX_OP1_MODULES_H_
