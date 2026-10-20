// Clouds as an engine of the OP-1's reverb category (op1/clouds_fx.h; until
// 2026-10 it replaced phone): the master effect (stereo) and the live
// instrument's effect (mono: a synth slot or the drum kit; only the live one
// exists), one CloudsCore each. The host (../fx/op1/fx_host.cc) decides
// which positions play Clouds, from the marker in their values, and calls
// stock spring otherwise.
//
// A position (re)starts after a pause (an effect switched off isn't called:
// a gap over 5 ms), after the host played something else there (stop), and
// for the mono position when the slot or the drum kit changes. A restart
// zeroes the CloudsCore (~360 KB) kClearBytes per call,
// passing the input through, then initialises it: one call doing it all
// takes longer than the 5 ms gap on the device (SDRAM), and the next call
// would restart again, starving the app (clouds1 hung at boot).
//
// The core runs at half the effect rate (OP1CLOUDS_SAMPLE_RATE 22050, to
// halve its CPU): the input is decimated 2:1, the core's wet signal is
// interpolated back and mixed with the full-rate dry input here
// (src/core/halfband.h; the core's own dry/wet stage is skipped).
//
// Per call, the CPU governor (src/core/governor.h) sets the granular mode's
// grain cap from the audio blocks elapsed, Clouds' own cycles and the cycles of
// the rest of the image (measured_cycles: the synth engines and the other
// position), skipping the first calls after a restart. (Test 29 tried a
// share of 0.40 so the screen keeps its frame rate next to Rings: Clouds
// sounded thin. The user prefers the sound; the share stays at its default.)
//
// Simulator builds (-DOP1_SIM, tests/sim_fx.cc): no firmware; the host's
// entries play Clouds on the values the call passes.

#include "op1/clouds_fx.h"

#include "op1/clouds_settings.h"
#include "op1/op1.h"
#include "src/core/clouds_core.h"
#include "src/core/governor.h"
#include "src/core/profile.h"
#include "src/core/halfband.h"

