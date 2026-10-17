// The fixed-point engines behind one dispatcher (no virtual functions: the
// OP-1 image has no run-time initialisers, and a switch keeps every object
// trivially constructible). Engine indices are Plaits' original 16:
//
//   0 virtual analog   4 additive    8 swarm      12 modal
//   1 waveshaping      5 wavetable   9 noise      13 bass drum
//   2 FM               6 chords     10 particle   14 snare drum
//   3 grain/formant    7 speech     11 string     15 hi-hat

#ifndef OP1PLAITS_FIXED_ENGINES_H_
#define OP1PLAITS_FIXED_ENGINES_H_

#include "fixed/fixed_math.h"
#include "fixed/additive_engine.h"
#include "fixed/bass_drum_engine.h"
#include "fixed/chord_engine.h"
#include "fixed/fm_engine.h"
#include "fixed/grain_engine.h"
#include "fixed/hi_hat_engine.h"
#include "fixed/modal_engine.h"
#include "fixed/noise_engine.h"
#include "fixed/particle_engine.h"
#include "fixed/snare_drum_engine.h"
#include "fixed/speech_engine.h"
#include "fixed/string_engine.h"
#include "fixed/swarm_engine.h"
#include "fixed/virtual_analog_engine.h"
#include "fixed/waveshaping_engine.h"
#include "fixed/wavetable_engine.h"

namespace op1plaits {

const int kNumEngines = 16;

// True for the engines converted so far.
bool EngineConverted(int engine);

// Upstream's post-processing settings per engine (voice.cc
// RegisterInstance): a negative gain means a limiter is used.
struct PostProcessing {
  int32_t out_gain;   // Q28 (|gain| <= 3)
  int32_t aux_gain;
  bool already_enveloped;
};
extern const PostProcessing kPostProcessing[kNumEngines];

// Scratch memory the engines allocate their buffers from (upstream: 16 KB).
const size_t kEngineRamSize = 16384 + 1024;

// One engine instance: whichever is selected (they share the memory, as on
// Plaits, where every engine is re-initialised from the same RAM block).
class EngineHost {
 public:
  void Init(int engine);
  void Reset();
  void Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
              size_t size, bool* already_enveloped = nullptr);
  int engine() const { return engine_; }

 private:
  int engine_;
  union {
    VirtualAnalogEngineFixed va;
    FMEngineFixed fm;
    WavetableEngineFixed wavetable;
    ChordEngineFixed chords;
    NoiseEngineFixed noise;
    AdditiveEngineFixed additive;
    ParticleEngineFixed particle;
    BassDrumEngineFixed bass_drum;
    SnareDrumEngineFixed snare;
    HiHatEngineFixed hi_hat;
    GrainEngineFixed grain;
    SwarmEngineFixed swarm;
    StringEngineFixed string_engine;
    ModalEngineFixed modal;
    WaveshapingEngineFixed waveshaping;
    SpeechEngineFixed speech;
  } e_;
  uint32_t ram_[kEngineRamSize / 4];
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_ENGINES_H_
