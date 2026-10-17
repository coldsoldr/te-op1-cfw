// FX probe (op1/fx_probe.cc): wraps the phone effect's mono and stereo
// process (vtable words in op1/patches.cc, built with -DOP1_FX_PROBE) to
// measure the effect interface on the device before Clouds.

#ifndef OP1RINGS_OP1_FX_PROBE_H_
#define OP1RINGS_OP1_FX_PROBE_H_

#include <cstdint>

namespace op1 {

// The phone effect (Ptch.cpp). Mono vtable 0x19112f4, stereo 0x1911314:
// process at +0x14. The stereo process runs the mono one on two
// sub-objects (this + 4 on L, this + 0x90 on R); both loop over 64 samples.
typedef void (*FxMonoFn)(void* self, int16_t* buf, const int16_t* params);
typedef void (*FxStereoFn)(void* self, int16_t* l, int16_t* r, const int16_t* params);
const uint32_t kPhoneMonoProcess = 0x01a1bb74;
const uint32_t kPhoneStereoProcess = 0x01a1c25c;
const uint32_t kPhoneMonoProcessSlot = 0x01911308;
const uint32_t kPhoneStereoProcessSlot = 0x01911328;
const int32_t kFxBlockSize = 64;

// Effect parameters, 8 int16 per position (docs/next-projects.md §1).
const uint32_t kFxParamsSynth = 0xff801444;    // + slot x 16, 8 slots
const uint32_t kFxParamsDrum = 0xff8015c4;     // + instrument x 16
const uint32_t kFxParamsMaster = 0xff8016c4;

// Synth voice render calls (op1/engine.cc, RingsRender), for the probe's
// voices-per-block reading.
extern uint32_t render_calls;

}  // namespace op1

extern "C" void FxProbeMono(void* self, int16_t* buf, const int16_t* params);
extern "C" void FxProbeStereo(void* self, int16_t* l, int16_t* r, const int16_t* params);

#endif  // OP1RINGS_OP1_FX_PROBE_H_
