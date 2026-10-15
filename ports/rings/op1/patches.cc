// The image's patch table: everything the engine image changes in the app
// besides the phase voice's vtable (render, note-on: the image header).
// tools/inject_app.py checks each stock value before patching. Addresses:
// docs/screen-interface.md §6 (screen) and §7 (encoder clicks).

#include "op1/patches.h"

#include "op1/input.h"
#include "op1/screen.h"

namespace op1 {

const Patch kPatches[kNumPatches] = {
  // Rings screen (op1/screen.cc). PdScreen getter 0x1a0cd50:
  // R2.L = 0x2e40; R2.H = 0x018f ("pd.svg") -> "rings.svg".
  { 0x01a0cd8e, kImm16Lo, 0x2e40, kRingsSvgName },
  { 0x01a0cd92, kImm16Hi, 0x018f, kRingsSvgName },
  // PdScreen vtable 0x191116c: Init (+0x24), Update (+0x1c).
  { 0x01911190, kWord32, 0x01a0cca0, reinterpret_cast<const void*>(&RingsScreenInit) },
  { 0x01911188, kWord32, 0x01a0c0a0, reinterpret_cast<const void*>(&RingsScreenUpdate) },
  // Encoder clicks (op1/input.cc). Synth controller vtable 0x1911994: key
  // down (+0x14), shifted key down (+0x1c), key up (+0x24).
  { 0x019119a8, kWord32, 0x01a51058, reinterpret_cast<const void*>(&RingsKeyDown) },
  { 0x019119b0, kWord32, 0x01a50904, reinterpret_cast<const void*>(&RingsShiftKeyDown) },
  { 0x019119b8, kWord32, 0x01a5056c, reinterpret_cast<const void*>(&RingsKeyUp) },
};

}  // namespace op1
