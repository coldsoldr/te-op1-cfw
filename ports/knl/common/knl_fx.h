// What our effects share as kennel plugins (docs/kennel-roadmap.md phase 1):
// the SDK's view of a position, the knob words, the presses, the screen
// reads, and the instance in the position's scratch buffer.
//
// Positions: 0 synth, 1 drum, 2 master (OP1_FX_MASTER). The master gets the
// stereo call (stereo: true in plugin.json), the instruments the mono one.
//
// Words: params[0..3] are the four knobs, raw words in the filter type's
// ranges (op1_fx_param_q15 normalises them to 0..32767); params[4] is the
// selector (never touched); params[5..7] are the page-2 words a knob press
// edits when its press is OP1_PRESS_PAGE (blue 5, green 6, white 7; orange
// shares 7, so orange is never a page knob here). They're saved with the
// sound, 0..32767, with plugin.json's page2_defaults. OP1_PRESS_LATCH on a
// knob is an unsaved flag (freeze, hold). op1_fx_page[pos][knob] holds
// both: 1 while the knob is on page 2 / latched.
//
// The instance: every core fits the position's 176,460-byte scratch buffer
// (op1_fx_scratch, shared with whichever effect is active there, so it's
// re-initialised in init and never assumed preserved), with the half-rate
// wrapper next to it. Nothing of a core's state is resident in the plugin
// arena; only the shared soft-limit table (1 KB) is.
//
// The screen (graphics thread): op1_fx_knobs_live gives the four knobs as
// the stock screens see them (0..32767, LFO included) for the shown
// position; the page-2 words come from the shown position's stored words.

#ifndef OP1KNL_COMMON_KNL_FX_H_
#define OP1KNL_COMMON_KNL_FX_H_

#include <cstddef>
#include <cstdint>

extern "C" {
#include "op1_plugin.h"
}

namespace knl {

const int32_t kNumPositions = 3;
const int32_t kBlock = OP1_FX_BLOCK;   // 64

inline int32_t Clamp15(int32_t v) { return v < 0 ? 0 : (v > 32767 ? 32767 : v); }

// The four knobs, normalised.
inline void Page1(const int16_t* p, int32_t v[4]) {
  for (int32_t k = 0; k < 4; ++k) v[k] = op1_fx_param_q15(p, k);
}

// A page-2 word (5..7), 0..32767.
inline int32_t Word(const int16_t* p, int32_t k) { return Clamp15(p[k]); }

// A page-2 word that picks one of `count` modes: one step per encoder
// click, cycling. Kennel moves a word by about 1536 per click (21 clicks
// over the range, Test 52), so the word is read in clicks, rounded to the
// nearest, as Rings' model and polyphony are (ports/rings/op1/settings.h).
const int32_t kPageClick = 1536;
inline int32_t ClickMode(int32_t word, int32_t count) { return ((word + kPageClick / 2) / kPageClick) % count; }

// Shift + turn on a continuous page-2 word: fine steps. Kennel's dispatcher
// moves a page-2 word by a click (~1536) whether shift is held or not (the
// stock knobs step by delta / 16 under shift). So the effect gives the fine
// step itself: once a render block it compares the words it follows with
// what it saw last, and a change of up to three clicks while shift is held
// (the stock flag, MainInterface's byte, as the firmware's sequencers read
// it) is rewritten as `step` words a click, in the stored words the
// dispatcher reads for its next click, so the turn continues from there.
// Bigger changes (a sound loading) pass as they are. Not for the words that
// pick a mode (ClickMode): a mode is a click, with or without shift.
const uint32_t kShiftHeld = 0x01dd7f78;
inline bool ShiftHeld() { return *reinterpret_cast<const volatile uint8_t*>(kShiftHeld) != 0; }

const int32_t kFineClick = kPageClick / 4;   // the default fine step: a quarter click

struct Fine {
  int16_t last[kNumPositions][3];
  bool seen[kNumPositions];

  void Reset(int32_t pos) { seen[pos % kNumPositions] = false; }

