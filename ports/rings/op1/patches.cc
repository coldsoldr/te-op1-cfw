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
#ifdef OP1_SEQ_PROBE
#include "op1/seq_probe.h"
#endif
#ifdef OP1_MARBLES
#include "op1/marbles_op1.h"
#endif
#ifdef OP1_GRIDS
#include "op1/grids_op1.h"
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
#if defined(OP1_CLOUDS) && defined(OP1_GRIDS) && defined(OP1_GRIDS_RENAME)
// ... and seq_types' "pattern" (Grids or pattern; only seq_types has that
// row). The chooser also looks the current type up by its shown name:
// GridsSeqName answers "multi" there (docs/sequencer-interface.md §12).
// Not built yet: the chooser's CALL (0x1970bea) can't reach region A
// directly (pcrel24 = +-16 MB; ~19.6 MB away), so it needs a trampoline.
const char kTypeListQuery[] =
    "SELECT id, CASE type WHEN 'phase' THEN 'multi' WHEN 'phone' THEN 'multi' WHEN 'pattern' THEN 'multi' "
    "ELSE type END FROM %s";
#elif defined(OP1_CLOUDS)
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
#ifdef OP1_MARBLES
  // Marbles on the tombola sequencer (../marbles/op1/marbles_op1.cc): its
  // player (vtable 0x1911e04), controller (0x1911ce4) and screen (0x1911ec4).
  { op1marbles::kKeyDownSlot, kWord32, op1marbles::kStockKeyDown, reinterpret_cast<const void*>(&MarblesKeyDown) },
  { op1marbles::kKeyUpSlot, kWord32, op1marbles::kStockKeyUp, reinterpret_cast<const void*>(&MarblesKeyUp) },
  { op1marbles::kTickSlot, kWord32, op1marbles::kStockTick, reinterpret_cast<const void*>(&MarblesTick) },
  { op1marbles::kUiFrameSlot, kWord32, op1marbles::kStockUiFrame, reinterpret_cast<const void*>(&MarblesUiFrame) },
  { op1marbles::kAllNotesOffSlot, kWord32, op1marbles::kStockAllNotesOff, reinterpret_cast<const void*>(&MarblesAllNotesOff) },
  { op1marbles::kCtlKeyDownSlot, kWord32, op1marbles::kStockCtlKeyDown, reinterpret_cast<const void*>(&MarblesCtlKeyDown) },
  { op1marbles::kCtlShiftKeyDownSlot, kWord32, op1marbles::kStockCtlShiftKeyDown, reinterpret_cast<const void*>(&MarblesCtlShiftKeyDown) },
  { op1marbles::kCtlKnobSlot, kWord32, op1marbles::kStockCtlKnob, reinterpret_cast<const void*>(&MarblesCtlKnob) },
  { op1marbles::kScreenUpdateSlot, kWord32, op1marbles::kStockScreenUpdate, reinterpret_cast<const void*>(&MarblesScreenUpdate) },
  { op1marbles::kScreenInitSlot, kWord32, op1marbles::kStockScreenInit, reinterpret_cast<const void*>(&MarblesScreenInit) },
#endif
#ifdef OP1_GRIDS
  // Grids on the pattern sequencer, drums only (../grids/op1/grids_op1.cc):
  // its player (vtable 0x19110c4), controller (0x191101c) and screen
  // (0x1911104); and the chooser's name lookup (a CALL, 0x1970bea).
  { op1grids::kKeyDownSlot, kWord32, op1grids::kStockKeyDown, reinterpret_cast<const void*>(&GridsKeyDown) },
  { op1grids::kKeyUpSlot, kWord32, op1grids::kStockKeyUp, reinterpret_cast<const void*>(&GridsKeyUp) },
  { op1grids::kTickSlot, kWord32, op1grids::kStockTick, reinterpret_cast<const void*>(&GridsTick) },
  { op1grids::kUiFrameSlot, kWord32, op1grids::kStockUiFrame, reinterpret_cast<const void*>(&GridsUiFrame) },
  { op1grids::kAllNotesOffSlot, kWord32, op1grids::kStockAllNotesOff, reinterpret_cast<const void*>(&GridsAllNotesOff) },
  { op1grids::kCtlKeyDownSlot, kWord32, op1grids::kStockCtlKeyDown, reinterpret_cast<const void*>(&GridsCtlKeyDown) },
  { op1grids::kCtlShiftKeyDownSlot, kWord32, op1grids::kStockCtlShiftKeyDown, reinterpret_cast<const void*>(&GridsCtlShiftKeyDown) },
  { op1grids::kCtlKeyUpSlot, kWord32, op1grids::kStockCtlKeyUp, reinterpret_cast<const void*>(&GridsCtlKeyUp) },
  { op1grids::kCtlKnobSlot, kWord32, op1grids::kStockCtlKnob, reinterpret_cast<const void*>(&GridsCtlKnob) },
  { op1grids::kScreenUpdateSlot, kWord32, op1grids::kStockScreenUpdate, reinterpret_cast<const void*>(&GridsScreenUpdate) },
  { op1grids::kScreenInitSlot, kWord32, op1grids::kStockScreenInit, reinterpret_cast<const void*>(&GridsScreenInit) },
#ifdef OP1_GRIDS_RENAME
  { op1grids::kChooserNameCall, kCall24, op1grids::kStockSeqName, reinterpret_cast<const void*>(&GridsSeqName) },
#endif
#endif
#ifdef OP1_SEQ_PROBE
  // Sequencer probe (op1/seq_probe.cc): tombola's player (vtable 0x1911e04),
  // controller key down (vtable 0x1911ce4) and screen (vtable 0x1911ec4).
  { op1seq::kKeyDownSlot, kWord32, op1seq::kStockKeyDown, reinterpret_cast<const void*>(&SeqProbeKeyDown) },
  { op1seq::kKeyUpSlot, kWord32, op1seq::kStockKeyUp, reinterpret_cast<const void*>(&SeqProbeKeyUp) },
  { op1seq::kOnLeaveSlot, kWord32, op1seq::kStockOnLeave, reinterpret_cast<const void*>(&SeqProbeOnLeave) },
  { op1seq::kTickSlot, kWord32, op1seq::kStockTick, reinterpret_cast<const void*>(&SeqProbeTick) },
  { op1seq::kUiFrameSlot, kWord32, op1seq::kStockUiFrame, reinterpret_cast<const void*>(&SeqProbeUiFrame) },
  { op1seq::kAllNotesOffSlot, kWord32, op1seq::kStockAllNotesOff, reinterpret_cast<const void*>(&SeqProbeAllNotesOff) },
  { op1seq::kCtlKeyDownSlot, kWord32, op1seq::kStockCtlKeyDown, reinterpret_cast<const void*>(&SeqProbeCtlKeyDown) },
  { op1seq::kScreenUpdateSlot, kWord32, op1seq::kStockScreenUpdate, reinterpret_cast<const void*>(&SeqProbeScreenUpdate) },
  { op1seq::kScreenInitSlot, kWord32, op1seq::kStockScreenInit, reinterpret_cast<const void*>(&SeqProbeScreenInit) },
#endif
};

}  // namespace op1
