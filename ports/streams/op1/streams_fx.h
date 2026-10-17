// The Streams effect's OP-1 entry points (op1/streams_fx.cc). Not hooked up
// yet: the glue (vtable words, the host slot's dispatch, the FX page's
// encoder clicks, the screen) is designed in docs/streams-port.md.
//
// Effect ABI (docs/next-projects.md §1, docs/device-tests.md Test 16):
//   mono   process(this, int16* buf, const int16* params)       in place
//   stereo process(this, int16* l, int16* r, const int16* params)  (4th arg
//          on the stack at [SP+12], as the C ABI puts it)
//   init(this, position): 0 synth, 1 drum, 2 master
// 64 samples per call at 44.1 kHz; a switched-off effect isn't called.

#ifndef OP1STREAMS_OP1_STREAMS_FX_H_
#define OP1STREAMS_OP1_STREAMS_FX_H_

#include <stdint.h>

#include "src/core/streams_core.h"

namespace op1streams {

enum Position { POSITION_SYNTH, POSITION_DRUM, POSITION_MASTER, POSITION_LAST };

// fx_params[4..7] (8 int16 per position; stock effects store but never read
// 4..7; factory presets hold 8000 there, op1.db may hold 0). Every word stays
// in 0..32767, the FX Ranges table's range for knobs 4..7, so nothing that
// clamps to that range (the effect LFO's apply function does) can change it:
//   [4] settings word: bit 15 = 0, bits 14..12 = kSettingsMarker (5:
//       "Streams"; any other value, e.g. 8000 = 0x1F40 or 0, means defaults /
//       not Streams), bits 2..0 function (0..5), bit 3 alternate, bit 4 split
//       times, bits 6..5 filter mode, bit 7 detector, bit 8 page (UI only)
//   [5] attack (0..32767)      [6] decay (0..32767)
//   [7] bits 14..8 response (0..127 -> 0..32767), bits 7..0 output trim
//       (0..255, 128 = 0 dB)
// The effect LFO adds its deltas to all 8 values in the copy that process
// most likely receives (docs/lfo-interface.md §5): the glue should pass
// [4..7] from the stored, un-modulated values (docs/streams-port.md §7).
//
// fx_params[0..3] are the four encoders of page A: SHAPE, MOD, SENSE, MIX,
// in the host slot's FX ranges (phone's: kEncoderMin/Max), normalised to
// 0..32767 here.
const uint16_t kSettingsMarker = 5;
// Phone's FX Ranges entries for its four encoders (docs/clouds-port.md §5).
const int16_t kEncoderMin[4] = { 204, 3072, 1536, 0 };
const int16_t kEncoderMax[4] = { 20480, 17408, 16896, 32767 };

// Encoder k's stored value (host range) <-> 0..32767.
int16_t NormaliseEncoder(int k, int16_t raw);
int16_t EncoderRaw(int k, int16_t value);

bool IsStreamsSettings(const int16_t* params);
// Controls from the 8 parameters (defaults where the marker is absent).
void DecodeControls(const int16_t* params, Controls* c);
// The settings word for a controls set (for the glue's click handlers).
int16_t EncodeSettings(const Controls& c, int page);
int16_t EncodeResponseOutput(const Controls& c);

// Elapsed-time source for the restart rule (a gap of > 5 ms between calls
// means the effect was off). Blackfin: the CYCLES counter. Host tests set
// g_fake_cycles.
extern uint32_t g_fake_cycles;
// Fresh starts of a core (first call, after init, after a gap): diagnostics.
extern uint32_t g_restarts;
const uint32_t kGapCycles = 2000000;  // 5 ms at 400 MHz

}  // namespace op1streams

extern "C" {
void StreamsFxInit(void* self, int32_t position);
void StreamsFxMono(void* self, int16_t* buf, const int16_t* params);
void StreamsFxStereo(void* self, int16_t* l, int16_t* r, const int16_t* params);
}

#endif  // OP1STREAMS_OP1_STREAMS_FX_H_
