// The image's patch table: everything the engine image changes in the app
// besides the phase voice's vtable (render, note-on: the image header).
// tools/inject_app.py checks each stock value before patching. Addresses:
// docs/screen-interface.md §6 (screen) and §7 (encoder clicks).

#include "op1/patches.h"

#ifdef OP1_FX_PROBE
#include "op1/fx_probe.h"
#endif
#ifdef OP1_CLOUDS
#include "fx/op1/fx_host.h"
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
// The Clouds image also shows the effect categories' names for their stock
// types (../fx/op1/fx_host.h): spring as "reverb", cwo as "modulation", punch
// as "dirt" (delay keeps its name); only fx_types has those rows. The FX
// browser finds its current entry by id (0x1a01954, "SELECT id FROM fx_types
// WHERE type='%s'" on the stored name), so the shown name is display only.
#if defined(OP1_CLOUDS) && defined(OP1_GRIDS) && defined(OP1_GRIDS_RENAME)
// ... and seq_types' "pattern" (Grids or pattern; only seq_types has that
// row). The chooser also looks the current type up by its shown name:
// GridsSeqName answers "multi" there (docs/sequencer-interface.md §12).
// Not built yet: the chooser's CALL (0x1970bea) can't reach region A
// directly (pcrel24 = +-16 MB; ~19.6 MB away), so it needs a trampoline.
const char kTypeListQuery[] =
    "SELECT id, CASE type WHEN 'phase' THEN 'multi' WHEN 'spring' THEN 'reverb' WHEN 'cwo' THEN 'modulation' "
    "WHEN 'punch' THEN 'dirt' WHEN 'pattern' THEN 'multi' ELSE type END FROM %s";
#elif defined(OP1_CLOUDS)
const char kTypeListQuery[] =
    "SELECT id, CASE type WHEN 'phase' THEN 'multi' WHEN 'spring' THEN 'reverb' WHEN 'cwo' THEN 'modulation' "
    "WHEN 'punch' THEN 'dirt' ELSE type END FROM %s";
#else
const char kTypeListQuery[] =
    "SELECT id, CASE type WHEN 'phase' THEN 'multi' ELSE type END FROM %s";
#endif

