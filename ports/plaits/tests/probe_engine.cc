// Debugging aid: one engine, upstream (float) against the port (fixed), with
// constant parameters, at 48 kHz in 12-sample blocks. Prints the SNR of OUT
// and AUX per 100 ms and, optionally, the first samples side by side.
//
// Usage: probe_engine ENGINE NOTE HARMONICS TIMBRE MORPH [TRIGGER] [DUMP]
//   (parameters 0..1; TRIGGER 1 = a patched trigger on the first block, 2 =
//   on the sixth)

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

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
#include "stmlib/utils/random.h"

#include "fixed/engines.h"

namespace {

alignas(16) char engine_storage[65536];
char ram_block[32768];

template<typename T>
plaits::Engine* Make() {
  memset(engine_storage, 0, sizeof(T));
  return new (engine_storage) T();
}

plaits::Engine* MakeEngine(int engine) {
  switch (engine) {
    case 0: return Make<plaits::VirtualAnalogEngine>();
    case 1: return Make<plaits::WaveshapingEngine>();
    case 2: return Make<plaits::FMEngine>();
    case 3: return Make<plaits::GrainEngine>();
    case 4: return Make<plaits::AdditiveEngine>();
    case 5: return Make<plaits::WavetableEngine>();
    case 6: return Make<plaits::ChordEngine>();
    case 7: return Make<plaits::SpeechEngine>();
    case 8: return Make<plaits::SwarmEngine>();
    case 9: return Make<plaits::NoiseEngine>();
    case 10: return Make<plaits::ParticleEngine>();
    case 11: return Make<plaits::StringEngine>();
    case 12: return Make<plaits::ModalEngine>();
    case 13: return Make<plaits::BassDrumEngine>();
    case 14: return Make<plaits::SnareDrumEngine>();
    default: return Make<plaits::HiHatEngine>();
  }
}

op1plaits::EngineHost host;

}  // namespace

int main(int argc, char** argv) {
  if (argc < 6) {
    fprintf(stderr, "usage: %s ENGINE NOTE HARMONICS TIMBRE MORPH [TRIGGER] [DUMP]\n", argv[0]);
    return 1;
  }
  const int engine = atoi(argv[1]);
  const int32_t note = static_cast<int32_t>(atof(argv[2]) * 256.0);
  const int32_t h = static_cast<int32_t>(atof(argv[3]) * 32768.0);
  const int32_t t = static_cast<int32_t>(atof(argv[4]) * 32768.0);
  const int32_t m = static_cast<int32_t>(atof(argv[5]) * 32768.0);
  // TRIGGER: 0 unpatched, 1 = rising edge on block 0, 2 = patched with the
  // edge on block 5 (as the voice's trigger delay presents a first note).
  const int trigger = argc > 6 ? atoi(argv[6]) : 0;
  const int edge_block = trigger == 2 ? 5 : 0;
  const int dump = argc > 7 ? atoi(argv[7]) : 0;

  stmlib::BufferAllocator allocator(ram_block, sizeof(ram_block));
  plaits::Engine* e = MakeEngine(engine);
  e->Init(&allocator);
  e->LoadUserData(NULL);
  e->Reset();
  stmlib::Random::Seed(0x21);
  host.Init(engine);

  const int kBlock = 12;
  const int kBlocks = 48000 / kBlock;
  double sig[2] = { 0, 0 }, err[2] = { 0, 0 };
  int dumped = 0;
  for (int b = 0; b < kBlocks; ++b) {
    plaits::EngineParameters p;
    op1plaits::FixedParameters q;
    p.trigger = q.trigger = trigger
        ? ((b == edge_block ? 1 : 0) | (b >= edge_block && b < 400 + edge_block ? 4 : 0))
        : plaits::TRIGGER_UNPATCHED;
    p.note = note / 256.0f;
    p.harmonics = h / 32768.0f;
    p.timbre = t / 32768.0f;
    p.morph = m / 32768.0f;
    p.accent = 26214 / 32768.0f;
    q.note = note << 8;
    q.harmonics = h;
    q.timbre = t;
    q.morph = m;
    q.accent = 26214;
    float out[kBlock], aux[kBlock];
    int32_t fout[kBlock], faux[kBlock];
    bool env = false;
    e->Render(p, out, aux, kBlock, &env);
    host.Render(q, fout, faux, kBlock);
    for (int i = 0; i < kBlock; ++i) {
      const double a = op1plaits::AudioToFloat(fout[i]);
      const double c = op1plaits::AudioToFloat(faux[i]);
      sig[0] += out[i] * out[i];
      err[0] += (a - out[i]) * (a - out[i]);
      sig[1] += aux[i] * aux[i];
      err[1] += (c - aux[i]) * (c - aux[i]);
      if (dumped < dump) {
        printf("%6d  out %9.5f %9.5f  aux %9.5f %9.5f\n", b * kBlock + i, out[i], a, aux[i], c);
        ++dumped;
      }
    }
    if ((b + 1) % (kBlocks / 10) == 0) {
      printf("t %.1f s  OUT SNR %6.1f dB  AUX SNR %6.1f dB\n", (b + 1) * kBlock / 48000.0,
             10 * std::log10(sig[0] / (err[0] + 1e-30)), 10 * std::log10(sig[1] / (err[1] + 1e-30)));
      sig[0] = sig[1] = err[0] = err[1] = 0;
    }
  }
  return 0;
}
