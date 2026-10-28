// Simulator check of Clouds on the reverb host's entry points
// (../fx/op1/fx_host.cc, op1/clouds_fx.cc), called as the OP-1 calls an
// effect: 64 samples at 44.1 kHz, the stereo entry for the master, the mono
// one for the synth. Built with -DOP1_SIM (make sim-fx): no firmware, both
// play the passed values (op1/clouds_settings.h).
//
// Prints per scenario the restart call's cycles and the cycles per call
// (MHz at the OP-1's call rate), the governor's grain cap and an output
// checksum, which must match the phone-hosted build's (the scenarios run on
// phone's encoder ranges); then the marker and the settings' click actions
// (page swap, mode, engine) on the synth FX page values, which must
// round-trip.

#include <cstdint>
#include <cstdio>

#include "harness/cycles.h"
#include "fx/op1/fx_host.h"
#include "fx/op1/two_page.h"
#include "op1/clouds_fx.h"
#include "src/core/profile.h"
#include "op1/clouds_settings.h"
#include "tests/sim_engines.h"

namespace op1 { uint32_t sim_pitch_buffer[128]; }

namespace {

// Phone's encoder ranges (FX Ranges table): Clouds' checksums before the
// move to the reverb host were made on them.
const int16_t kPhoneRanges[4][2] = { { 204, 20480 }, { 3072, 17408 }, { 1536, 16896 }, { 0, 32767 } };

// The marker of Clouds on the reverb host.
const int32_t kClouds = op1fx::kMarker | op1clouds::kCloudsEngineId << op1fx::kEngineShift;

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
  int16_t encoders[4];
  int32_t mode;        // mode index: 0 looping delay, 1 granular
  int16_t texture;     // the hidden texture's offset >> 4 (values[4] bits 0-10)
  int16_t hidden[3];   // values[5..7] bits 0-14: dry/wet, feedback, reverb offsets
  uint32_t checksum;   // the phone-hosted build's (seq13/seq14)
};

// Encoders within phone's ranges (blue 204..20480, green 3072..17408,
// white 1536..16896, orange 0..32767).
const Scenario kScenarios[] = {
  // looping delay, page 1: position .3, size .5, pitch centre, density 0
  { "master_ld",        true,  { 6300, 10240, 9216, 0 },     0, 0,   { 0, 0, 0 },        0xb4658573 },
  // granular, density .8 (random grains), hidden page at its defaults
  { "master_granular",  true,  { 6300, 10240, 9216, 26214 }, 1, 0,   { 0, 0, 0 },        0xfd327fd4 },
  // granular, density .95 (dense)
  { "master_gr_dense",  true,  { 6300, 12000, 9216, 31129 }, 1, 0,   { 0, 0, 0 },        0x0cb734b8 },
  // granular, density .8, the hidden page: reverb .5 only
  { "master_gr_reverb", true,  { 6300, 10240, 9216, 26214 }, 1, 0,   { 0, 0, 16384 },    0xde6c92a7 },
  // granular, density .8, texture .9 (diffusion on), feedback .3, reverb .5
  // (texture offset (29490 - 16384) >> 4 = 819)
  { "master_gr_full",   true,  { 6300, 10240, 9216, 26214 }, 1, 819, { 0, 9830, 16384 }, 0x6c74c7cd },
  { "synth_ld",         false, { 6300, 10240, 9216, 0 },     0, 0,   { 0, 0, 0 },        0xaeffbb47 },
  { "synth_granular",   false, { 6300, 10240, 9216, 26214 }, 1, 0,   { 0, 0, 0 },        0x7122a804 },
};

void ScenarioValues(const Scenario& sc, int16_t* v) {
  using namespace op1clouds;
  for (int k = 0; k < 4; ++k) v[k] = sc.encoders[k];
  v[4] = static_cast<int16_t>(kClouds);
  FxSettings s;
  s.mode_index = sc.mode;
  s.page = 0;
  s.hidden[0] = ((sc.texture << 4 | 8) + kDefaults[4]) & 0x7fff;
  for (int k = 1; k < 4; ++k) s.hidden[k] = (sc.hidden[k - 1] + kDefaults[4 + k]) & 0x7fff;
  EncodeSettings(s, v);
}

// Every effect value within 0..32767 (the patch check's range for spring).
bool InRange(const int16_t* v) {
  for (int k = 4; k < 8; ++k) {
    if (v[k] < 0) return false;
  }
  return true;
}

