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

// int16 knobs[8] per synth slot (8 slots); the slot the core plays
// (docs/screen-interface.md §4c).
const uint32_t kKnobs = 0xff8013c4;
const int32_t kNumSlots = 8;
const uint32_t kCoreSlot = 0x01aaac0c;

// Voices render at 88.2 kHz (measured by probe v2; the stock phase engine
// does phase += inc per sample with one cycle per 2^32, so the increments
// are f / 88200 * 2^32).
const int32_t kVoiceSampleRate = 88200;

// The stock phase engine's methods (vtable at 0x191113c).
typedef void (*RenderFn)(void* self, int16_t* out, const int16_t* params);
typedef void (*VoidFn)(void* self);
// Note-on gets R1 = the slot's knobs[8] (docs/screen-interface.md §4c).
typedef void (*NoteOnFn)(void* self, const int16_t* knobs);
const uint32_t kPhaseRender = 0x01a0bc5c;
const uint32_t kPhaseNoteOn = 0x01a0be82;

inline int32_t VoiceIndex(const void* self) {
  uint32_t offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self)) - kVoiceObjectBase;
  // offset / 28 for the six voices' offsets (0..167), without libgcc's
  // division (~500 instructions per call on Blackfin): (offset * 2341) >> 16
  // equals it on that whole range (checked for every offset).
  static_assert(kPhaseVoiceObjectSize == 28 && kNumVoices == 6, "VoiceIndex: the constant is for 28 x 6");
  if (offset >= kPhaseVoiceObjectSize * kNumVoices) return 0;
  return static_cast<int32_t>((offset * 2341u) >> 16);
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

#ifdef OP1_HOST_SIM
// Host simulation (tests/host_probe.cc): a simulated cycle counter.
extern uint32_t host_cycles;
inline uint32_t Cycles() { return host_cycles; }
inline void BusyWait(uint32_t start, uint32_t cycles) { host_cycles = start + cycles; }
#else
inline uint32_t Cycles() {
  uint32_t c;
  __asm__ __volatile__("%0 = CYCLES;" : "=d"(c));
  return c;
}
inline void BusyWait(uint32_t start, uint32_t cycles) {
  while (Cycles() - start < cycles) { }
}
#endif

inline int16_t Saturate16(int32_t x) {
#ifdef __bfin__
  // `<< 16 (S)` then `>>> 16`: the clamp in two instructions (GCC compiles
  // the comparisons below to branches).
  int32_t r;
  __asm__("%0 = %1 << 16 (S);" : "=d"(r) : "d"(x));
  return static_cast<int16_t>(r >> 16);
#else
  return static_cast<int16_t>(x > 32767 ? 32767 : (x < -32768 ? -32768 : x));
#endif
}

// A patch tools/inject_app.py applies to the app, after checking the stock
// value. kWord32 replaces a 32-bit word; kImm16Lo/kImm16Hi replace an
// instruction's 16-bit immediate (at `address`) with the low/high half of
// `value`, so an R.L/R.H pair can be pointed at our data.
struct Patch {
  uint32_t address;
  uint32_t kind;
  uint32_t stock;
  const void* value;      // an address in the image (a link-time constant)
};

const uint32_t kWord32 = 0;
const uint32_t kImm16Lo = 1;
const uint32_t kImm16Hi = 2;

// Image header at the start of the injected region, read by
// tools/inject_app.py to find the entry points.
struct Header {
  uint32_t magic;         // 'O' 'P' '1' 'X'
  uint32_t version;
  RenderFn render;        // render entry
  NoteOnFn note_on;       // note-on entry (null = keep stock)
  VoidFn note_off;        // note-off entry (null = keep stock)
  const Patch* patches;   // further patches (null = none)
  uint32_t num_patches;
  uint32_t reserved;
};

const uint32_t kHeaderMagic = 0x5831504f;  // "OP1X"

}  // namespace op1

#endif  // OP1RINGS_OP1_OP1_H_
