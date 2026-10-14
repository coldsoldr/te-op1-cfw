// Probe build v3: measures the OP-1's block period, free CPU and the held
// key's phase increment from inside the phase engine, and reports them as
// beep counts. Replaces the phase engine's render (vtable +0x14).
//
// v1 reported values as tone pitches and came out scrambled. v2 reported beep
// counts, but judged overload against the *shortest* call interval; intervals
// jitter by ~8%, so it saw overload at once and reported 0 free CPU. v3 judges
// overload on the mean interval over 64 calls, which the jitter barely moves
// (the intervals telescope). Free CPU here is what the audio path leaves; the
// UI may run in that time too.
//
// While a key is held:
//   1. Calibrate (~2 s, plays the stock phase sound). The first voice object
//      seen is the reference voice. Records its mean call interval (the
//      block period, in cycles), its longest interval, and the phase
//      increment at the start of its last block.
//   2. Ramp (stock sound, stutters expected near the end): in each
//      reference-voice call, busy-waits B cycles, raising B by 0.5% of the
//      period every 64 of its calls. A 64-call window whose mean interval is
//      over 2% longer than the period is overload; the probe then stops
//      burning, keeping the last B that ran without overload, less 2%.
//   3. Report, repeating, on every call (hold a key to hear it):
//        sync: one long tone (12 units), 12 units silence
//        then five numbers, each as decimal digits:
//          digit d -> d beeps (0 -> 10 beeps), each 2 units on, 2 off;
//          6 more units silence between digits, 20 between numbers.
//        numbers: 3 (the probe version), block period / 1024,
//                 free cycles per block / 1024,
//                 the held key's pitch in 0.1 Hz assuming 88.2 kHz,
//                 longest call interval / 1024
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

const int32_t kCalibrationCalls = 1400;   // reference-voice calls (~2 s)
const int32_t kRampStepCalls = 64;
const uint32_t kFillPattern = 0xa5a5a5a5u;
const uint32_t kRegionEnd = 0x03000000;
const int32_t kNumbers = 5;
const uint32_t kVersion = 3;

enum Phase { CALIBRATE, RAMP, REPORT };

Phase phase;
bool initialized;
int32_t reference_voice;
uint32_t last_reference_call;
uint32_t first_timed_call;     // start of the calibration's timed calls
uint32_t period;               // mean reference-voice call interval
uint32_t max_interval;
uint32_t pitch_increment;
uint32_t window_start;         // ramp: start of the current 64-call window
uint32_t burn;
uint32_t best_burn;
int32_t reference_calls;
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
    first_timed_call = now;
    period = 0;
    max_interval = 0;
    pitch_increment = 0;
    burn = best_burn = 0;
    reference_calls = 0;
    corrupted = false;
    check_address = 0;
    last_now = now;
  }
  const uint32_t delta = now - last_now;
  last_now = now;

  if (voice == reference_voice) {
    const uint32_t interval = now - last_reference_call;
    last_reference_call = now;
    ++reference_calls;
    switch (phase) {
      case CALIBRATE:
        // Skip the first calls (note start), then time the rest.
        if (reference_calls == 8) first_timed_call = now;
        if (reference_calls > 8 && interval > max_interval) max_interval = interval;
        pitch_increment = op1::PitchBuffer()[0];
        if (reference_calls == kCalibrationCalls) {
          period = (now - first_timed_call) / static_cast<uint32_t>(kCalibrationCalls - 8);
          phase = RAMP;
          reference_calls = 0;
          window_start = now;
        }
        break;
      case RAMP:
        if (reference_calls % kRampStepCalls == 0) {
          const uint32_t mean = (now - window_start) / kRampStepCalls;
          window_start = now;
          if (mean > period + period / 50) {
            phase = REPORT;
          } else {
            best_burn = burn;
            burn += period / 200;
            if (burn >= period) {
              best_burn = period;
              phase = REPORT;
            }
          }
        }
        if (phase == REPORT) {
          burn = 0;
          numbers[0] = kVersion;
          numbers[1] = period >> 10;
          // Overload under the 2% threshold goes unseen: report the burn
          // minus that margin.
          numbers[2] = (best_burn > period / 50 ? best_burn - period / 50 : 0) >> 10;
          // f = inc / 2^32 * 88200, in 0.1 Hz, rounded.
          numbers[3] = static_cast<uint32_t>(
              (static_cast<uint64_t>(pitch_increment) * (op1::kVoiceSampleRate * 10) + (1ull << 31)) >> 32);
          numbers[4] = max_interval >> 10;
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
  op1::kHeaderMagic, kVersion, &ProbeRender, nullptr, nullptr, { 0, 0, 0 },
};
