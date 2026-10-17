#include "fixed/engines.h"

namespace op1plaits {

uint32_t Random::state_ = 0x21;

const PostProcessing kPostProcessing[kNumEngines] = {
  { Q(0.8, 28), Q(0.8, 28), false },    // virtual analog
  { Q(0.7, 28), Q(0.6, 28), false },    // waveshaping
  { Q(0.6, 28), Q(0.6, 28), false },    // FM
  { Q(0.7, 28), Q(0.6, 28), false },    // grain
  { Q(0.8, 28), Q(0.8, 28), false },    // additive
  { Q(0.6, 28), Q(0.6, 28), false },    // wavetable
  { Q(0.8, 28), Q(0.8, 28), false },    // chords
  { Q(-0.7, 28), Q(0.8, 28), false },   // speech
  { Q(-3.0, 28), Q(1.0, 28), false },   // swarm
  { Q(-1.0, 28), Q(-1.0, 28), false },  // noise
  { Q(-2.0, 28), Q(1.0, 28), false },   // particle
  { Q(-1.0, 28), Q(0.8, 28), true },    // string
  { Q(-1.0, 28), Q(0.8, 28), true },    // modal
  { Q(0.8, 28), Q(0.8, 28), true },     // bass drum
  { Q(0.8, 28), Q(0.8, 28), true },     // snare drum
  { Q(0.8, 28), Q(0.8, 28), true },     // hi-hat
};

#define OP1PLAITS_FOR_EACH_ENGINE(X_) /* see fixed/engine_list.h */

bool EngineConverted(int engine) {
  switch (engine) {
#define X(index, member) case index: return true;
#include "fixed/engine_list.h"
#undef X
    default: return false;
  }
}

void EngineHost::Init(int engine) {
  engine_ = engine;
  for (size_t i = 0; i < kEngineRamSize / 4; ++i) ram_[i] = 0;
  Allocator allocator;
  allocator.Init(ram_, sizeof(ram_));
  switch (engine) {
#define X(index, member) case index: e_.member.Init(&allocator); break;
#include "fixed/engine_list.h"
#undef X
    default: break;
  }
  Reset();
  Random::Seed(0x21);
}

void EngineHost::Reset() {
  switch (engine_) {
#define X(index, member) case index: e_.member.Reset(); break;
#include "fixed/engine_list.h"
#undef X
    default: break;
  }
}

void EngineHost::Render(const FixedParameters& p, int32_t* out, int32_t* aux,
                        size_t size, bool* already_enveloped) {
  bool enveloped = kPostProcessing[engine_].already_enveloped;
  switch (engine_) {
#define X(index, member) case index: e_.member.Render(p, out, aux, size, &enveloped); break;
#include "fixed/engine_list.h"
#undef X
    default:
      for (size_t i = 0; i < size; ++i) out[i] = aux[i] = 0;
      break;
  }
  if (already_enveloped) *already_enveloped = enveloped;
}

}  // namespace op1plaits