namespace {

using op1clouds::CloudsCore;
using op1clouds::EncoderRange;
using op1clouds::FxSettings;
using op1clouds::Governor;
using op1clouds::HalfbandDecimator;
using op1clouds::HalfbandInterpolator;

const uint32_t kGapCycles = 2000000;          // 5 ms at 400 MHz
const uint32_t kBlockCounter = 0xff8016e0;    // +1 per audio block (the firmware's callback)
const uint32_t kNominalPeriod = 566 * 1024;   // Test 16: 64 samples at 44.1 kHz
const int32_t kSettleCalls = 16;              // no governor updates after a restart
const uint32_t kClearBytes = 8192;            // of the core zeroed per call (~45 calls)

template <typename T>
T Read(uint32_t address) {
  return *reinterpret_cast<const volatile T*>(address);
}

struct Instance {
  CloudsCore core;
  Governor governor;
  bool running;
  uint32_t cleared; // bytes of core zeroed so far, while restarting
  bool freeze;
  int32_t settle;
  int32_t slot;
  uint32_t last_call;
  uint32_t last_block;   // the block counter at the previous call
  uint32_t own;     // Clouds' cycles in the previous call
  uint32_t own_avg; // smoothed (1/16 per call), for the screen's meter
  uint32_t mark;    // measured_cycles after the previous call
  int32_t dry_wet;  // Q16, the mix's smoothed value
  HalfbandDecimator decimator[2];
  HalfbandInterpolator interpolator[2];
  int16_t half[2][op1fx::kFxBlockSize / 2];
  int16_t wet[2][op1fx::kFxBlockSize];   // half scale (+-2 of headroom)
};

Instance synth_fx;
Instance master_fx;

op1clouds::SoftLimitTable soft_limit;

Instance* InstanceAt(int32_t position) {
  return position == op1fx::POSITION_MASTER ? &master_fx : &synth_fx;
}

// One step of a restart: zeroes the next kClearBytes of the core, and
// after the last one initialises it. Returns whether it's running.
bool RestartStep(Instance* in, int32_t channels) {
  const uint32_t size = sizeof(CloudsCore) & ~3u;
  uint32_t end = in->cleared + kClearBytes;
  if (end > size) end = size;
  volatile uint32_t* words = reinterpret_cast<volatile uint32_t*>(&in->core);
  for (uint32_t i = in->cleared >> 2; i < end >> 2; ++i) words[i] = 0;
  in->cleared = end;
  if (end < size) return false;
  in->core.Init(channels, false);
  in->core.set_external_mix(true);
  for (int32_t c = 0; c < 2; ++c) {
    in->decimator[c].Init();
    in->interpolator[c].Init();
  }
  in->dry_wet = 0;
  op1clouds::InitSoftLimitTable(&soft_limit);
  in->governor.Init(channels == 1 ? 40 : 32, kNominalPeriod);
  in->core.granular()->set_max_grains(in->governor.cap());
  in->core.granular()->set_clock(op1::Cycles);
  in->running = true;
  in->settle = kSettleCalls;
  in->own = 0;
  return true;
}

// The crossfade (Q16 dry/wet) in Q15: the dry side, and the wet side x 0.6
// (upstream's x 1.2 of half the sum).
inline int16_t FadeOut(int32_t dry_wet) {
  return static_cast<int16_t>(op1clouds::InterpolateQ16(op1clouds::kXfadeOutQ31, dry_wet, 4) >> 16);
}

inline int16_t WetGain(int32_t dry_wet) {
  using namespace op1clouds;
  return static_cast<int16_t>(MulQ31(InterpolateQ16(kXfadeInQ31, dry_wet, 4), Q(0.6, 31)) >> 16);
}

// The core at half rate on one call: the decimated input in, the wet
// signal interpolated back and mixed with the dry input as the core would
// (upstream's equal-power crossfade, the wet side x 1.2, soft limit of
// half the sum), then x 2: unity gain on the OP-1 (src/core/halfband.h). 16 x 16 products throughout: the wet signal is kept at
// half scale, the fades in Q15.
void Render(Instance* in, int16_t* l, int16_t* r) {
  using namespace op1clouds;
  const int32_t n = op1fx::kFxBlockSize;
  const int32_t channels = r ? 2 : 1;
  OP1CLOUDS_PROFILE_MARK(profile_t);
  in->decimator[0].Process(l, in->half[0], n / 2);
  if (r) in->decimator[1].Process(r, in->half[1], n / 2);
  OP1CLOUDS_PROFILE_ADD(PROFILE_RESAMPLE_MIX, profile_t);
  if (r) {
    in->core.ProcessStereo(in->half[0], in->half[1], n / 2);
  } else {
    in->core.ProcessMono(in->half[0], n / 2);
  }
  OP1CLOUDS_PROFILE_RESET(profile_t);
  for (int32_t c = 0; c < channels; ++c) {
    // The half-rate buffer is free again: the wet signal, half scale.
    int16_t* w = in->half[c];
    const int32_t* wet = in->core.wet(c);
    for (int32_t i = 0; i < n / 2; ++i) w[i] = static_cast<int16_t>(Sat16(wet[i] >> 13));
    in->interpolator[c].Process(w, in->wet[c], n / 2);
  }

  const int32_t target = in->core.mutable_parameters()->dry_wet;
  const int32_t increment = (target - in->dry_wet) / n;
  if (increment == 0) {
    // The knob is still (the usual case): constant fades.
    in->dry_wet = target;
    const int16_t fade_out = FadeOut(target);
    const int16_t wet_gain = WetGain(target);
    MixSoftLimit(l, in->wet[0], n, fade_out, wet_gain, &soft_limit);
    if (r) MixSoftLimit(r, in->wet[1], n, fade_out, wet_gain, &soft_limit);
    OP1CLOUDS_PROFILE_ADD(PROFILE_RESAMPLE_MIX, profile_t);
    return;
  }
  int32_t dry_wet = in->dry_wet;
  for (int32_t i = 0; i < n; ++i) {
    dry_wet += increment;
    const int16_t fade_out = FadeOut(dry_wet);
    const int16_t wet_gain = WetGain(dry_wet);
    MixSoftLimitC(&l[i], &in->wet[0][i], 1, fade_out, wet_gain, &soft_limit);
    if (r) MixSoftLimitC(&r[i], &in->wet[1][i], 1, fade_out, wet_gain, &soft_limit);
  }
  in->dry_wet = dry_wet;
  OP1CLOUDS_PROFILE_ADD(PROFILE_RESAMPLE_MIX, profile_t);
}

// Clouds on one call. `live`: the encoders as passed (the LFO's copy),
// `values`: the position's 8 stored values, `s` their settings. A call that
// restarts leaves the input as it is.
void Process(Instance* in, int32_t channels, int32_t slot, int16_t* l, int16_t* r,
             const int16_t* live, const int16_t* values, const FxSettings& s,
             const EncoderRange* ranges) {
  const uint32_t start = op1::Cycles();
  const uint32_t interval = start - in->last_call;
  if (in->running && (interval > kGapCycles || slot != in->slot)) in->running = false;
  if (!in->running) {
    if (in->cleared == 0 || slot != in->slot) {
      in->cleared = 0;
      in->freeze = false;
      in->slot = slot;
    }
    if (RestartStep(in, channels)) in->cleared = 0;
    const uint32_t end = op1::Cycles();
    in->last_call = end;
#ifndef OP1_SIM
    in->last_block = Read<uint32_t>(kBlockCounter);
#endif
    op1fx::measured_cycles += end - start;
    in->mark = op1fx::measured_cycles;
    return;
  }
  in->last_call = start;
#ifdef OP1_SIM
  const uint32_t blocks = 1;
  const uint32_t call_interval = kNominalPeriod;   // the simulator's cycles aren't a clock
#else
  const uint32_t call_interval = interval;
  const uint32_t block = Read<uint32_t>(kBlockCounter);
  const uint32_t blocks = block - in->last_block;
  in->last_block = block;
#endif

  // The grain cap: the rest of the image's cycles since our previous call.
  const uint32_t others = op1fx::measured_cycles - in->mark;
  if (in->settle) {
    --in->settle;
  } else {
    op1clouds::GranularPlayer* granular = in->core.granular();
    in->core.granular()->set_max_grains(in->governor.Update(
        blocks, call_interval, in->own, granular->TakeOverlapCycles(), granular->active_grains(),
        others));
  }

  int32_t v[op1clouds::kNumParams];
  op1clouds::ParamValues(ranges, live, s, v);
  op1clouds::CloudsParams* p = in->core.mutable_parameters();
  op1clouds::ApplyParams(v, p);
  p->freeze = in->freeze;
  in->core.set_playback_mode(op1clouds::kModeOrder[s.mode_index]);

  const uint32_t t1 = op1::Cycles();
  Render(in, l, r);
  const uint32_t end = op1::Cycles();
  in->own = end - t1;
  in->own_avg += static_cast<int32_t>(in->own - in->own_avg) >> 4;
#ifdef OP1CLOUDS_PROFILE
  op1clouds::profile_instance_cycles[channels - 1] += in->own;
  op1clouds::profile_grains[channels - 1] = in->core.granular()->active_grains();
  op1clouds::profile_cap[channels - 1] = in->governor.cap();
  op1clouds::profile_late[channels - 1] = in->governor.late_calls();
  op1clouds::profile_interval[channels - 1] = in->governor.mean_interval_permille();
#endif
  op1fx::measured_cycles += end - start;
  in->mark = op1fx::measured_cycles;
}


void EngineProcess(int32_t position, int32_t slot, int16_t* l, int16_t* r, const int16_t* live,
                   const int16_t* values, const EncoderRange* ranges) {
  const FxSettings s = op1clouds::DecodeSettings(values);
  Process(InstanceAt(position), r ? 2 : 1, slot, l, r, live, values, s, ranges);
}

void EngineStop(int32_t position) { InstanceAt(position)->running = false; }

void EngineEnter(int16_t* values, const EncoderRange*) { op1clouds::EnterSettings(values); }

// blue = next mode (shift: back), green = freeze, white = page.
bool EngineClick(int32_t position, int16_t* values, const EncoderRange* ranges, op1fx::Click click,
                 int32_t direction, bool* changed) {
  *changed = false;
  switch (click) {
    case op1fx::CLICK_BLUE:
      op1clouds::StepMode(values, direction);
      break;
    case op1fx::CLICK_GREEN: {
      // Freeze isn't saved: it holds the live buffer.
      Instance* in = InstanceAt(position);
      in->freeze = !in->freeze;
      return true;
    }
    case op1fx::CLICK_WHITE:
      op1clouds::SwapPage(ranges, values);
      break;
    default:
      return false;
  }
  *changed = true;
  return true;
}

}  // namespace

namespace op1clouds {

const op1fx::Engine kCloudsEngine = {
  kCloudsEngineId, "clouds.svg", EngineProcess, MigrateSettings, EngineStop, EngineEnter, EngineClick,
#ifdef OP1_SIM
  nullptr, nullptr,   // no screen in the simulator
#else
  ScreenInit, ScreenUpdate,
#endif
};

void GetScreenInfo(int32_t screen_position, const int16_t* encoders, const int16_t* values,
                   const op1fx::EncoderRange* ranges, ScreenInfo* info) {
  const FxSettings s = DecodeSettings(values);
  const Instance* in = screen_position == op1fx::kScreenMaster ? &master_fx : &synth_fx;
  info->page = s.page;
  info->mode = kModeOrder[s.mode_index];
  info->freeze = in->freeze;
  ParamValues(ranges, encoders, s, info->values);
  info->pitch_q16 = PitchQ16(info->values[2]);
  info->load_percent = in->running ? static_cast<int32_t>(in->own_avg / (kNominalPeriod / 100)) : 0;
}

}  // namespace op1clouds
