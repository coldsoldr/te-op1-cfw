// FX probe v4: measures the OP-1's effect interface from inside the phone
// effect, on top of the MULTI engine, and reports the numbers as beep counts
// (tools/decode_probe_beeps.py). Both process entries (mono: the synth and
// drum effect; stereo: the master effect) call stock phone as before, so the
// effect sounds unchanged until a measurement starts. Each entry has its own
// state machine:
//
//   1. Idle (stock phone): rolling windows of 1024 calls (~1.5 s). Each
//      complete window latches the mean call interval (the block period),
//      the longest interval, stock phone's mean cycles per call and the synth
//      voice render calls per effect call. An interval over 5 ms (a gap: the
//      effect wasn't called) is counted and restarts the window.
//   2. Ramp: starts when the blue encoder's value (params[0]) changes after a
//      window has latched. As probe v3: each call busy-waits B cycles after
//      stock phone, B rising by 0.5% of the period every 64 calls; a 64-call
//      window whose mean interval is over 2% longer than the period is
//      overload, and the last B that ran clean, less 2%, is the free time.
//   3. Report, repeating: the effect's output becomes the beep pattern (a
//      square wave with a 32-sample period; for stereo on the left channel
//      only, the right is silent). Same timing as probe v3: one unit is 2^23
//      cycles; sync 12 units on, 12 off; digit d -> d beeps (0 -> 10) of 2
//      on, 2 off; 6 more off after each digit, 20 after each number.
//      Numbers: 4 (the version), kind (1 mono, 2 stereo), position from the
//      params address (1 synth, 2 drum, 3 master, 9 other), period / 1024,
//      free cycles per call / 1024, stock phone's cycles per call / 256,
//      synth render calls per effect call x 10, longest interval / 1024,
//      gaps since idle started.
//
// A change of the white encoder (params[2]) in any state returns to idle and
// clears the gap count, so a run can be repeated.
//
// The tone's pitch gives the effect's sample rate: 1378 Hz at 44.1 kHz.

#include "op1/fx_probe.h"

#include "op1/op1.h"

namespace op1 {

uint32_t render_calls;

}  // namespace op1

namespace {

const uint32_t kVersion = 4;
const int32_t kWindowShift = 10;              // 1024 calls per idle window
const int32_t kRampStepShift = 6;             // 64 calls per ramp step
const uint32_t kGapCycles = 2000000;          // 5 ms at 400 MHz
const int32_t kNumbers = 9;

enum State { IDLE, RAMP, REPORT };

struct Probe {
  bool started;
  State state;
  int16_t last_blue;
  int16_t last_white;
  uint32_t last_call;
  // Idle window.
  uint32_t window_start;
  int32_t window_calls;
  uint32_t window_max_interval;
  uint32_t window_phone_cycles;
  uint32_t window_renders_start;
  uint32_t gaps;
  // Latched.
  bool latched;
  uint32_t period;
  uint32_t max_interval;
  uint32_t phone_cycles;
  uint32_t renders_x10;
  // Ramp.
  int32_t ramp_calls;
  uint32_t ramp_window_start;
  uint32_t burn;
  uint32_t best_burn;
  // Report.
  uint64_t report_cycles;
  uint32_t numbers[kNumbers];
};

Probe mono_probe;
Probe stereo_probe;

uint32_t Position(const int16_t* params) {
  const uint32_t a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(params));
  if (a - op1::kFxParamsSynth < 8 * 16) return 1;
  if (a - op1::kFxParamsDrum < 8 * 16) return 2;
  if (a == op1::kFxParamsMaster) return 3;
  return 9;
}

void StartWindow(Probe* p, uint32_t now) {
  p->window_start = now;
  p->window_calls = 0;
  p->window_max_interval = 0;
  p->window_phone_cycles = 0;
  p->window_renders_start = op1::render_calls;
}

void Idle(Probe* p, uint32_t now) {
  p->state = IDLE;
  p->latched = false;
  p->gaps = 0;
  p->burn = 0;
  StartWindow(p, now);
}

// Whether the report pattern is "on" at `units` (2^23 cycles) into a report
// cycle; also returns the cycle length. As op1/probe.cc.
bool ReportOn(const Probe& p, uint32_t units, uint32_t* cycle_length) {
  uint32_t t = 24;
  bool on = units < 12;
  for (int32_t k = 0; k < kNumbers; ++k) {
    uint32_t value = p.numbers[k];
    uint32_t digits[10];
    int32_t n = 0;
    do { digits[n++] = value % 10; value /= 10; } while (value && n < 10);
    for (int32_t d = n - 1; d >= 0; --d) {
      const uint32_t beeps = digits[d] ? digits[d] : 10;
      for (uint32_t b = 0; b < beeps; ++b) {
        if (units >= t && units < t + 2) on = true;
        t += 4;
      }
      t += 6;
    }
    t += 20;
  }
  *cycle_length = t;
  return on;
}

