// What our sequencer features share as kennel plugins (docs/kennel-roadmap.md
// phase 3; the SDK's op1_feature_seq.h): the R246 state a sequencer type
// reads directly (as hundekraft's own euclid, dots and c64arp do), the notes
// through the core's op1_seq_note_on / op1_seq_note_off, and the player
// object (common/knl_seq_player.c: R246's ok_player vtable shape, its entries
// the five knl_seq_* functions the plugin defines).
//
// A kennel sequencer runs as R246's type 3 (finger) plus "which type is
// active" per mode, so one player object serves both modes: Mode() says
// which instrument's turn it is, and a plugin keeps one core per mode.
//
// Threads, as the stock sequencers: key_down / key_up on the KeyHandler
// thread (only while the sequencer is on), tick on the sequencer thread (96
// per beat, the counter's phase in half a beat as before), the type's key /
// press / turn on the KeyHandler thread while its screen is shown, the
// screen on the Gfx thread. Notes are played from tick and from reset /
// stop, which the core calls where the stock players silence theirs.
//
// Host builds (-DOP1_HOST, the tests in ports/knl/tests) get the same names
// from common/knl_seq_host.h instead: plain variables for the mode, the
// octaves and shift, and a log of the notes.

#ifndef OP1KNL_COMMON_KNL_SEQ_H_
#define OP1KNL_COMMON_KNL_SEQ_H_

#include <stdint.h>

#ifdef OP1_HOST
#include "common/knl_seq_host.h"
#else
extern "C" {
#include "op1_feature_seq.h"
#include "seq/seq_text.h"
}
#endif

extern "C" {
// The player's vtable entries (knl_seq_player.c), one set per plugin.
int knl_seq_key_down(void* self, int key, int flag);   // 1 = consumed (key_up follows)
void knl_seq_key_up(void* self, int key);
void knl_seq_reset(void* self);
void knl_seq_tick(void* self, int t);
void knl_seq_stop(void* self);
void* knl_seq_player_object(void);
}

namespace knl {
namespace seq {

#ifndef OP1_HOST
// R246's state (the addresses hundekraft's sequencers read: kn_seq.c, euclid.c).
inline int Mode() { return *reinterpret_cast<const volatile int32_t*>(0x01DE4C40) == 1 ? 1 : 0; }
inline int SynthOctave() {
  return reinterpret_cast<const int32_t*>(0x01DFE4E0)[*reinterpret_cast<const volatile int32_t*>(0xFF8016D4) & 7];
}
inline int DrumOctave() {
  return reinterpret_cast<const int32_t*>(0x01DFE500)[*reinterpret_cast<const volatile int32_t*>(0xFF8016D8) & 7];
}
inline bool ShiftHeld() { return *reinterpret_cast<const volatile uint8_t*>(0x01DD7F78) != 0; }
// The OP-1's own random generator, for a seed.
inline uint32_t Entropy() { return *reinterpret_cast<const volatile uint32_t*>(0xff802bc8); }
#endif

const int32_t kNumKeys = 24;

// The MIDI note a plain press of key k gives in mode m (0x35 = key 0 at
// octave 0, the keyboard's own arithmetic).
inline int32_t KeyNote(int mode, int32_t key) {
  return 0x35 + key + (mode ? DrumOctave() : SynthOctave()) * 12;
}

// A note on mode's instrument. Synth: `value` is a pitch in semitones above
// key 0 at the current octave (any value; outside MIDI nothing plays). Drum:
// `value` is the sound, a key 0..23 (other values wrap onto the keyboard),
// at its own pitch. Returns the id op1_seq_note_off takes, or -1.
inline int32_t NoteOn(int mode, int32_t value, int32_t velocity) {
  if (velocity < 1) velocity = 1;
  if (velocity > 127) velocity = 127;
  if (mode) {
    const int32_t key = ((value % kNumKeys) + kNumKeys) % kNumKeys;
    return op1_seq_note_on(1, KeyNote(1, key), key, velocity);
  }
  const int32_t midi = KeyNote(0, value);
  if (midi < 0 || midi > 127) return -1;
  return op1_seq_note_on(0, midi, value, velocity);
}

inline void NoteOff(int32_t id) {
  if (id >= 0) op1_seq_note_off(id);
}

// The phase of a tick in half a beat (the player's counter, 0..47; wrapped
// in case a core counts further).
inline int32_t Phase(int t) { return ((t % 48) + 48) % 48; }

// Encoder clicks (the OP-1's 0x3b..0x3e) as the type's press gives them.
const int kKnobBlue = 0;
const int kKnobGreen = 1;
const int kKnobWhite = 2;
const int kKnobOrange = 3;

// Keys a sequencer type consumes on its screen: lift, drop and scissors
// (0x0a..0x0c), which finger's controller would otherwise act on (as
// hundekraft's peal and c64arp do).
inline bool ConsumedKey(int code) { return code >= 0x0a && code <= 0x0c; }

// Settings text: Q16 values travel as 0..8191 (sq_join takes 0..9999).
inline int Q16ToText(int32_t v) { return v < 0 ? 0 : (v > 65535 ? 8191 : v >> 3); }
inline int32_t TextToQ16(int v) { return v < 0 ? 0 : (v >= 8191 ? 65535 : v << 3); }   // the top value = full scale
inline bool InRange(int v, int lo, int hi) { return v >= lo && v <= hi; }

}  // namespace seq
}  // namespace knl

#endif  // OP1KNL_COMMON_KNL_SEQ_H_
