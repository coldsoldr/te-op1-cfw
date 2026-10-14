// Probe build v2: measures the OP-1's render call pattern, clock and free
// CPU from inside the phase engine and reports them as beep counts. Replaces
// the phase engine's render (vtable +0x14).
//
// v1 reported values as tone pitches from one oscillator shared by all
// render calls; with several calls per block the pitches came out scrambled
// (and its block detection fired twice per block). v2 avoids pitch entirely.
//
// While a key is held:
//   1. Calibrate (~2 s, plays the stock phase sound). The first voice object
//      seen is the reference voice. Records its call interval (the audio
//      block period, in cycles), the number of render calls between two of
//      its calls (min and max), the number of distinct voice objects called,
//      and the total number of calls in a window of 2^28 cycles.
//   2. Ramp: in each reference-voice call, busy-waits B cycles, raising B by
//      0.5% of the period every 64 of its calls, until one of its calls
//      arrives > 5% late (overload). Then stops burning.
//   3. Report, repeating, on every call (hold a key to hear it):
//        sync: one long tone (12 units), 12 units silence
//        then six numbers, each as decimal digits:
//          digit d -> d beeps (0 -> 10 beeps), each 2 units on, 2 off;
//          6 more units silence between digits, 20 between numbers.
//        numbers: block period / 1024, burn / 1024 (free CPU per block),
//                 min calls per block, max calls per block,
//                 distinct voices, calls in 2^28 cycles
//      One unit is 2^23 CPU cycles, so the beep timing in a recording gives
//      the clock: 4 units (one beep period) = 2^25 cycles.
// Throughout: the rest of the injected region holds a fill pattern (written by
// the LDR). Each call checks 64 words of it; if any word changed, the output
// becomes a continuous tone from then on.
//
// Tones are a square wave with a 32-sample period, identical in every
// 128-sample buffer, so they're continuous whatever the call pattern.

#include <cstdint>

#include "op1/op1.h"

namespace {

const int32_t kCalibrationCalls = 700;    // reference-voice calls (~2 s)
const int32_t kRampStepCalls = 64;
const uint32_t kWindow = 1u << 28;        // cycles, for counting calls
const uint32_t kFillPattern = 0xa5a5a5a5u;
const uint32_t kRegionEnd = 0x03000000;
const int32_t kNumbers = 6;

enum Phase { CALIBRATE, RAMP, REPORT };

Phase phase;
bool initialized;
int32_t reference_voice;
uint32_t last_reference_call;
uint32_t period;               // shortest reference-voice call interval
uint32_t burn;
uint32_t best_burn;
int32_t reference_calls;
int32_t calls_since_reference;
int32_t min_calls_per_block;
int32_t max_calls_per_block;
uint32_t voices_seen;          // bit mask of voice indices
uint32_t window_start;
int32_t window_calls;
int32_t window_result;
uint64_t report_cycles;        // cycles since the report started
uint32_t last_now;
uint32_t numbers[kNumbers];
bool corrupted;
uint32_t check_address;

extern "C" char __image_end[];

void CheckFill() {
#ifdef OP1_HOST_SIM
  return;   // the region doesn't exist on the host
#endif
  const uint32_t start = (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(__image_end)) + 4095) & ~4095u;
  if (check_address < start || check_address + 256 > kRegionEnd) {
    check_address = start;
  }
  const volatile uint32_t* p = reinterpret_cast<const volatile uint32_t*>(check_address);
  for (int32_t i = 0; i < 64; ++i) {
    if (p[i] != kFillPattern) corrupted = true;
  }
  check_address += 256;
}

void Square(int16_t* out, bool on) {
  for (int32_t i = 0; i < op1::kBlockSize; ++i) {
    out[i] = on ? ((i & 16) ? 8192 : -8192) : 0;
  }
}

// Voice slot from the object address: 0-5 for the phase engine's voices;
// anything else (an object outside the voice array) counts as 31.
int32_t RawIndex(const void* self) {
  uint32_t offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self)) - op1::kVoiceObjectBase;
  uint32_t index = offset / op1::kPhaseVoiceObjectSize;
  return index < 31 ? static_cast<int32_t>(index) : 31;
}

int32_t CountBits(uint32_t x) {
  int32_t n = 0;
  while (x) { n += x & 1; x >>= 1; }
  return n;
}

