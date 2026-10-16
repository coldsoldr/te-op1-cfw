// The image's patch table: everything the engine image changes in the app
// besides the phase voice's vtable (render, note-on: the image header).
// tools/inject_app.py checks each stock value before patching. Addresses:
// docs/screen-interface.md §6 (screen) and §7 (encoder clicks).

#include "op1/patches.h"

#include "op1/input.h"
#include "op1/screen.h"

namespace op1 {

namespace {

// The engine browser's list (0x1a02440 -> 0x19707d8, "SELECT id, type FROM
// %s" with %s = synth_types) shows "phase" as "multi". The id still picks
// the engine, whose type is read again by id (0x194031e), so presets and
// slots keep "type":"phase". The same function lists drum_types, fx_types,
// lfo_types and seq_types: none has a 'phase' row, so they're unchanged.
const char kTypeListQuery[] =
    "SELECT id, CASE type WHEN 'phase' THEN 'multi' ELSE type END FROM %s";

}  // namespace

const Patch kPatches[kNumPatches] = {
  // Type list query (0x19707d8): R1.L = 0x42a8 at 0x1970866, R1.H = 0x018d
  // at 0x197086a (the immediates are the instructions' second halfwords)
  // ("SELECT id, type FROM %s") -> kTypeListQuery.
  { 0x01970868, kImm16Lo, 0x42a8, kTypeListQuery },
  { 0x0197086c, kImm16Hi, 0x018d, kTypeListQuery },
  // The MULTI screen (op1/screen.cc): the PdScreen keeps loading "pd.svg".
  // PdScreen vtable 0x191116c: Init (+0x24), Update (+0x1c).
  { 0x01911190, kWord32, 0x01a0cca0, reinterpret_cast<const void*>(&MultiScreenInit) },
  { 0x01911188, kWord32, 0x01a0c0a0, reinterpret_cast<const void*>(&MultiScreenUpdate) },
  // Encoder clicks (op1/input.cc). Synth controller vtable 0x1911994: key
  // down (+0x14), shifted key down (+0x1c), key up (+0x24).
  { 0x019119a8, kWord32, 0x01a51058, reinterpret_cast<const void*>(&RingsKeyDown) },
  { 0x019119b0, kWord32, 0x01a50904, reinterpret_cast<const void*>(&RingsShiftKeyDown) },
  { 0x019119b8, kWord32, 0x01a5056c, reinterpret_cast<const void*>(&RingsKeyUp) },
};

}  // namespace op1
