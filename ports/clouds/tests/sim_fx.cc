// Simulator check of op1/clouds_fx.cc's entry points, called as the OP-1
// calls an effect: 64 samples at 44.1 kHz, the stereo entry for the master,
// the mono one for the synth. Built with -DOP1_SIM (make sim-fx): no
// firmware, both play Clouds on the values passed (op1/clouds_settings.h).
//
// Prints per scenario the restart call's cycles and the cycles per call
// (MHz at the OP-1's call rate), the governor's grain cap and an output
// checksum; then the settings' click actions (page swap, mode, engine) on
// the synth FX page values, which must round-trip.

#include <cstdint>
#include <cstdio>

#include "harness/cycles.h"
#include "op1/clouds_fx.h"
#include "src/core/profile.h"
#include "op1/clouds_settings.h"

namespace op1 { uint32_t sim_pitch_buffer[128]; }
namespace op1fx { extern int16_t sim_synth_values[8]; }

namespace {

int failures;

void Check(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL: %s\n", what);
    ++failures;
  }
}

struct Scenario {
  const char* name;
  bool stereo;
  int16_t values[8];
};

// Encoders within phone's ranges (blue 204..20480, green 3072..17408,
// white 1536..16896, orange 0..32767); values[4]: mode index << 1.
const Scenario kScenarios[] = {
  // looping delay, page 1: position .3, size .5, pitch centre, density 0
  { "master_ld",        true,  { 6300, 10240, 9216, 0, 0, 0, 0, 0 } },
  // granular, density .8 (random grains), hidden page at its defaults
  { "master_granular",  true,  { 6300, 10240, 9216, 26214, 1 << 1, 0, 0, 0 } },
  // granular, density .95 (dense)
  { "master_gr_dense",  true,  { 6300, 12000, 9216, 31129, 1 << 1, 0, 0, 0 } },
  // granular, density .8, the hidden page: reverb .5 only
  { "master_gr_reverb", true,  { 6300, 10240, 9216, 26214, 1 << 1, 0, 0, 16384 } },
  // granular, density .8, texture .9 (diffusion on), feedback .3, reverb .5
  // (values[4]: mode 1, texture offset (29490 - 16384) >> 4 = 819 << 4)
  { "master_gr_full",   true,  { 6300, 10240, 9216, 26214, (1 << 1) | (819 << 4), 0, 9830, 16384 } },
  { "synth_ld",         false, { 6300, 10240, 9216, 0, 0, 0, 0, 0 } },
  { "synth_granular",   false, { 6300, 10240, 9216, 26214, 1 << 1, 0, 0, 0 } },
};

uint32_t Run(const Scenario& sc, int calls, harness::CycleStats* stats, uint64_t* restart) {
  int16_t l[op1fx::kFxBlockSize], r[op1fx::kFxBlockSize];
  uint32_t x = 12345, checksum = 0;
  uint32_t phase = 0;
  for (int c = 0; c < calls; ++c) {
    for (int i = 0; i < op1fx::kFxBlockSize; ++i) {
      x = x * 1664525u + 1013904223u;
      phase += 1 << 25;   // a saw at ~430 Hz plus some noise
      l[i] = static_cast<int16_t>((static_cast<int32_t>(phase) >> 18) + (static_cast<int32_t>(x) >> 21));
      r[i] = static_cast<int16_t>((static_cast<int32_t>(phase) >> 19) - (static_cast<int32_t>(x) >> 21));
    }
    const uint64_t t0 = harness::ReadCycles();
    if (sc.stereo) {
      CloudsFxStereo(nullptr, l, r, sc.values);
    } else {
      CloudsFxMono(nullptr, l, sc.values);
    }
    const uint64_t t = harness::ReadCycles() - t0;
    if (c == 0) {
      *restart = t;
    } else {
      stats->Add(t);
    }
    for (int i = 0; i < op1fx::kFxBlockSize; ++i) {
      checksum = checksum * 31 + static_cast<uint16_t>(l[i]);
      if (sc.stereo) checksum = checksum * 31 + static_cast<uint16_t>(r[i]);
    }
  }
  return checksum;
}

