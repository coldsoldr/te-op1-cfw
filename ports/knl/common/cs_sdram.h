// Fixed SDRAM for our plugins' big state, outside kennel's plugin arena.
//
// Kennel's arena (kn_boot.c, 3.0.3) is 0x02980000..0x02C00000, with the top
// 512 KB as the loader's file staging buffer: 2 MB for every plugin file's
// image and bss, hundekraft's own ~45 files (1.14 MB) included. Rings'
// 573 KB of voice states and reverb, and a Clouds position's 360 KB, don't
// fit beside them (2026-10-26: all twelve of ours, or Rings + one Clouds,
// pushed hundekraft's last files out with "plugin memory full", and the
// inventory text, built in the same arena, failed with "io").
//
// Region A of docs/memory-map.md, 0x02C00000..0x02FFFFFF, is a 4 MB SDRAM
// page the stock app never touches: not loaded, not in the heap table, no
// constant points into it; our firmware ran its whole image and bss there
// (0x02C00000..0x02DECA08, Tests 1..49). Kennel's core uses 0x01E00000..
// for its code and 0x01EC0000.. for its bss, and nothing above the arena
// (docs/plugin-format.md calls 0x02980000..0x03000000 the "planned plugin
// block", so a future core could grow the arena into this page: these
// plugins are tied to cores whose arena ends at 0x02C00000; check KN_ARENA_HI
// in kn_boot.c before using them on a newer one). 0x03000000 is the second
// display buffer; 0x02F00000..0x02FFFFFF stays empty as a guard below it.
//
// The memory is not zeroed for us (bss is; this isn't): every user clears
// what it takes before the first use, and never assumes another plugin
// left it alone between its own uses (the Clouds slots are per position,
// shared by cs_clouds and cs_clouds_delay: only one effect runs at a
// position at a time, and the plugin that takes it over clears it).

#ifndef OP1KNL_COMMON_CS_SDRAM_H_
#define OP1KNL_COMMON_CS_SDRAM_H_

#include <cstdint>

namespace cssdram {

const uint32_t kBase = 0x02C00000u;
const uint32_t kEnd = 0x02F00000u;   // the guard above is kept empty

// rings: six voice states and the shared reverb (ports/rings/op1/engine.cc).
const uint32_t kRings = kBase;
const uint32_t kRingsSize = 0x90000u;   // 576 KB; 571,788 used (2026-10-26)

// cs_clouds / cs_clouds_delay: one CloudsCore per effect position (synth,
// drum, master), whichever Clouds plugin runs there.
const uint32_t kCloudsSlot = 0x60000u;   // 384 KB each
const uint32_t kClouds = kRings + kRingsSize;
const uint32_t kNumCloudsSlots = 3;

const uint32_t kUsedEnd = kClouds + kNumCloudsSlots * kCloudsSlot;   // 0x02DB0000
static_assert(kUsedEnd <= kEnd, "cs_sdram: past the guard");

inline void Zero(uint32_t address, uint32_t bytes) {
  volatile uint32_t* p = reinterpret_cast<volatile uint32_t*>(address);
  for (uint32_t i = 0; i < bytes / 4; ++i) p[i] = 0;
}

}  // namespace cssdram

#endif  // OP1KNL_COMMON_CS_SDRAM_H_