#ifdef OP1_CLOUDS
// The synth patch check (patch.cpp, 0x1a037a8) range-checks each effect
// value against the FX Ranges table (0x1a1df4e) and fails the boot with
// "Invalid synth patch ... error 24" (a fatal error, 0x1919aa8) if one is
// out of range. fx1's engine marker was negative, out of spring's 0..32767
// for values[4..7] (Test 37): a slot saved with it stopped the boot. The
// loop's test after the call, CC = R0 == 0 (0x0c00 at 0x1a040ac), becomes
// CC = R0 < R0 (0x0880, never true), so effect values no longer fail a
// patch; the host migrates fx1's values (../fx/op1/fx_host.cc).
const uint16_t kNeverInvalid = 0x0880;
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
  // The reverb category (../fx/op1/fx_host.cc: spring or Clouds) in place
  // of the spring effect's process, mono vtable 0x19113bc and stereo vtable
  // 0x19113dc (+0x14).
  { op1fx::kSpringMonoProcessSlot, kWord32, op1fx::kSpringMonoProcess, reinterpret_cast<const void*>(&FxReverbMono) },
  { op1fx::kSpringStereoProcessSlot, kWord32, op1fx::kSpringStereoProcess, reinterpret_cast<const void*>(&FxReverbStereo) },
  // Its screen (spring's or an engine's document): spring's screen vtable
  // 0x19113fc, Update (+0x1c) and Init (+0x24).
  { op1fx::kSpringScreenUpdateSlot, kWord32, op1fx::kSpringScreenUpdate, reinterpret_cast<const void*>(&FxReverbScreenUpdate) },
  { op1fx::kSpringScreenInitSlot, kWord32, op1fx::kSpringScreenInit, reinterpret_cast<const void*>(&FxReverbScreenInit) },
  // The dirt category (crush, ../fx/op1/crush_fx.cc) in place of punch's
  // process, mono vtable 0x1910c1c and stereo 0x1910c3c (+0x14), and its
  // screen (vtable 0x1910c5c: Update +0x1c, Init +0x24).
  { op1fx::kPunchMonoProcessSlot, kWord32, op1fx::kPunchMonoProcess, reinterpret_cast<const void*>(&FxDirtMono) },
  { op1fx::kPunchStereoProcessSlot, kWord32, op1fx::kPunchStereoProcess, reinterpret_cast<const void*>(&FxDirtStereo) },
  { op1fx::kPunchScreenUpdateSlot, kWord32, op1fx::kPunchScreenUpdate, reinterpret_cast<const void*>(&FxDirtScreenUpdate) },
  { op1fx::kPunchScreenInitSlot, kWord32, op1fx::kPunchScreenInit, reinterpret_cast<const void*>(&FxDirtScreenInit) },
  // The modulation category in place of cwo's process (mono vtable
  // 0x190f574, stereo 0x190f594, +0x14) and screen (vtable 0x190f53c), and
  // the delay category in place of delay's (0x190fb6c, 0x190fb8c; screen
  // 0x190fc64). With no engine in the image they play stock.
  { op1fx::kCwoMonoProcessSlot, kWord32, op1fx::kCwoMonoProcess, reinterpret_cast<const void*>(&FxModulationMono) },
  { op1fx::kCwoStereoProcessSlot, kWord32, op1fx::kCwoStereoProcess, reinterpret_cast<const void*>(&FxModulationStereo) },
  { op1fx::kCwoScreenUpdateSlot, kWord32, op1fx::kCwoScreenUpdate, reinterpret_cast<const void*>(&FxModulationScreenUpdate) },
  { op1fx::kCwoScreenInitSlot, kWord32, op1fx::kCwoScreenInit, reinterpret_cast<const void*>(&FxModulationScreenInit) },
  { op1fx::kDelayMonoProcessSlot, kWord32, op1fx::kDelayMonoProcess, reinterpret_cast<const void*>(&FxDelayMono) },
  { op1fx::kDelayStereoProcessSlot, kWord32, op1fx::kDelayStereoProcess, reinterpret_cast<const void*>(&FxDelayStereo) },
  { op1fx::kDelayScreenUpdateSlot, kWord32, op1fx::kDelayScreenUpdate, reinterpret_cast<const void*>(&FxDelayScreenUpdate) },
  { op1fx::kDelayScreenInitSlot, kWord32, op1fx::kDelayScreenInit, reinterpret_cast<const void*>(&FxDelayScreenInit) },
  // The synth patch check's effect value test (above).
  { 0x01a040ac, kHalf16, 0x0c00, &kNeverInvalid },
  // Encoder clicks on the master FX page (op1/input.cc). MasterController
  // vtable 0x1910aac: key down (+0x14), shifted key down (+0x1c), key up
  // (+0x24).
  { 0x01910ac0, kWord32, 0x019db7c4, reinterpret_cast<const void*>(&FxMasterKeyDown) },
  { 0x01910ac8, kWord32, 0x019d987e, reinterpret_cast<const void*>(&FxMasterShiftKeyDown) },
  { 0x01910ad0, kWord32, 0x019db708, reinterpret_cast<const void*>(&FxMasterKeyUp) },
  // And on the drum FX page: drum controller vtable 0x190fa64, same
  // offsets (its knob handler 0x1977430 at +0x2c, as the synth's).
  { 0x0190fa78, kWord32, 0x01978008, reinterpret_cast<const void*>(&FxDrumKeyDown) },
  { 0x0190fa80, kWord32, 0x019777dc, reinterpret_cast<const void*>(&FxDrumShiftKeyDown) },
  { 0x0190fa88, kWord32, 0x019773e4, reinterpret_cast<const void*>(&FxDrumKeyUp) },
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