void Square(int16_t* out, bool on) {
  for (int32_t i = 0; i < op1::kFxBlockSize; ++i) {
    out[i] = on ? ((i & 16) ? 8192 : -8192) : 0;
  }
}

// Runs after stock phone. `phone` is stock phone's cycles for this call.
// Returns whether the output should be the report.
bool Step(Probe* p, uint32_t kind, const int16_t* params, uint32_t now, uint32_t phone,
          bool* tone) {
  if (!p->started) {
    p->started = true;
    p->last_blue = params[0];
    p->last_white = params[2];
    p->last_call = now;
    Idle(p, now);
    return false;
  }
  const uint32_t interval = now - p->last_call;
  p->last_call = now;
  const int16_t blue = params[0];
  const int16_t white = params[2];
  const bool blue_moved = blue != p->last_blue;
  const bool white_moved = white != p->last_white;
  p->last_blue = blue;
  p->last_white = white;

  if (white_moved) {
    Idle(p, now);
    return false;
  }

  switch (p->state) {
    case IDLE:
      if (interval > kGapCycles) {
        ++p->gaps;
        StartWindow(p, now);
        break;
      }
      if (interval > p->window_max_interval) p->window_max_interval = interval;
      p->window_phone_cycles += phone;
      if (++p->window_calls == (1 << kWindowShift)) {
        p->period = (now - p->window_start) >> kWindowShift;
        p->max_interval = p->window_max_interval;
        p->phone_cycles = p->window_phone_cycles >> kWindowShift;
        p->renders_x10 = ((op1::render_calls - p->window_renders_start) * 10) >> kWindowShift;
        p->latched = true;
        StartWindow(p, now);
      }
      if (blue_moved && p->latched) {
        p->state = RAMP;
        p->ramp_calls = 0;
        p->ramp_window_start = now;
        p->burn = p->best_burn = 0;
      }
      break;
    case RAMP:
      if (++p->ramp_calls == (1 << kRampStepShift)) {
        p->ramp_calls = 0;
        const uint32_t mean = (now - p->ramp_window_start) >> kRampStepShift;
        p->ramp_window_start = now;
        bool done = mean > p->period + p->period / 50;
        if (!done) {
          p->best_burn = p->burn;
          p->burn += p->period / 200;
          if (p->burn >= p->period) {
            p->best_burn = p->period;
            done = true;
          }
        }
        if (done) {
          p->state = REPORT;
          p->burn = 0;
          p->numbers[0] = kVersion;
          p->numbers[1] = kind;
          p->numbers[2] = Position(params);
          p->numbers[3] = p->period >> 10;
          // Overload under the 2% threshold goes unseen: report the burn
          // minus that margin.
          p->numbers[4] = (p->best_burn > p->period / 50 ? p->best_burn - p->period / 50 : 0) >> 10;
          p->numbers[5] = p->phone_cycles >> 8;
          p->numbers[6] = p->renders_x10;
          p->numbers[7] = p->max_interval >> 10;
          p->numbers[8] = p->gaps;
          p->report_cycles = 0;
        }
      }
      break;
    case REPORT: {
      p->report_cycles += interval;
      uint32_t cycle_length;
      ReportOn(*p, 0, &cycle_length);
      *tone = ReportOn(*p, static_cast<uint32_t>(p->report_cycles >> 23) % cycle_length, &cycle_length);
      return true;
    }
  }
  if (p->burn) op1::BusyWait(op1::Cycles(), p->burn);
  return false;
}

}  // namespace

#ifdef OP1_HOST_SIM
// Host simulation (tests/host_fx_probe.cc) stands in for stock phone.
void HostPhone(int16_t* buf);
#endif

extern "C" void FxProbeMono(void* self, int16_t* buf, const int16_t* params) {
  const uint32_t start = op1::Cycles();
#ifdef OP1_HOST_SIM
  HostPhone(buf);
#else
  reinterpret_cast<op1::FxMonoFn>(op1::kPhoneMonoProcess)(self, buf, params);
#endif
  const uint32_t now = op1::Cycles();
  bool tone = false;
  if (Step(&mono_probe, 1, params, start, now - start, &tone)) Square(buf, tone);
}

extern "C" void FxProbeStereo(void* self, int16_t* l, int16_t* r, const int16_t* params) {
  const uint32_t start = op1::Cycles();
#ifdef OP1_HOST_SIM
  HostPhone(l);
  HostPhone(r);
#else
  reinterpret_cast<op1::FxStereoFn>(op1::kPhoneStereoProcess)(self, l, r, params);
#endif
  const uint32_t now = op1::Cycles();
  bool tone = false;
  if (Step(&stereo_probe, 2, params, start, now - start, &tone)) {
    Square(l, tone);
    Square(r, false);
  }
}
