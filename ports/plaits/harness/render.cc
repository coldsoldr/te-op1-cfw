// Renders the harness scenarios (harness/scenarios.h) for every engine, at the
// engine level (EngineParameters in, out/aux out), to stereo float WAVs:
// left = OUT, right = AUX.
//
//   -DREF   upstream Plaits (mutableinstruments/plaits, namespace plaits)
//   -DCOPY  the copy in src/ (namespace op1plaits): must be bit-identical
//   -DPORT  the fixed-point port (fixed/), converted engines only
//
// Usage: render OUTDIR [engine]   (engine: index 0..15, default all)
//
// The float builds run at 48 kHz in 12-sample blocks, as Plaits does. The
// port runs at PLAITS_SAMPLE_RATE in PLAITS_BLOCK_SIZE blocks (48000 / 12 for
// the comparable build).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "../../rings/harness/wav.h"
#include "harness/scenarios.h"

#ifndef REAL_FLOAT_DEFINED
typedef float real_float;   // the WAV sample type (make ref64 turns float into double)
#endif

#if defined(REF) || defined(COPY)

#ifdef REF
#include "plaits/dsp/engine/additive_engine.h"
#include "plaits/dsp/engine/bass_drum_engine.h"
#include "plaits/dsp/engine/chord_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/grain_engine.h"
#include "plaits/dsp/engine/hi_hat_engine.h"
#include "plaits/dsp/engine/modal_engine.h"
#include "plaits/dsp/engine/noise_engine.h"
#include "plaits/dsp/engine/particle_engine.h"
#include "plaits/dsp/engine/snare_drum_engine.h"
#include "plaits/dsp/engine/speech_engine.h"
#include "plaits/dsp/engine/string_engine.h"
#include "plaits/dsp/engine/swarm_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/wavetable_engine.h"
namespace P = plaits;
#else
#include "src/engine/additive_engine.h"
#include "src/engine/bass_drum_engine.h"
#include "src/engine/chord_engine.h"
#include "src/engine/fm_engine.h"
#include "src/engine/grain_engine.h"
#include "src/engine/hi_hat_engine.h"
#include "src/engine/modal_engine.h"
#include "src/engine/noise_engine.h"
#include "src/engine/particle_engine.h"
#include "src/engine/snare_drum_engine.h"
#include "src/engine/speech_engine.h"
#include "src/engine/string_engine.h"
#include "src/engine/swarm_engine.h"
#include "src/engine/virtual_analog_engine.h"
#include "src/engine/waveshaping_engine.h"
#include "src/engine/wavetable_engine.h"
namespace P = op1plaits;
#endif

#include "stmlib/utils/random.h"

