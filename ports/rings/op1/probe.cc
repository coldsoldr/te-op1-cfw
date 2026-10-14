// Probe build: measures the OP-1's free CPU from inside the phase engine and
// reports it as tones. Replaces the phase engine's render (vtable +0x14).
//
// While a key is held:
//   1. ~2 s: plays the stock phase sound and measures the audio block period
//      in CPU cycles (the shortest gap between blocks).
//   2. Ramp: each block, busy-waits B cycles, raising B by 0.5% of the period
//      every 64 blocks, until a block arrives > 30% late (overload). It then
//      stops burning and keeps the last good B.
//   3. Report, repeating, on every voice (so hold a key to hear it):
//        tone 1: clock in MHz, as Hz         (e.g. 400 MHz -> 400 Hz)
//        tone 2: free CPU in MHz, as Hz      (the largest B that kept up)
//        tone 3: 100 * (fewest voices rendered in any block) + 50 Hz
//      1.5 s each with 0.5 s gaps, then 2 s of silence.
// Throughout: the rest of the injected region holds a fill pattern
// (written by the LDR). Each call checks 64 words of it; if any word changed,
// a two-tone siren replaces the output from then on.
//
// No floating point; integer division only outside the per-sample loops.

#include <cstdint>

#include "op1/op1.h"

namespace {

const uint32_t kNewBlockGap = 200000;     // cycles between calls = new block
const int32_t kCalibrationBlocks = 700;   // ~2 s
const int32_t kRampStepBlocks = 64;       // ~186 ms per 0.5% step
const uint32_t kFillPattern = 0xa5a5a5a5u;
const uint32_t kRegionEnd = 0x03000000;

enum Phase { CALIBRATE, RAMP, REPORT };

Phase phase;
bool initialized;
uint32_t last_call_end;
uint32_t last_block_start;
uint32_t period;            // shortest block period seen during calibration
uint32_t burn;              // current burn per block, cycles
uint32_t best_burn;         // largest burn that kept up
int32_t blocks;
int32_t voices_this_block;
int32_t min_voices;
uint32_t report_hz[3];
uint32_t report_phase;      // tone oscillator phase
int32_t report_sample;      // position in the report cycle, samples
bool corrupted;
uint32_t check_address;

extern "C" char __image_end[];

uint32_t FillStart() {
  return (reinterpret_cast<uint32_t>(__image_end) + 4095) & ~4095u;
}

void CheckFill() {
  const uint32_t start = FillStart();
  if (check_address < start || check_address + 256 > kRegionEnd) {
    check_address = start;
  }
  const volatile uint32_t* p = reinterpret_cast<const volatile uint32_t*>(check_address);
  for (int32_t i = 0; i < 64; ++i) {
    if (p[i] != kFillPattern) corrupted = true;
  }
  check_address += 256;
}

// Cycles per block -> MHz, for 128-sample blocks at 44.1 kHz.
uint32_t CyclesPerBlockToMHz(uint32_t cycles) {
  return (cycles / 1000) * 44100 / 128000;
}

void OnBlockStart(uint32_t now) {
  uint32_t gap = now - last_block_start;
  last_block_start = now;
  if (blocks > 0 && voices_this_block < min_voices) min_voices = voices_this_block;
  voices_this_block = 0;
  ++blocks;
  switch (phase) {
    case CALIBRATE:
      if (blocks > 4 && gap < period) period = gap;
      if (blocks >= kCalibrationBlocks) {
        phase = RAMP;
        blocks = 0;
      }
      break;
    case RAMP:
      if (blocks > 2 && gap > period + period * 3 / 10) {
        // Late block: overload. Stop burning; report the previous level.
        phase = REPORT;
        burn = 0;
        report_hz[0] = CyclesPerBlockToMHz(period);
        report_hz[1] = CyclesPerBlockToMHz(best_burn);
        report_hz[2] = 100 * static_cast<uint32_t>(min_voices) + 50;
        report_sample = 0;
        break;
      }
      best_burn = burn;
      if (blocks % kRampStepBlocks == 0) {
        burn += period / 200;
        if (burn >= period) {
          // Never overloaded: report everything as free.
          best_burn = period;
          phase = REPORT;
          burn = 0;
          report_hz[0] = CyclesPerBlockToMHz(period);
          report_hz[1] = CyclesPerBlockToMHz(best_burn);
          report_hz[2] = 100 * static_cast<uint32_t>(min_voices) + 50;
          report_sample = 0;
        }
      }
      break;
    case REPORT:
      break;
  }
}

// Triangle wave at `hz` into out (a quarter of full scale).
void Tone(int16_t* out, uint32_t hz, int32_t size) {
  const uint32_t increment = hz * 97392u;   // hz / 44100 * 2^32
  for (int32_t i = 0; i < size; ++i) {
    report_phase += increment;
    int32_t t = static_cast<int32_t>(report_phase >> 16) - 32768;   // saw
    int32_t tri = (t < 0 ? -t : t) - 16384;                         // triangle
    out[i] = static_cast<int16_t>(tri >> 1);
  }
}

void ReportOutput(int16_t* out) {
  const int32_t kTone = 66150, kGap = 22050, kPause = 88200;
  const int32_t kCycle = 3 * kTone + 2 * kGap + kPause;
  if (corrupted) {
    Tone(out, (report_sample / 11025) & 1 ? 1500 : 1000, op1::kBlockSize);
  } else {
    int32_t t = report_sample % kCycle;
    int32_t slot = -1;
    for (int32_t k = 0; k < 3; ++k) {
      int32_t start = k * (kTone + kGap);
      if (t >= start && t < start + kTone) slot = k;
    }
    if (slot >= 0) {
      Tone(out, report_hz[slot], op1::kBlockSize);
    } else {
      for (int32_t i = 0; i < op1::kBlockSize; ++i) out[i] = 0;
    }
  }
}

}  // namespace

extern "C" void ProbeRender(void* self, int16_t* out, const int16_t* params) {
  uint32_t now = op1::Cycles();
  if (!initialized) {
    initialized = true;
    phase = CALIBRATE;
    period = 0xffffffffu;
    burn = best_burn = 0;
    blocks = 0;
    voices_this_block = 0;
    min_voices = op1::kNumVoices;
    last_block_start = now;
    last_call_end = now;
    corrupted = false;
    check_address = 0;
  }
  if (now - last_call_end > kNewBlockGap) {
    OnBlockStart(now);
    if (phase == REPORT || corrupted) report_sample += op1::kBlockSize;
    // Burn once per block.
    if (burn) {
      while (op1::Cycles() - now < burn) { }
    }
  }
  ++voices_this_block;
  CheckFill();

  if (phase == REPORT || corrupted) {
    ReportOutput(out);
  } else {
    reinterpret_cast<op1::RenderFn>(op1::kPhaseRender)(self, out, params);
  }
  last_call_end = op1::Cycles();
}

extern "C" __attribute__((section(".op1header"), used))
const op1::Header op1_header = {
  op1::kHeaderMagic, 1, &ProbeRender, nullptr, nullptr, { 0, 0, 0 },
};