void TestSettings() {
  using namespace op1clouds;
  int16_t* v = op1fx::sim_synth_values;
  const int16_t page1[4] = { 6300, 10240, 9216, 26214 };
  for (int k = 0; k < 4; ++k) v[k] = page1[k];
  for (int k = 4; k < 8; ++k) v[k] = 0;

  FxSettings s = DecodeSettings(v);
  Check(s.engine == FX_CLOUDS && s.mode_index == 0 && s.page == 0, "zero settings: Clouds, LD, page 1");
  Check(s.hidden[0] == (kDefaults[4] | 8) && s.hidden[1] == kDefaults[5] && s.hidden[2] == 0 &&
            s.hidden[3] == 0,
        "zero settings: page 2 at its defaults");

  // White: page 2 on the encoders, page 1 hidden; and back.
  Check(op1fx::SynthFxClick(op1fx::CLICK_WHITE, 1), "white consumed");
  s = DecodeSettings(v);
  Check(s.page == 1, "page 2 after white");
  int32_t values[kNumParams];
  EncoderRange ranges[4];
  const int16_t kR[4][2] = { { 204, 20480 }, { 3072, 17408 }, { 1536, 16896 }, { 0, 32767 } };
  for (int k = 0; k < 4; ++k) InitRange(&ranges[k], kR[k][0], kR[k][1]);
  ParamValues(ranges, v, s, values);
  printf("page 2 shown: encoders %d %d %d %d, values %d %d %d %d | %d %d %d %d\n", v[0], v[1], v[2],
         v[3], values[0], values[1], values[2], values[3], values[4], values[5], values[6],
         values[7]);
  Check(values[5] > 16300 && values[5] < 16500, "dry/wet default ~50% on the encoder");
  Check(op1fx::SynthFxClick(op1fx::CLICK_WHITE, 1), "white consumed");
  s = DecodeSettings(v);
  Check(s.page == 0, "page 1 after two whites");
  for (int k = 0; k < 4; ++k) {
    const int d = v[k] - page1[k];
    // The first hidden value keeps 11 bits: 16 steps of v15, < 11 encoder
    // steps on blue.
    Check(d >= -12 && d <= 12, "page 1 encoders round-trip");
  }
  printf("page 1 back:  encoders %d %d %d %d (were %d %d %d %d)\n", v[0], v[1], v[2], v[3],
         page1[0], page1[1], page1[2], page1[3]);

  // Blue: looping delay -> granular -> looping delay; shift goes back.
  op1fx::SynthFxClick(op1fx::CLICK_BLUE, 1);
  Check(DecodeSettings(v).mode_index == 1, "blue: granular");
  op1fx::SynthFxClick(op1fx::CLICK_BLUE, 1);
  Check(DecodeSettings(v).mode_index == 0, "blue twice: looping delay");
  op1fx::SynthFxClick(op1fx::CLICK_BLUE, -1);
  Check(DecodeSettings(v).mode_index == 1, "shift + blue: granular");

  // Orange: phone; blue is then not ours; orange: Clouds again.
  Check(op1fx::SynthFxClick(op1fx::CLICK_ORANGE, 1), "orange consumed");
  Check(DecodeSettings(v).engine == FX_PHONE, "orange: phone");
  Check(!op1fx::SynthFxClick(op1fx::CLICK_BLUE, 1), "blue on phone goes to stock");
  int16_t buf[op1fx::kFxBlockSize];
  for (int i = 0; i < op1fx::kFxBlockSize; ++i) buf[i] = static_cast<int16_t>(i * 100);
  CloudsFxMono(nullptr, buf, v);
  bool untouched = true;
  for (int i = 0; i < op1fx::kFxBlockSize; ++i) untouched &= buf[i] == i * 100;
  Check(untouched, "phone: Clouds leaves the buffer alone");
  op1fx::SynthFxClick(op1fx::CLICK_ORANGE, -1);
  s = DecodeSettings(v);
  Check(s.engine == FX_CLOUDS && s.mode_index == 1 && s.page == 0, "orange back: Clouds, granular, page 1");
  for (int k = 4; k < 8; ++k) Check(v[k] >= 0, "values[4..7] within 0..32767");

  // Pitch: the dead zone and the ends.
  Check(PitchQ16(16384) == 0 && PitchQ16(16384 + kPitchDeadZone) == 0, "pitch centre");
  const int32_t top = PitchQ16(32767), bottom = PitchQ16(0);
  printf("pitch ends: %.3f %.3f semitones\n", top / 65536.0, bottom / 65536.0);
  Check(top > (24 << 16) - 512 && top <= (24 << 16) + 512, "pitch +24");
  Check(bottom < -(24 << 16) + 512 && bottom >= -(24 << 16) - 512, "pitch -24");
}

}  // namespace

#ifdef OP1CLOUDS_PROFILE
// Stage cycles per call (src/core/profile.h), printed per scenario, as the
// device's CPU probe shows them (ports/rings/op1/cpu_probe.cc, page 1).
uint32_t ProfileClock() { return static_cast<uint32_t>(harness::ReadCycles()); }
const char* const kStageNames[] = { "record", "player", "overlap", "diffuse", "reverb", "resample+mix" };
#endif

int main() {
  harness::EnableCycleCounter();
  const int kCalls = 1400;   // ~2 s
#ifdef OP1CLOUDS_PROFILE
  op1clouds::profile_clock = ProfileClock;
#endif
  for (const Scenario& sc : kScenarios) {
    harness::CycleStats stats;
    uint64_t restart = 0;
#ifdef OP1CLOUDS_PROFILE
    uint32_t before[op1clouds::PROFILE_NUM_STAGES];
    for (int i = 0; i < op1clouds::PROFILE_NUM_STAGES; ++i) before[i] = op1clouds::profile_cycles[i];
#endif
    const uint32_t checksum = Run(sc, kCalls, &stats, &restart);
    printf("%-16s restart %7lu cyc  ", sc.name, static_cast<unsigned long>(restart));
    stats.Print("", op1fx::kFxBlockSize);
    printf("%-16s checksum %08lx\n", sc.name, static_cast<unsigned long>(checksum));
#ifdef OP1CLOUDS_PROFILE
    for (int i = 0; i < op1clouds::PROFILE_NUM_STAGES; ++i) {
      const uint32_t per_call = (op1clouds::profile_cycles[i] - before[i]) / kCalls;
      // 689 calls a second: MHz = cycles per call x 689 / 1e6.
      printf("    %-13s %6lu cyc/call  %5.1f MHz\n", kStageNames[i],
             static_cast<unsigned long>(per_call), per_call * 689.0 / 1e6);
    }
    printf("    grains %ld\n", static_cast<long>(op1clouds::profile_grains[sc.stereo ? 1 : 0]));
#endif
  }
  TestSettings();
  printf(failures ? "sim-fx: %d FAILED\n" : "sim-fx: all checks passed\n", failures);
  return failures ? 1 : 0;
}