uint32_t Run(const Scenario& sc, int calls, harness::CycleStats* stats, uint64_t* restart) {
  int16_t l[op1fx::kFxBlockSize], r[op1fx::kFxBlockSize];
  uint32_t x = 12345, checksum = 0;
  uint32_t phase = 0;
  int16_t values[8];
  ScenarioValues(sc, values);
  for (int c = 0; c < calls; ++c) {
    for (int i = 0; i < op1fx::kFxBlockSize; ++i) {
      x = x * 1664525u + 1013904223u;
      phase += 1 << 25;   // a saw at ~430 Hz plus some noise
      l[i] = static_cast<int16_t>((static_cast<int32_t>(phase) >> 18) + (static_cast<int32_t>(x) >> 21));
      r[i] = static_cast<int16_t>((static_cast<int32_t>(phase) >> 19) - (static_cast<int32_t>(x) >> 21));
    }
    const uint64_t t0 = harness::ReadCycles();
    if (sc.stereo) {
      FxReverbStereo(nullptr, l, r, values);
    } else {
      FxReverbMono(nullptr, l, values);
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
  // The marker: what factory presets store in values[4] is stock.
  const int16_t kStockWords[] = { 0, 4000, 8000, 32767 };
  for (int16_t w : kStockWords) {
    int16_t t[8] = { 0, 0, 0, 0, w, 0, 0, 0 };
    Check(op1fx::EngineId(t) == 0, "stock values[4]: stock engine");
  }
  int16_t t[8] = { 0, 0, 0, 0, 0xff, 0, 0, 0 };
  op1fx::SetEngineId(t, 3);
  Check(op1fx::EngineId(t) == 3 && (t[4] & 0xff) == 0xff && InRange(t),
        "marker round trip, engine bits kept, in range");
  t[4] = 0;
  op1fx::SetEngineId(t, 15);
  Check(op1fx::EngineId(t) == 15 && InRange(t), "marker id 15 in range");

  // The scenarios' encoding decodes to the values they meant.
  {
    int16_t sv[8];
    ScenarioValues(kScenarios[4], sv);   // master_gr_full
    const FxSettings fs = DecodeSettings(sv);
    Check(fs.mode_index == 1 && fs.page == 0 && fs.hidden[0] == ((819 << 4 | 8) + 16384) &&
              fs.hidden[1] == 16384 && fs.hidden[2] == 9830 && fs.hidden[3] == 16384 && InRange(sv),
          "scenario encoding round trip");
  }

  // fx1's values (Test 37: negative, out of the patch check's range) are
  // converted by the host: Clouds keeps its settings, anything else is stock.
  {
    int16_t* lv = op1fx::sim_synth_values;
    const int16_t enc[4] = { 6300, 10240, 9216, 26214 };
    for (int k = 0; k < 4; ++k) lv[k] = enc[k];
    // fx1: marker | id 3 << 11 | first 819; [5] mode bit 0 (granular);
    // [6] feedback offset 9830; [7] reverb offset 16384, page bit 1.
    lv[4] = static_cast<int16_t>(0x8000 | 3 << 11 | 819);
    lv[5] = static_cast<int16_t>(0x8000);
    lv[6] = 9830;
    lv[7] = static_cast<int16_t>(0x8000 | 16384);
    int16_t buf[op1fx::kFxBlockSize] = { };
    FxReverbMono(nullptr, buf, lv);
    const FxSettings fs = DecodeSettings(lv);
    Check(op1fx::EngineId(lv) == kCloudsEngineId && InRange(lv), "fx1 Clouds: migrated, in range");
    Check(fs.mode_index == 1 && fs.page == 1 && fs.hidden[0] == ((819 << 4 | 8) + kDefaults[0]) &&
              fs.hidden[1] == kDefaults[1] && fs.hidden[2] == ((9830 + kDefaults[2]) & 0x7fff) &&
              fs.hidden[3] == ((16384 + kDefaults[3]) & 0x7fff),
          "fx1 Clouds: settings kept");
    lv[4] = static_cast<int16_t>(0x8000 | 5 << 11);   // an engine this image lacks
    FxReverbMono(nullptr, buf, lv);
    Check(op1fx::EngineId(lv) == 0 && lv[4] == 8000 && lv[7] == 8000, "fx1 other: stock spring");
  }

  int16_t* v = op1fx::sim_synth_values;
  const int16_t page1[4] = { 6300, 10240, 9216, 26214 };
  for (int k = 0; k < 4; ++k) v[k] = page1[k];
  v[4] = static_cast<int16_t>(kClouds);
  for (int k = 5; k < 8; ++k) v[k] = 0;

  FxSettings s = DecodeSettings(v);
  Check(op1fx::EngineId(v) == kCloudsEngineId && s.mode_index == 0 && s.page == 0,
        "zero settings: Clouds, LD, page 1");
  Check(s.hidden[0] == (kDefaults[4] | 8) && s.hidden[1] == kDefaults[5] && s.hidden[2] == 0 &&
            s.hidden[3] == 0,
        "zero settings: page 2 at its defaults");

  // White: page 2 on the encoders, page 1 hidden; and back.
  Check(op1fx::SynthFxClick(op1fx::CLICK_WHITE, 1), "white consumed");
  s = DecodeSettings(v);
  Check(s.page == 1, "page 2 after white");
  int32_t values[kNumParams];
  EncoderRange ranges[4];
  for (int k = 0; k < 4; ++k) InitRange(&ranges[k], kPhoneRanges[k][0], kPhoneRanges[k][1]);
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

  // Orange: stock spring (this image's reverb host has spring, plate and
  // Clouds), with spring's factory values[4..7]; blue is then not ours;
  // orange back: Clouds again as it was left (granular, page 2).
  op1fx::SynthFxClick(op1fx::CLICK_WHITE, 1);   // to page 2 first
  int16_t left[8];
  for (int k = 0; k < 8; ++k) left[k] = v[k];
  Check(op1fx::SynthFxClick(op1fx::CLICK_ORANGE, 1), "orange consumed");
  Check(op1fx::EngineId(v) == 0, "orange: stock spring");
  for (int k = 4; k < 8; ++k) Check(v[k] == 8000, "stock: spring's factory values[4..7]");
  Check(InRange(v), "values in range after every click");
  Check(!op1fx::SynthFxClick(op1fx::CLICK_BLUE, 1), "blue on stock goes to stock");
  int16_t buf[op1fx::kFxBlockSize];
  for (int i = 0; i < op1fx::kFxBlockSize; ++i) buf[i] = static_cast<int16_t>(i * 100);
  FxReverbMono(nullptr, buf, v);
  bool untouched = true;
  for (int i = 0; i < op1fx::kFxBlockSize; ++i) untouched &= buf[i] == i * 100;
  Check(untouched, "stock: the host leaves the buffer to spring");
  op1fx::SynthFxClick(op1fx::CLICK_ORANGE, -1);
  s = DecodeSettings(v);
  Check(op1fx::EngineId(v) == kCloudsEngineId && s.mode_index == 1 && s.page == 1,
        "orange back: Clouds, granular, page 2");
  bool same = true;
  for (int k = 0; k < 8; ++k) same &= v[k] == left[k];
  Check(same, "orange back: Clouds' values as they were left");
  Check(InRange(v), "orange back: in range");
  // Stock remembers its knobs too: turn one on stock, leave and come back.
  op1fx::SynthFxClick(op1fx::CLICK_ORANGE, 1);
  v[0] = 2000;
  op1fx::SynthFxClick(op1fx::CLICK_ORANGE, -1);
  Check(op1fx::EngineId(v) == kCloudsEngineId && v[0] == left[0], "Clouds' knob back");
  op1fx::SynthFxClick(op1fx::CLICK_ORANGE, 1);
  Check(op1fx::EngineId(v) == 0 && v[0] == 2000, "stock's knob back");
  for (int k = 4; k < 8; ++k) Check(v[k] == 8000, "stock: spring's values[4..7] back");
  op1fx::SynthFxClick(op1fx::CLICK_ORANGE, -1);
  // Forward: stock, then the engines in id order (plate 1, hall 2 if in
  // the image), then Clouds.
  bool forward = op1fx::SynthFxClick(op1fx::CLICK_ORANGE, 1) && op1fx::EngineId(v) == 0;
  int32_t last_id = 0;
  for (int32_t i = 0; i < 4 && forward && op1fx::EngineId(v) != kCloudsEngineId; ++i) {
    forward = op1fx::SynthFxClick(op1fx::CLICK_ORANGE, 1) && op1fx::EngineId(v) > last_id && InRange(v);
    last_id = op1fx::EngineId(v);
  }
  Check(forward && op1fx::EngineId(v) == kCloudsEngineId, "orange forward cycles stock -> engines -> Clouds");

  // The master's clicks act on its own values.
  int16_t* m = op1fx::sim_master_values;
  for (int k = 0; k < 8; ++k) m[k] = 8000;
  bool master_forward = true;
  for (int32_t i = 0; i < 4 && master_forward && op1fx::EngineId(m) != kCloudsEngineId; ++i) {
    master_forward = op1fx::MasterFxClick(op1fx::CLICK_ORANGE, 1) && op1fx::EngineId(m) != 0;
  }
  Check(master_forward && op1fx::EngineId(m) == kCloudsEngineId,
        "master: orange from stock spring through the engines to Clouds");
  Check(DecodeSettings(m).mode_index == kEnterMode, "master: Clouds starts in its entry mode");
  {
    // The entry defaults on both pages: the encoders hold page 1, the
    // hidden page is page 2.
    EncoderRange r[4];   // the test's ranges (sim_ranges), the host's for the master
    for (int k = 0; k < 4; ++k) InitRange(&r[k], op1fx::sim_ranges[k][0], op1fx::sim_ranges[k][1]);
    const FxSettings ms = DecodeSettings(m);
    bool entry = ms.page == 0;
    for (int k = 0; k < 4; ++k) {
      const int32_t shown = EncoderToValue(r[k], m[k]);
      entry &= shown - kEnterDefaults[k] < 4 && kEnterDefaults[k] - shown < 4;
      entry &= ms.hidden[k] - kEnterDefaults[4 + k] < 16 && kEnterDefaults[4 + k] - ms.hidden[k] < 16;
    }
    Check(entry, "master: Clouds starts at the entry defaults on both pages");
  }

  // Pitch: the dead zone and the ends.
  Check(PitchQ16(16384) == 0 && PitchQ16(16384 + kPitchDeadZone) == 0, "pitch centre");
  const int32_t top = PitchQ16(32767), bottom = PitchQ16(0);
  printf("pitch ends: %.3f %.3f semitones\n", top / 65536.0, bottom / 65536.0);
  Check(top > (24 << 16) - 512 && top <= (24 << 16) + 512, "pitch +24");
  Check(bottom < -(24 << 16) + 512 && bottom >= -(24 << 16) - 512, "pitch -24");
}

// CRUSH on the dirt host (../fx/op1/crush_fx.cc): identity at its neutral
// settings, the bit depth and hold length at the far ends, the cost.
void TestCrush() {
  const int16_t kCrush = static_cast<int16_t>(op1fx::kMarker | 4 << op1fx::kEngineShift);
  // Phone's ranges in this test (sim_ranges): blue 204..20480, green
  // 3072..17408, white 1536..16896, orange 0..32767.
  auto run = [&](int16_t rate, int16_t bits, int16_t jitter, int16_t mix, int16_t* out, int n_calls,
                 bool stereo, uint64_t* cycles) {
    int16_t v[8] = { rate, bits, jitter, mix, kCrush, 0, 0, 0 };
    uint32_t phase = 0;
    *cycles = 0;
    for (int c = 0; c < n_calls; ++c) {
      int16_t l[op1fx::kFxBlockSize], r[op1fx::kFxBlockSize];
      for (int i = 0; i < op1fx::kFxBlockSize; ++i) {
        phase += 1 << 24;   // a saw at ~170 Hz
        l[i] = r[i] = static_cast<int16_t>(static_cast<int32_t>(phase) >> 17);
      }
      const uint64_t t0 = harness::ReadCycles();
      if (stereo) {
        FxDirtStereo(nullptr, l, r, v);
      } else {
        FxDirtMono(nullptr, l, v);
      }
      *cycles += harness::ReadCycles() - t0;
      if (c < 16) {   // out holds 16 calls
        for (int i = 0; i < op1fx::kFxBlockSize; ++i) out[c * op1fx::kFxBlockSize + i] = l[i];
      }
    }
  };
  const int kCalls = 16, kN = kCalls * op1fx::kFxBlockSize;
  static int16_t out[kN];
  uint64_t cycles;

  // rate 44.1 kHz, 16 bits, no jitter, mix 1: the input unchanged.
  run(204, 3072, 1536, 32767, out, kCalls, false, &cycles);
  uint32_t phase = 0;
  bool same = true;
  for (int i = 0; i < kN; ++i) {
    phase += 1 << 24;
    same &= out[i] == static_cast<int16_t>(static_cast<int32_t>(phase) >> 17);
  }
  Check(same, "crush: neutral settings leave the input unchanged");

  // 2 bits: at most 4 levels.
  run(204, 17408, 1536, 32767, out, kCalls, false, &cycles);
  int levels[8], n_levels = 0;
  for (int i = 0; i < kN; ++i) {
    bool known = false;
    for (int k = 0; k < n_levels; ++k) known |= levels[k] == out[i];
    if (!known && n_levels < 8) levels[n_levels++] = out[i];
  }
  Check(n_levels <= 4, "crush: 2 bits give at most 4 levels");

  // 500 Hz: holds of ~88 samples.
  run(20480, 3072, 1536, 32767, out, kCalls, false, &cycles);
  int runs = 1;
  for (int i = 1; i < kN; ++i) runs += out[i] != out[i - 1];
  printf("crush 500 Hz: %d holds in %d samples (~%d each)\n", runs, kN, kN / runs);
  Check(kN / runs >= 70 && kN / runs <= 100, "crush: 500 Hz holds ~88 samples");

  // Cost, stereo and mono, mid settings with jitter.
  run(12000, 10000, 9000, 26000, out, 200, true, &cycles);
  printf("crush stereo: %lu cyc/call = %.2f MHz\n", static_cast<unsigned long>(cycles / 200),
         cycles / 200.0 * 689 / 1e6);
  run(12000, 10000, 9000, 26000, out, 200, false, &cycles);
  printf("crush mono:   %lu cyc/call = %.2f MHz\n", static_cast<unsigned long>(cycles / 200),
         cycles / 200.0 * 689 / 1e6);
}


// PLATE on the reverb host (../fx/op1/plate_fx.cc): the restart call passes
// the input through, mix 0 is the dry signal, a burst leaves a tail, the
// two pages round-trip, the cost.
void TestPlate() {
  const int16_t kPlate = static_cast<int16_t>(op1fx::kMarker | 1 << op1fx::kEngineShift);
  // Burst: a saw for the first 8 calls, then silence.
  auto run = [&](int16_t mix, int n_calls, bool stereo, int64_t* energy_late, int64_t* err_dry,
                 uint64_t* cycles) {
    int16_t v[8] = { 16384, 9830, 3277, mix, kPlate, 0, 0, 0 };
    uint32_t phase = 0;
    *energy_late = *err_dry = 0;
    *cycles = 0;
    for (int c = 0; c < n_calls; ++c) {
      int16_t l[op1fx::kFxBlockSize], r[op1fx::kFxBlockSize], dry[op1fx::kFxBlockSize];
      for (int i = 0; i < op1fx::kFxBlockSize; ++i) {
        phase += 1 << 24;
        dry[i] = l[i] = r[i] =
            c < 8 || n_calls > 100 ? static_cast<int16_t>(static_cast<int32_t>(phase) >> 18) : 0;
      }
      const uint64_t t0 = harness::ReadCycles();
      if (stereo) {
        FxReverbStereo(nullptr, l, r, v);
      } else {
        FxReverbMono(nullptr, l, v);
      }
      *cycles += harness::ReadCycles() - t0;
      for (int i = 0; i < op1fx::kFxBlockSize; ++i) {
        const int32_t d = l[i] - dry[i];
        if (c == 0 && d != 0) *err_dry += 1 << 20;   // the restart call: dry
        if (c > 0) *err_dry += d < 0 ? -d : d;
        if (c >= 20) *energy_late += static_cast<int64_t>(l[i]) * l[i];
      }
    }
  };
  int64_t late, err;
  uint64_t cycles;
  // Phone's orange range is 0..32767: the encoder is the mix.
  run(0, 40, false, &late, &err, &cycles);
  printf("plate mix 0: |wet - dry| sum %lld\n", static_cast<long long>(err));
  Check(err < 7 * op1fx::kFxBlockSize * 40, "plate: the restart call and mix 0 are the dry signal");
  run(32767, 40, false, &late, &err, &cycles);
  printf("plate mix 1: tail energy after the burst %lld\n", static_cast<long long>(late));
  Check(late > 1000000, "plate: a burst leaves a tail");
  run(32767, 40, true, &late, &err, &cycles);
  Check(late > 1000000, "plate stereo: a burst leaves a tail");

  // The pages: swap and back restores the encoders and the hidden page.
  const int32_t kDefaults[8] = { 16384, 9830, 3277, 11469, 18725, 22938, 9830, 3277 };
  op1fx::EncoderRange ranges[4];
  for (int k = 0; k < 4; ++k) op1fx::InitRange(&ranges[k], kPhoneRanges[k][0], kPhoneRanges[k][1]);
  int16_t v[8] = { 0, 0, 0, 0, kPlate, 0, 0, 0 };
  op1fx::EnterTwoPage(ranges, kDefaults, v);
  v[0] = 9000;
  int16_t before[8];
  for (int k = 0; k < 8; ++k) before[k] = v[k];
  op1fx::SwapTwoPage(ranges, kDefaults, v);
  bool in_range = op1fx::EngineId(v) == 1;
  for (int k = 0; k < 8; ++k) in_range &= v[k] >= 0;
  Check(in_range, "plate: page 2 keeps the marker, every value in range");
  op1fx::SwapTwoPage(ranges, kDefaults, v);
  bool same = true;
  for (int k = 0; k < 8; ++k) same &= v[k] >= before[k] - 8 && v[k] <= before[k] + 8;
  Check(same, "plate: two page swaps round-trip");

  run(11469, 400, true, &late, &err, &cycles);
  printf("plate stereo: %lu cyc/call = %.2f MHz\n", static_cast<unsigned long>(cycles / 400),
         cycles / 400.0 * 689 / 1e6);
  run(11469, 400, false, &late, &err, &cycles);
  printf("plate mono:   %lu cyc/call = %.2f MHz\n", static_cast<unsigned long>(cycles / 400),
         cycles / 400.0 * 689 / 1e6);
}


// Every further engine on its host (tests/sim_engines.h): mix 0 dry, mix 1
// wet, the cost against its budget (the half-rate engines include ~5.3 MHz
// stereo of resampling and mixing). Knobs: encoder values on phone's ranges.
void TestEngines() {
  using sim_engines::Case;
  const Case kCases[] = {
#ifdef OP1FX_PLATE
    { "plate", FxReverbMono, FxReverbStereo, 1, { 10000, 9000, 5000 }, false, 12.0, 9.0 },
#endif
#ifdef OP1FX_HALL
    { "hall", FxReverbMono, FxReverbStereo, 2, { 10000, 9000, 5000 }, false, 13.0, 10.0 },
#endif
#ifdef OP1FX_DRIVE
    { "drive", FxDirtMono, FxDirtStereo, 1, { 14000, 10000, 6000 }, true, 5.0, 3.5 },
#endif
#ifdef OP1FX_TAPE
    { "tape", FxDirtMono, FxDirtStereo, 2, { 14000, 10000, 6000 }, true, 6.0, 4.0 },
#endif
#ifdef OP1FX_FOLD
    { "fold", FxDirtMono, FxDirtStereo, 3, { 14000, 10000, 6000 }, true, 5.0, 3.5 },
#endif
#ifdef OP1FX_ENSEMBLE
    { "ensemble", FxModulationMono, FxModulationStereo, 1, { 10000, 12000, 9000 }, false, 10.0, 7.0 },
#endif
#ifdef OP1FX_SHIFT
    { "shift", FxModulationMono, FxModulationStereo, 2, { 14000, 6000, 3000 }, true, 6.0, 4.0 },
#endif
#ifdef OP1FX_TAPE_ECHO
    { "tape echo", FxDelayMono, FxDelayStereo, 1, { 4000, 12000, 5000 }, false, 11.0, 8.0 },
#endif
    { nullptr, nullptr, nullptr, 0, { 0, 0, 0 }, false, 0, 0 },
  };
  for (const Case& c : kCases) {
    if (c.name) failures += sim_engines::Test(c);
  }
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
  op1fx::sim_ranges = kPhoneRanges;
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
    Check(checksum == sc.checksum, "checksum as the phone-hosted build's");
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
  TestCrush();
  TestPlate();
  TestEngines();
  printf(failures ? "sim-fx: %d FAILED\n" : "sim-fx: all checks passed\n", failures);
  return failures ? 1 : 0;
}
