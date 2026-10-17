// Clouds as the OP-1's phone effect (op1/clouds_fx.cc): the process entries
// that go in place of phone's (vtable words, ports/rings/op1/patches.cc in
// the -DOP1_CLOUDS image), the encoder clicks on the synth FX page
// (ports/rings/op1/input.cc) and the cycle count the CPU governor reads.

#ifndef OP1CLOUDS_OP1_CLOUDS_FX_H_
#define OP1CLOUDS_OP1_CLOUDS_FX_H_

#include <cstdint>

namespace op1fx {

// The phone effect (Ptch.cpp). Mono vtable 0x19112f4, stereo 0x1911314:
// process at +0x14. Mono: synth and drum position, in place, 64 samples;
// stereo: master, params at [SP+12] (docs/next-projects.md §1, Test 16).
typedef void (*FxMonoFn)(void* self, int16_t* buf, const int16_t* params);
typedef void (*FxStereoFn)(void* self, int16_t* l, int16_t* r, const int16_t* params);
const uint32_t kPhoneMonoProcess = 0x01a1bb74;
const uint32_t kPhoneStereoProcess = 0x01a1c25c;
const uint32_t kPhoneMonoProcessSlot = 0x01911308;
const uint32_t kPhoneStereoProcessSlot = 0x01911328;
const int32_t kFxBlockSize = 64;

// Cycles of the work the image measures (the synth engines' render and
// note-on, every Clouds instance), a running total: the governor of each
// instance reads what the others took since its previous call.
extern uint32_t measured_cycles;

// Encoder clicks, in the order of op1/input.cc's key codes.
enum Click { CLICK_BLUE = 0, CLICK_GREEN = 1, CLICK_WHITE = 2, CLICK_ORANGE = 3 };

// Whether the synth controller is showing the FX page of a slot whose
// effect is phone (the clicks and their releases are then Clouds').
bool SynthFxPageIsPhone();

// A click (direction 1, or -1 with shift) on the synth FX page. Returns
// whether it was consumed: orange always (Clouds <-> phone), the others
// while the slot's effect is Clouds.
bool SynthFxClick(Click click, int32_t direction);

// The same on the master FX page (MasterController subscreen 2) while the
// master effect is phone.
bool MasterFxPageIsPhone();
bool MasterFxClick(Click click, int32_t direction);

}  // namespace op1fx

extern "C" void CloudsFxMono(void* self, int16_t* buf, const int16_t* params);
extern "C" void CloudsFxStereo(void* self, int16_t* l, int16_t* r, const int16_t* params);

#endif  // OP1CLOUDS_OP1_CLOUDS_FX_H_
