// Simulator smoke test of the OP-1 engine entry points (op1/engine.cc), as
// the OP-1 would call them: voice objects at 0xffb00000 + 28 i (only used as
// numbers), 128-sample blocks at 88.2 kHz. Build with -DOP1_SIM (make
// sim-engine): the pitch buffer is an array here instead of 0xff801950.
//
//   sim_engine [OUT.raw]   optionally writes voice 0's first second (s16le,
//                          88.2 kHz mono) for a pitch check
//
// Note-on gets the slot's knobs[8] as on the OP-1: params, then model and
// polyphony (op1/settings.h). SIM_MODEL selects the model (make
// sim-engine-fm builds with 3, the FM voice; make sim-model M=n any model).
// The last scenarios play the polyphony states with fast repeated notes
// (constant stealing and fades); their block times include the note-ons,
// which copy the stolen voice's state.

#include <cstdint>
#include <cstdio>

#include "harness/cycles.h"
#include "op1/op1.h"
#include "op1/settings.h"

#ifndef SIM_MODEL
#define SIM_MODEL 0
#endif

extern "C" void RingsRender(void* self, int16_t* out, const int16_t* params);
extern "C" void RingsNoteOn(void* self, const int16_t* knobs);

namespace op1 { uint32_t sim_pitch_buffer[kBlockSize]; }

static void* Voice(int v) {
  return reinterpret_cast<void*>(op1::kVoiceObjectBase + v * op1::kPhaseVoiceObjectSize);
}

int main(int argc, char** argv) {
  harness::EnableCycleCounter();
  uint32_t* pitch = op1::sim_pitch_buffer;
  const uint32_t kA3 = 10713115;   // 220 Hz: 220 / 88200 * 2^32
  for (int i = 0; i < op1::kBlockSize; ++i) pitch[i] = kA3;
  // .25 .6 .7 .3, then model and polyphony "all".
  int16_t knobs[8] = { 8192, 19660, 22937, 9830, op1::EncodeSetting(SIM_MODEL), 0, 0, 0 };
  const int16_t* params = knobs;
  int16_t out[op1::kBlockSize];

  FILE* raw = argc > 1 ? fopen(argv[1], "wb") : nullptr;

  // One voice playing, five idle (all six rendered every block).
  RingsNoteOn(Voice(0), knobs);
  harness::CycleStats one, six;
  int32_t peak = 0;
  uint32_t checksum = 0;
  const int kBlocks = op1::kVoiceSampleRate / op1::kBlockSize;   // ~1 s
  for (int b = 0; b < kBlocks; ++b) {
    uint64_t t0 = harness::ReadCycles();
    for (int v = 0; v < op1::kNumVoices; ++v) {
      RingsRender(Voice(v), out, params);
      if (v == 0 && raw) fwrite(out, sizeof(out), 1, raw);
      for (int i = 0; i < op1::kBlockSize; ++i) {
        if (v == 0) checksum = checksum * 31u + static_cast<uint16_t>(out[i]);
        int32_t a = out[i] < 0 ? -out[i] : out[i];
        if (a > peak) peak = a;
      }
    }
    if (b > 0 && b <= 100) one.Add(harness::ReadCycles() - t0);
  }
  if (raw) fclose(raw);
  printf("1 voice playing: peak %d, checksum %08x\n", static_cast<int>(peak),
         static_cast<unsigned>(checksum));
  // A 128-sample call at 88.2 kHz lasts as long as 64 samples at 44.1 kHz,
  // which is what Print assumes.
  one.Print("6 renders/block, 1 active", op1::kBlockSize / 2);

  // All six voices playing.
  for (int v = 0; v < op1::kNumVoices; ++v) RingsNoteOn(Voice(v), knobs);
  for (int b = 0; b < 50; ++b) {
    uint64_t t0 = harness::ReadCycles();
    for (int v = 0; v < op1::kNumVoices; ++v) RingsRender(Voice(v), out, params);
    if (b > 0) six.Add(harness::ReadCycles() - t0);
  }
  six.Print("6 renders/block, 6 active", op1::kBlockSize / 2);

  // All six playing while structure, brightness and damping are turned
  // continuously (a step every block, so the coefficients are recomputed in
  // every call): the worst case for knob movement.
  harness::CycleStats turning;
  int16_t moving[4] = { params[0], params[1], params[2], params[3] };
  for (int b = 0; b < 50; ++b) {
    moving[0] = static_cast<int16_t>(8192 + b * 300);
    moving[1] = static_cast<int16_t>(19660 - b * 200);
    moving[2] = static_cast<int16_t>(22937 - b * 200);
    uint64_t t0 = harness::ReadCycles();
    for (int v = 0; v < op1::kNumVoices; ++v) RingsRender(Voice(v), out, moving);
    if (b > 0) turning.Add(harness::ReadCycles() - t0);
  }
  turning.Print("6 active, knobs turning", op1::kBlockSize / 2);

  // Polyphony 1, 2 and 4: a note every 12 blocks (17 ms) on the next OP-1
  // voice, so every note steals and fades; then a six-note chord at once.
  const char* kNames[] = { "", "poly 1 (60 modes), fast notes", "poly 2 (28 modes), fast notes",
                           "poly 4 (12 modes), fast notes" };
  for (int p = op1::POLY_1; p <= op1::POLY_4; ++p) {
    knobs[op1::kPolyKnob] = op1::EncodeSetting(p);
    harness::CycleStats fast, chord, note_on;
    int32_t poly_peak = 0;
    for (int b = 0; b < 240; ++b) {
      uint64_t t0 = harness::ReadCycles();
      if (b % 12 == 0) {
        RingsNoteOn(Voice((b / 12) % op1::kNumVoices), knobs);
        if (b >= 24) note_on.Add(harness::ReadCycles() - t0);
      }
      for (int v = 0; v < op1::kNumVoices; ++v) {
        RingsRender(Voice(v), out, params);
        for (int i = 0; i < op1::kBlockSize; ++i) {
          int32_t a = out[i] < 0 ? -out[i] : out[i];
          if (a > poly_peak) poly_peak = a;
        }
      }
      if (b >= 24) fast.Add(harness::ReadCycles() - t0);
    }
    fast.Print(kNames[p], op1::kBlockSize / 2);
    note_on.Print("  note-on alone", op1::kBlockSize / 2);
    for (int b = 0; b < 12; ++b) {
      uint64_t t0 = harness::ReadCycles();
      if (b == 0) {
        for (int v = 0; v < op1::kNumVoices; ++v) RingsNoteOn(Voice(v), knobs);
      }
      for (int v = 0; v < op1::kNumVoices; ++v) RingsRender(Voice(v), out, params);
      chord.Add(harness::ReadCycles() - t0);
    }
    chord.Print("  then a 6-note chord", op1::kBlockSize / 2);
    printf("  peak %d\n", static_cast<int>(poly_peak));
  }
  return 0;
}