  // mask: bit k for word 5 + k; step: words a click under shift.
  void Apply(int32_t pos, uint32_t mask, int32_t step = kFineClick) {
    pos %= kNumPositions;
    volatile int16_t* w = const_cast<volatile int16_t*>(op1_fx_knobs(pos));
    if (!seen[pos]) {
      for (int32_t k = 0; k < 3; ++k) last[pos][k] = w[5 + k];
      seen[pos] = true;
      return;
    }
    const bool shift = ShiftHeld();
    for (int32_t k = 0; k < 3; ++k) {
      if (!((mask >> k) & 1)) continue;
      int32_t v = w[5 + k];
      const int32_t prev = last[pos][k];
      if (v == prev) continue;
      const int32_t d = v - prev;
      const int32_t mag = d < 0 ? -d : d;
      const int32_t clicks = (mag + kPageClick / 2) / kPageClick;
      if (shift && clicks <= 3) {
        const int32_t n = clicks < 1 ? 1 : clicks;   // a clamped last click still counts as one
        v = Clamp15(prev + (d < 0 ? -n * step : n * step));
        w[5 + k] = static_cast<int16_t>(v);
      }
      last[pos][k] = static_cast<int16_t>(v);
    }
  }
};

// An input gain page-2 word as a Q14 factor: -6 dB at 0, 0 dB at the centre,
// +6 dB at the top (10^(dB / 20) x 16384, one table entry per dB,
// interpolated). The bitcrusher's and drive's gain; their screens read the
// word in tenths of a dB ((word x 120 + 16383) / 32767 - 60).
inline int32_t GainQ14(int32_t word) {
  static const int32_t kTable[13] = { 8211, 9215, 10338, 11600, 13016, 14605, 16384, 18382, 20626, 23143, 25966, 29135, 32690 };
  const int32_t pos = Clamp15(word) * 12;      // 0 .. 12 x 32767
  const int32_t i = pos / 32767;
  const int32_t frac = pos - i * 32767;
  if (i >= 12) return kTable[12];
  return kTable[i] + (((kTable[i + 1] - kTable[i]) * frac) / 32767);
}

// The project tempo, as the firmware's LFOs read it: the value LFO's
// tempo-synced phase increment at 0x1aadad4, bpm x 1662149.125
// (docs/lfo-interface.md section 4, docs/sequencer-interface.md section 4:
// SetTempo writes it; the stock 2.4.6 address, read like the shift flag).
// Out of 20..400 BPM (a wrong address, a different core) it's taken as
// 120 BPM, so a tempo lock still gives a musical time.
const uint32_t kTempoIncrement = 0x01aadad4;
const uint32_t kTempo120 = 199457895u;   // 120 x 1662149.125
inline uint32_t TempoIncrement() {
  const uint32_t inc = *reinterpret_cast<const volatile uint32_t*>(kTempoIncrement);
  return (inc < 33242983u || inc > 664859650u) ? kTempo120 : inc;   // 20..400 BPM
}

// A latched knob (OP1_PRESS_LATCH), or a knob on page 2.
inline bool Pressed(int32_t pos, int32_t knob) { return op1_fx_page[pos % kNumPositions][knob] != 0; }

// Any of the knobs in `mask` (bit k = knob k) on page 2.
inline bool AnyPage2(int32_t pos, uint32_t mask) {
  for (int32_t k = 0; k < 4; ++k) {
    if (((mask >> k) & 1) && Pressed(pos, k)) return true;
  }
  return false;
}

// The position's instance in its scratch buffer (null if the firmware has
// none: then the effect passes the input through).
template <class T>
inline T* Instance(int32_t pos) {
  static_assert(sizeof(T) <= OP1_FX_SCRATCH_BYTES, "the instance must fit the position's scratch buffer");
  return static_cast<T*>(op1_fx_scratch(pos % kNumPositions));
}

// Zeroes an instance before its Init (the buffer held another effect's
// state; the cores' Init routines assume what a zeroed static would hold).
inline void Zero(void* p, size_t bytes) {
  uint32_t* w = static_cast<uint32_t*>(p);
  for (size_t i = 0; i < bytes / 4; ++i) w[i] = 0;
}

// ---- screen side ----

// The shown position: 0 synth, 1 drum, 2 master.
inline int32_t ScreenPos() { return op1_fx_screen_pos(); }

inline void ScreenPage1(int32_t v[4]) {
  int16_t k[4];
  op1_fx_knobs_live(k);
  for (int32_t i = 0; i < 4; ++i) v[i] = k[i];
}

inline int32_t ScreenWord(int32_t k) { return Clamp15(op1_fx_knobs(ScreenPos())[k]); }

}  // namespace knl

#endif  // OP1KNL_COMMON_KNL_FX_H_