// Whether the report pattern is "on" at `units` (in units of 2^23 cycles)
// after the start of a report cycle; also returns the cycle length.
bool ReportOn(uint32_t units, uint32_t* cycle_length) {
  // Build the pattern on the fly: sync, then numbers.
  uint32_t t = 0;
  bool on = false;
  bool found = false;
  // sync
  if (units < 12) { on = true; found = true; }
  t = 24;
  for (int32_t k = 0; k < kNumbers; ++k) {
    // digits, most significant first
    uint32_t value = numbers[k];
    uint32_t digits[10];
    int32_t n = 0;
    do { digits[n++] = value % 10; value /= 10; } while (value && n < 10);
    for (int32_t d = n - 1; d >= 0; --d) {
      uint32_t beeps = digits[d] ? digits[d] : 10;
      for (uint32_t b = 0; b < beeps; ++b) {
        if (!found && units >= t && units < t + 2) { on = true; found = true; }
        t += 4;
      }
      t += 6;
    }
    t += 20;
  }
  *cycle_length = t;
  return on;
}

}  // namespace

extern "C" void ProbeRender(void* self, int16_t* out, const int16_t* params) {
  const uint32_t now = op1::Cycles();
  const int32_t voice = RawIndex(self);
  if (!initialized) {
    initialized = true;
    phase = CALIBRATE;
    reference_voice = voice;
    last_reference_call = now;
    period = 0xffffffffu;
    burn = best_burn = 0;
    reference_calls = 0;
    calls_since_reference = 0;
    min_calls_per_block = 1 << 30;
    max_calls_per_block = 0;
    voices_seen = 0;
    window_start = now;
    window_calls = 0;
    window_result = -1;
    corrupted = false;
    check_address = 0;
    last_now = now;
  }
  const uint32_t delta = now - last_now;
  last_now = now;

  if (phase == CALIBRATE) {
    voices_seen |= 1u << voice;
    if (window_result < 0) {
      if (now - window_start < kWindow) {
        ++window_calls;
      } else {
        window_result = window_calls;
      }
    }
  }
  ++calls_since_reference;

  if (voice == reference_voice) {
    const uint32_t interval = now - last_reference_call;
    last_reference_call = now;
    ++reference_calls;
    if (reference_calls > 4 && phase != REPORT) {
      if (calls_since_reference < min_calls_per_block) min_calls_per_block = calls_since_reference;
      if (calls_since_reference > max_calls_per_block) max_calls_per_block = calls_since_reference;
    }
    calls_since_reference = 0;
    switch (phase) {
      case CALIBRATE:
        if (reference_calls > 4 && interval < period) period = interval;
        if (reference_calls >= kCalibrationCalls && window_result >= 0) {
          phase = RAMP;
          reference_calls = 0;
        }
        break;
      case RAMP:
        if (reference_calls > 2 && interval > period + period / 20) {
          phase = REPORT;
          burn = 0;
        } else {
          best_burn = burn;
          if (reference_calls % kRampStepCalls == 0) {
            burn += period / 200;
            if (burn >= period) {
              best_burn = period;
              phase = REPORT;
              burn = 0;
            }
          }
        }
        if (phase == REPORT) {
          numbers[0] = period >> 10;
          // Overload is detected 5% past the limit: report the burn minus
          // that margin.
          numbers[1] = (best_burn > period / 20 ? best_burn - period / 20 : 0) >> 10;
          numbers[2] = static_cast<uint32_t>(min_calls_per_block);
          numbers[3] = static_cast<uint32_t>(max_calls_per_block);
          numbers[4] = static_cast<uint32_t>(CountBits(voices_seen));
          numbers[5] = static_cast<uint32_t>(window_result);
          report_cycles = 0;
        }
        break;
      case REPORT:
        break;
    }
    if (burn) op1::BusyWait(now, burn);
  }

  CheckFill();
  if (corrupted) {
    Square(out, true);
  } else if (phase == REPORT) {
    uint32_t cycle_length;
    report_cycles += delta;
    uint32_t units = static_cast<uint32_t>(report_cycles >> 23);
    ReportOn(0, &cycle_length);
    Square(out, ReportOn(units % cycle_length, &cycle_length));
  } else {
#ifdef OP1_HOST_SIM
    for (int32_t i = 0; i < op1::kBlockSize; ++i) out[i] = 0;   // stock sound
#else
    reinterpret_cast<op1::RenderFn>(op1::kPhaseRender)(self, out, params);
#endif
  }
}

extern "C" __attribute__((section(".op1header"), used))
const op1::Header op1_header = {
  op1::kHeaderMagic, 2, &ProbeRender, nullptr, nullptr, { 0, 0, 0 },
};
