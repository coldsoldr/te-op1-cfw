// Plaits' chord engine (plaits/dsp/engine/chord_engine.cc and
// chords/chord_bank.cc) in fixed point: five voices (four chord notes and an
// inversion voice), each a divide-down string machine crossfading into a
// wavetable voice.

#ifndef OP1PLAITS_FIXED_CHORD_ENGINE_H_
#define OP1PLAITS_FIXED_CHORD_ENGINE_H_

#include "fixed/chord_oscillators.h"
#include "fixed/fixed_math.h"

namespace op1plaits {

const int kChordNumNotes = 4;
const int kChordNumVoices = kChordNumNotes + 1;
const int kChordNumChords = 11;
const int kChordNumHarmonics = 3;

class ChordEngineFixed {
 public:
  void Init(Allocator* allocator);
  void Reset() { chord_index_ = 0; }
  void Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
              size_t size, bool* already_enveloped);

 private:
  int ComputeChordInversion(int32_t inversion, int32_t* ratios, int32_t* amplitudes) const;

  StringSynthOscillatorFixed divide_down_voice_[kChordNumVoices];
  WavetableOscillatorFixed<15> wavetable_voice_[kChordNumVoices];
  int chord_index_;      // HysteresisQuantizer2 state

  int32_t morph_lp_;     // Q31
  int32_t timbre_lp_;    // Q31
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_CHORD_ENGINE_H_
