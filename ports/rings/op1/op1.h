// Shared definitions for code injected into the OP-1 app (firmware 246).
// Addresses and conventions: docs/engine-interface.md.

#ifndef OP1RINGS_OP1_OP1_H_
#define OP1RINGS_OP1_OP1_H_

#include <cstdint>

namespace op1 {

// Synth voice objects live in the L1 scratchpad; the phase engine's are 28
// bytes each, 6 voices.
const uint32_t kVoiceObjectBase = 0xffb00000;
const uint32_t kPhaseVoiceObjectSize = 28;
const int32_t kNumVoices = 6;

// Per-sample phase increments (f / fs * 2^32) for the voice being rendered.
const uint32_t kPitchBuffer = 0xff801950;
const int32_t kBlockSize = 128;

// The stock phase engine's methods (vtable at 0x191113c).
typedef void (*RenderFn)(void* self, int16_t* out, const int16_t* params);
typedef void (*VoidFn)(void* self);
const uint32_t kPhaseRender = 0x01a0bc5c;
const uint32_t kPhaseNoteOn = 0x01a0be82;

inline int32_t VoiceIndex(const void* self) {
  uint32_t offset = reinterpret_cast<uint32_t>(self) - kVoiceObjectBase;
  uint32_t v = offset / kPhaseVoiceObjectSize;
  return v < static_cast<uint32_t>(kNumVoices) ? static_cast<int32_t>(v) : 0;
}

#ifdef OP1_SIM
// Simulator builds (tests/sim_engine.cc): L1 isn't mapped in bfin-elf-run's
// user environment, so the pitch buffer is an ordinary array.
extern uint32_t sim_pitch_buffer[kBlockSize];
inline const uint32_t* PitchBuffer() { return sim_pitch_buffer; }
#else
inline const uint32_t* PitchBuffer() {
  return reinterpret_cast<const uint32_t*>(kPitchBuffer);
}
#endif

inline uint32_t Cycles() {
  uint32_t c;
  __asm__ __volatile__("%0 = CYCLES;" : "=d"(c));
  return c;
}

inline int16_t Saturate16(int32_t x) {
  return static_cast<int16_t>(x > 32767 ? 32767 : (x < -32768 ? -32768 : x));
}

// Image header at the start of the injected region, read by
// tools/inject_app.py to find the entry points.
struct Header {
  uint32_t magic;         // 'O' 'P' '1' 'X'
  uint32_t version;
  RenderFn render;        // render entry
  VoidFn note_on;         // note-on entry (null = keep stock)
  VoidFn note_off;        // note-off entry (null = keep stock)
  uint32_t reserved[3];
};

const uint32_t kHeaderMagic = 0x5831504f;  // "OP1X"

}  // namespace op1

#endif  // OP1RINGS_OP1_OP1_H_
