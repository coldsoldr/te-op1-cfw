// Blackfin cycles of one voice per model, and of its parts, per 64-sample
// call at 44.1 kHz (what the engine renders per OP-1 call): where a voice's
// time goes. Steady state, a note ringing, no knob movement unless stated.
//
//   make bench-models

#include <cstdint>
#include <cstdio>

#include "harness/cycles.h"
#include "src/ffloat.h"
#include "src/op1_voice.h"
#include "src/string_voice.h"

namespace {

const int kCall = 64;

op1rings::RingsVoice voice;
op1rings::StringVoice string_voice;
op1rings::MonoLimiter limiter;
op1rings::SharedReverb reverb;
int32_t out[kCall];
int32_t send[kCall];

int32_t Q31(double x) { return op1rings::FFloat(x).ToQ31(); }

const uint32_t kInc = static_cast<uint32_t>(220.0 / 44100.0 * 4294967296.0);

void BenchVoice(const char* name, op1rings::ResonatorModel model, int32_t polyphony, bool turning) {
  voice.Init(16);
  voice.set_polyphony(polyphony);
  voice.set_model(model);
  voice.NoteOn();
  op1rings::VoicePatch patch = { Q31(0.4), Q31(0.6), Q31(0.7), Q31(0.3) };
  harness::CycleStats stats;
  for (int c = 0; c < 400; ++c) {
    if (turning) {
      patch.structure = Q31(0.4) + c * 200000;
      patch.brightness = Q31(0.6) - c * 200000;
      patch.damping = Q31(0.7) - c * 200000;
    }
    uint64_t t0 = harness::ReadCycles();
    for (int i = 0; i < kCall; ++i) send[i] = 0;
    voice.RenderMono(&kInc, patch, out, kCall, send);
    if (c >= 200) stats.Add(harness::ReadCycles() - t0);
  }
  stats.Print(name, kCall);
}

}  // namespace

// MonoLimiterAsm against MonoLimiterLoopC on signals that cross the
// limiter's threshold and its soft-limit range (beyond 4.0).
bool CheckLimiter() {
  int32_t a[kCall], b[kCall];
  int32_t peak_c = 1 << 26, peak_a = 1 << 26;
  uint32_t rng = 3;
  bool same = true;
  uint32_t checksum = 0;
  for (int c = 0; c < 400; ++c) {
    const int shift = 2 + (c / 50) % 4;   // levels from ~0.06 to ~8
    for (int i = 0; i < kCall; ++i) {
      rng = rng * 1664525u + 1013904223u;
      a[i] = b[i] = static_cast<int32_t>(rng) >> shift;
    }
    const int32_t gain = 858993459 + c * 1000000;
    op1rings::MonoLimiterLoopC(a, kCall, &peak_c, gain, 375809638);
    op1rings::MonoLimiterAsm(b, kCall, &peak_a, gain, 375809638);
    for (int i = 0; i < kCall; ++i) {
      if (a[i] != b[i]) same = false;
      checksum = checksum * 31u + static_cast<uint32_t>(a[i]);
    }
    if (peak_c != peak_a) same = false;
  }
  printf("limiter: C vs assembly %s, checksum %08x\n", same ? "identical" : "DIFFERENT",
         static_cast<unsigned>(checksum));
  return same;
}

int main() {
  harness::EnableCycleCounter();
  if (!CheckLimiter()) return 1;
  BenchVoice("modal 16 modes", op1rings::MODEL_MODAL, 4, false);
  BenchVoice("modal 16, turning", op1rings::MODEL_MODAL, 4, true);
  BenchVoice("sympathetic x2", op1rings::MODEL_SYMPATHETIC_STRING, 4, false);
  BenchVoice("sympathetic x2, turning", op1rings::MODEL_SYMPATHETIC_STRING, 4, true);
  BenchVoice("sympathetic x8", op1rings::MODEL_SYMPATHETIC_STRING, 1, false);
  BenchVoice("string", op1rings::MODEL_STRING, 4, false);
  // String + reverb: the voice (its string, its send) and, once per block
  // for every voice, the shared reverb (op1/reverb_bus.h).
  BenchVoice("string + reverb (voice)", op1rings::MODEL_STRING_AND_REVERB, 4, false);
  BenchVoice("string + reverb (voice), turning", op1rings::MODEL_STRING_AND_REVERB, 4, true);
  {
    reverb.Init();
    reverb.Clear();
    op1rings::VoicePatch patch = { Q31(0.4), Q31(0.6), Q31(0.7), Q31(0.3) };
    harness::CycleStats fresh, settled;
    for (int c = 0; c < 400; ++c) {
      for (int i = 0; i < kCall; ++i) out[i] = ((c * 37 + i * 101) % 4096) << 14;
      uint64_t t0 = harness::ReadCycles();
      reverb.Process(patch, out);
      (c < 98 ? fresh : settled).Add(harness::ReadCycles() - t0);
    }
    fresh.Print("  SharedReverb, after a Clear", kCall);
    settled.Print("  SharedReverb, settled", kCall);
  }

  // Parts: StringVoice alone (2 strings, 2 x 32 samples), and the limiter.
  string_voice.Init();
  string_voice.Configure(op1rings::MODEL_SYMPATHETIC_STRING, 4);
  string_voice.Strum(Q31(220.0 / 44100.0), Q31(0.1), Q31(0.3));
  op1rings::VoicePatch patch = { Q31(0.4), Q31(0.6), Q31(0.7), Q31(0.3) };
  harness::CycleStats sv, lim;
  const op1rings::LimiterGains gains = { 268435456, 858993459, op1rings::FFloat(0.8) };
  limiter.Init();
  for (int c = 0; c < 400; ++c) {
    uint64_t t0 = harness::ReadCycles();
    string_voice.Process(Q31(220.0 / 44100.0), patch, out, 32);
    string_voice.Process(Q31(220.0 / 44100.0), patch, out + 32, 32);
    if (c >= 200) sv.Add(harness::ReadCycles() - t0);
    t0 = harness::ReadCycles();
    limiter.Process(out, 32, gains);
    limiter.Process(out + 32, 32, gains);
    if (c >= 200) lim.Add(harness::ReadCycles() - t0);
  }
  sv.Print("  StringVoice x2 strings", kCall);
  lim.Print("  MonoLimiter", kCall);
  return 0;
}
