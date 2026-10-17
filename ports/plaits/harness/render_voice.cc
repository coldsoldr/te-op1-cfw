// Voice-level renders: the whole Plaits voice (trigger, decay envelope, LPG,
// post-processing, 16-bit output) per engine, to int16 stereo WAVs (left =
// OUT, right = AUX).
//
//   -DCOPY  the copy's Voice (src/voice.cc: upstream's voice with the eight
//           engine2 engines removed; the engines are bit-identical to
//           upstream, `make copy-check`)
//   -DPORT  the fixed-point VoiceFixed (fixed/voice.cc)
//
// Usage: render_voice OUTDIR [engine]
//
// Scenarios (harness/scenarios.h parameters):
//   lpg   - trigger patched (the scenario's gate), LEVEL unpatched: the LPG
//           pings on each trigger (decay 0.5, colour 0.5)
//   free  - nothing patched: the LPG is bypassed (engines' limiters/gains)
//   level - trigger and LEVEL patched (a ramp): the LPG follows LEVEL

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "harness/scenarios.h"

namespace {

struct Int16WavWriter {
  FILE* fp = nullptr;
  uint32_t frames = 0;
  uint32_t rate = 0;
  void U32(uint32_t v) { fwrite(&v, 4, 1, fp); }
  void U16(uint16_t v) { fwrite(&v, 2, 1, fp); }
  void Header() {
    fwrite("RIFF", 1, 4, fp); U32(36 + frames * 4);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp); U32(16); U16(1); U16(2); U32(rate); U32(rate * 4); U16(4); U16(16);
    fwrite("data", 1, 4, fp); U32(frames * 4);
  }
  bool Open(const char* path, uint32_t r) {
    fp = fopen(path, "wb");
    if (!fp) return false;
    rate = r;
    Header();
    return true;
  }
  void Write(const int16_t* interleaved, size_t n) {
    fwrite(interleaved, 4, n, fp);
    frames += n;
  }
  void Close() {
    fseek(fp, 0, SEEK_SET);
    Header();
    fclose(fp);
  }
};

const int kNumVoiceKinds = 3;
const char* const kVoiceKindNames[kNumVoiceKinds] = { "lpg", "free", "level" };

}  // namespace

#ifdef COPY

#include "src/voice.h"
#include "stmlib/utils/random.h"

namespace {

char ram_block[32768];
op1plaits::Voice voice;

class Backend {
 public:
  static int sample_rate() { return 48000; }
  static int block() { return 12; }
  bool Start(int engine) {
    memset(ram_block, 0, sizeof(ram_block));
    stmlib::BufferAllocator allocator(ram_block, sizeof(ram_block));
    voice.Init(&allocator);
    stmlib::Random::Seed(0x21);
    engine_ = engine;
    return true;
  }
  void Render(int kind, const harness::BlockParams& b, int32_t level, int16_t* frames, int size) {
    op1plaits::Patch patch;
    op1plaits::Modulations m;
    memset(&patch, 0, sizeof(patch));
    memset(&m, 0, sizeof(m));
    patch.note = b.note / 256.0f;
    patch.harmonics = b.harmonics / 32768.0f;
    patch.timbre = b.timbre / 32768.0f;
    patch.morph = b.morph / 32768.0f;
    patch.engine = engine_;   // Patch::engine is an int (the quantizer adds the CV)
    patch.decay = 0.5f;
    patch.lpg_colour = 0.5f;
    m.trigger = (b.trigger & harness::kTriggerHigh) ? 1.0f : 0.0f;
    m.trigger_patched = kind != 1;
    m.level_patched = kind == 2;
    m.level = level / 32768.0f;
    voice.Render(patch, m, reinterpret_cast<op1plaits::Voice::Frame*>(frames), size);
  }
 private:
  int engine_;
};

}  // namespace

#else  // PORT

#include "fixed/voice.h"

namespace {

op1plaits::VoiceFixed voice;

class Backend {
 public:
  static int sample_rate() { return PLAITS_SAMPLE_RATE; }
  static int block() { return PLAITS_BLOCK_SIZE; }
  bool Start(int engine) {
    if (!op1plaits::EngineConverted(engine)) return false;
    voice.Init();
    op1plaits::Random::Seed(0x21);
    engine_ = engine;
    return true;
  }
  void Render(int kind, const harness::BlockParams& b, int32_t level, int16_t* frames, int size) {
    op1plaits::VoicePatch patch;
    op1plaits::VoiceModulations m;
    memset(&patch, 0, sizeof(patch));
    memset(&m, 0, sizeof(m));
    patch.note = b.note << 8;
    patch.harmonics = b.harmonics;
    patch.timbre = b.timbre;
    patch.morph = b.morph;
    patch.engine = engine_;
    patch.decay = 16384;
    patch.lpg_colour = 16384;
    m.trigger = (b.trigger & harness::kTriggerHigh) ? 32768 : 0;
    m.trigger_patched = kind != 1;
    m.level_patched = kind == 2;
    m.level = level;
    voice.Render(patch, m, frames, size);
  }
 private:
  int engine_;
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
  int16_t frames[2 * 64];
  for (int engine = first; engine <= last; ++engine) {
    for (int kind = 0; kind < kNumVoiceKinds; ++kind) {
      if (!backend.Start(engine)) continue;
      char path[512];
      snprintf(path, sizeof(path), "%s/%02d_%s_voice_%s.wav", argv[1], engine,
               harness::kEngineNames[engine], kVoiceKindNames[kind]);
      Int16WavWriter wav;
      if (!wav.Open(path, rate)) return 1;
      const int32_t total = blocks_per_second * 3 / 2;
      for (int32_t b = 0; b < total; ++b) {
        harness::BlockParams p;
        // Trigger scenario parameters (gates every 0.25 s); "free" uses the
        // combo scenario (everything moving, no trigger).
        harness::ScenarioParams(kind == 1 ? harness::KIND_COMBO : harness::KIND_TRIGGER, b,
                                blocks_per_second, total, &p);
        const int32_t level = harness::Triangle(b, blocks_per_second);   // 1 Hz ramp
        backend.Render(kind, p, level, frames, block);
        wav.Write(frames, block);
      }
      wav.Close();
    }
  }
  return 0;
}