namespace {

const int kSampleRate = 48000;
const int kBlock = 12;

char ram_block[32768];

// Each scenario gets a fresh engine object in zeroed memory: some upstream
// engines leave members uninitialised in Init (FMEngine's downsampler
// states), which would otherwise carry over from the previous scenario.
alignas(16) char engine_storage[65536];

template<typename T>
P::Engine* Make() {
  static_assert(sizeof(T) <= sizeof(engine_storage), "engine_storage too small");
  memset(engine_storage, 0, sizeof(T));
  return new (engine_storage) T();
}

P::Engine* MakeEngine(int engine) {
  switch (engine) {
    case 0: return Make<P::VirtualAnalogEngine>();
    case 1: return Make<P::WaveshapingEngine>();
    case 2: return Make<P::FMEngine>();
    case 3: return Make<P::GrainEngine>();
    case 4: return Make<P::AdditiveEngine>();
    case 5: return Make<P::WavetableEngine>();
    case 6: return Make<P::ChordEngine>();
    case 7: return Make<P::SpeechEngine>();
    case 8: return Make<P::SwarmEngine>();
    case 9: return Make<P::NoiseEngine>();
    case 10: return Make<P::ParticleEngine>();
    case 11: return Make<P::StringEngine>();
    case 12: return Make<P::ModalEngine>();
    case 13: return Make<P::BassDrumEngine>();
    case 14: return Make<P::SnareDrumEngine>();
    default: return Make<P::HiHatEngine>();
  }
}

class Backend {
 public:
  static int sample_rate() { return kSampleRate; }
  static int block() { return kBlock; }
  bool Start(int engine) {
    memset(ram_block, 0, sizeof(ram_block));
    stmlib::BufferAllocator allocator(ram_block, sizeof(ram_block));
    e_ = MakeEngine(engine);
    e_->Init(&allocator);
    e_->LoadUserData(NULL);
    e_->Reset();
    stmlib::Random::Seed(0x21);
    return true;
  }
  void Render(const harness::BlockParams& b, real_float* out, real_float* aux, int size) {
    P::EngineParameters p;
    p.trigger = b.trigger;
    p.note = static_cast<float>(b.note) / 256.0f;
    p.harmonics = static_cast<float>(b.harmonics) / 32768.0f;
    p.timbre = static_cast<float>(b.timbre) / 32768.0f;
    p.morph = static_cast<float>(b.morph) / 32768.0f;
    p.accent = static_cast<float>(b.accent) / 32768.0f;
    bool already_enveloped = false;
    float o[64], a[64];
    e_->Render(p, o, a, size, &already_enveloped);
    for (int i = 0; i < size; ++i) {
      out[i] = static_cast<real_float>(o[i]);
      aux[i] = static_cast<real_float>(a[i]);
    }
  }
 private:
  P::Engine* e_;
};

}  // namespace

#elif defined(PORT)

#include "fixed/engines.h"

namespace {

class Backend {
 public:
  static int sample_rate() { return PLAITS_SAMPLE_RATE; }
  static int block() { return PLAITS_BLOCK_SIZE; }
  bool Start(int engine) {
    if (!op1plaits::EngineConverted(engine)) return false;
    engine_ = engine;
    host_.Init(engine);
    return true;
  }
  void Render(const harness::BlockParams& b, real_float* out, real_float* aux, int size) {
    op1plaits::FixedParameters p;
    p.trigger = b.trigger;
    p.note = b.note << 8;   // 1/256 semitone -> Q16
    p.harmonics = b.harmonics;
    p.timbre = b.timbre;
    p.morph = b.morph;
    p.accent = b.accent;
    host_.Render(p, out_, aux_, size);
    for (int i = 0; i < size; ++i) {
      out[i] = op1plaits::AudioToFloat(out_[i]);
      aux[i] = op1plaits::AudioToFloat(aux_[i]);
    }
  }
 private:
  int engine_;
  op1plaits::EngineHost host_;
  int32_t out_[64];
  int32_t aux_[64];
};

}  // namespace

#endif

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s OUTDIR [engine]\n", argv[0]);
    return 1;
  }
  int first = 0, last = 15;
  if (argc >= 3) first = last = atoi(argv[2]);
  static Backend backend;
  const int rate = Backend::sample_rate();
  const int block = Backend::block();
  const int blocks_per_second = rate / block;
  real_float out[64], aux[64];
  for (int engine = first; engine <= last; ++engine) {
    for (int kind = 0; kind < harness::kNumKinds; ++kind) {
      if (!backend.Start(engine)) continue;
      char path[512];
      snprintf(path, sizeof(path), "%s/%02d_%s_%s.wav", argv[1], engine,
               harness::kEngineNames[engine], harness::kKindNames[kind]);
      harness::FloatWavWriter wav;
      if (!wav.Open(path, rate)) {
        fprintf(stderr, "can't open %s\n", path);
        return 1;
      }
      const int32_t total = static_cast<int32_t>(
          harness::ScenarioSeconds(kind) * blocks_per_second);
      for (int32_t b = 0; b < total; ++b) {
        harness::BlockParams p;
        harness::ScenarioParams(kind, b, blocks_per_second, total, &p);
        backend.Render(p, out, aux, block);
        wav.Write(out, aux, block);
      }
      wav.Close();
    }
  }
  return 0;
}
