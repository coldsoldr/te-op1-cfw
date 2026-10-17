// The image's patch table: everything the engine image changes in the app
// besides the phase voice's vtable (render, note-on: the image header).
// tools/inject_app.py checks each stock value before patching. Addresses:
// docs/screen-interface.md §6 (screen) and §7 (encoder clicks).

#include "op1/patches.h"

#ifdef OP1_FX_PROBE
#include "op1/fx_probe.h"
#endif
#ifdef OP1_CLOUDS
#include "op1/clouds_fx.h"
#endif
#include "op1/input.h"
#include "op1/screen.h"

namespace op1 {

namespace {

// The engine browser's list (0x1a02440 -> 0x19707d8, "SELECT id, type FROM
// %s" with %s = synth_types) shows "phase" as "multi". The id still picks
// the engine, whose type is read again by id (0x194031e), so presets and
// slots keep "type":"phase". The same function lists drum_types, fx_types,
// lfo_types and seq_types: none has a 'phase' row, so they're unchanged.
// The Clouds image also shows fx_types' "phone" as "multi" (Clouds or
// phone); only fx_types has a 'phone' row.
#ifdef OP1_CLOUDS
const char kTypeListQuery[] =
    "SELECT id, CASE type WHEN 'phase' THEN 'multi' WHEN 'phone' THEN 'multi' ELSE type END FROM %s";
#else
const char kTypeListQuery[] =
    "SELECT id, CASE type WHEN 'phase' THEN 'multi' ELSE type END FROM %s";
#endif

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
#ifdef OP1_FX_PROBE
  // FX probe (op1/fx_probe.cc): the phone effect's process, mono vtable
  // 0x19112f4 and stereo vtable 0x1911314 (+0x14).
  { kPhoneMonoProcessSlot, kWord32, kPhoneMonoProcess, reinterpret_cast<const void*>(&FxProbeMono) },
  { kPhoneStereoProcessSlot, kWord32, kPhoneStereoProcess, reinterpret_cast<const void*>(&FxProbeStereo) },
#endif
#ifdef OP1_CLOUDS
  // Clouds (../clouds/op1/clouds_fx.cc) in place of the phone effect's
  // process, mono vtable 0x19112f4 and stereo vtable 0x1911314 (+0x14).
  { op1fx::kPhoneMonoProcessSlot, kWord32, op1fx::kPhoneMonoProcess, reinterpret_cast<const void*>(&CloudsFxMono) },
  { op1fx::kPhoneStereoProcessSlot, kWord32, op1fx::kPhoneStereoProcess, reinterpret_cast<const void*>(&CloudsFxStereo) },
  // Clouds' screen in place of phone's (../clouds/op1/clouds_screen.cc):
  // PhoneScreen vtable Update (+0x1c) and Init (+0x24).
  { op1fx::kPhoneScreenUpdateSlot, kWord32, op1fx::kStockPhoneScreenUpdate, reinterpret_cast<const void*>(&CloudsScreenUpdate) },
  { op1fx::kPhoneScreenInitSlot, kWord32, op1fx::kStockPhoneScreenInit, reinterpret_cast<const void*>(&CloudsScreenInit) },
  // Encoder clicks on the master FX page (op1/input.cc). MasterController
  // vtable 0x1910aac: key down (+0x14), shifted key down (+0x1c), key up
  // (+0x24).
  { 0x01910ac0, kWord32, 0x019db7c4, reinterpret_cast<const void*>(&CloudsMasterKeyDown) },
  { 0x01910ac8, kWord32, 0x019d987e, reinterpret_cast<const void*>(&CloudsMasterShiftKeyDown) },
  { 0x01910ad0, kWord32, 0x019db708, reinterpret_cast<const void*>(&CloudsMasterKeyUp) },
  // And on the drum FX page: drum controller vtable 0x190fa64, same
  // offsets (its knob handler 0x1977430 at +0x2c, as the synth's).
  { 0x0190fa78, kWord32, 0x01978008, reinterpret_cast<const void*>(&CloudsDrumKeyDown) },
  { 0x0190fa80, kWord32, 0x019777dc, reinterpret_cast<const void*>(&CloudsDrumShiftKeyDown) },
  { 0x0190fa88, kWord32, 0x019773e4, reinterpret_cast<const void*>(&CloudsDrumKeyUp) },
#endif
};

}  // namespace op1
