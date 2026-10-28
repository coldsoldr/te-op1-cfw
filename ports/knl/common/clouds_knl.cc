// Clouds as a kennel effect plugin (common/clouds_knl.h): the firmware
// build's glue (ports/clouds/op1/clouds_fx.cc) on kennel's op1_fx_stereo_t.
// The core runs at half the effect rate (OP1CLOUDS_SAMPLE_RATE 22050):
// the input is decimated 2:1, the wet signal interpolated back and mixed
// with the full-rate dry input here. A restart (a pause over 5 ms: an
// effect switched off isn't called; a slot change on the synth position)
// zeroes the core 8 KB per call, passing the input through, then
// initialises it. The CPU governor (src/core/governor.h) sets the granular
// grain cap from the call interval and Clouds' own cycles; the rest of the
// machine's load is unknown here (the firmware build fed Rings' cycles in),
// so the governor leans on its lateness feedback.
// The core (~360 KB) lives in the position's fixed SDRAM slot outside
// kennel's arena (common/cs_sdram.h), shared by both Clouds plugins and
// never zero on arrival: it is always cleared before use.

#include "common/cs_sdram.h"
#include "common/clouds_knl.h"
#include "common/knl_fx.h"
#include "op1/clouds_settings.h"
#include "op1/op1.h"
#include "src/core/clouds_core.h"
#include "src/core/governor.h"
#include "src/core/halfband.h"

#ifndef CS_CLOUDS_MODE
#error "CS_CLOUDS_MODE: MODE_GRANULAR or MODE_LOOPING_DELAY"
#endif

namespace {

knl::Fine fine;   // shift + a page-2 knob: quarter clicks

using op1clouds::CloudsCore;
using op1clouds::Governor;
using op1clouds::HalfbandDecimator;
using op1clouds::HalfbandInterpolator;

const uint32_t kGapCycles = 2000000;          // 5 ms at 400 MHz
const uint32_t kBlockCounter = 0xff8016e0;    // +1 per audio block (the firmware's callback)
const uint32_t kNominalPeriod = 566 * 1024;   // Test 16: 64 samples at 44.1 kHz
const int32_t kSettleCalls = 16;              // no governor updates after a restart
const uint32_t kClearBytes = 8192;            // of the core zeroed per call (~45 calls)
const int32_t kMode = CS_CLOUDS_MODE;

template <typename T>
T Read(uint32_t address) {
  return *reinterpret_cast<const volatile T*>(address);
}

struct Instance {
  CloudsCore* core;   // the position's slot (common/cs_sdram.h)
  Governor governor;
  bool running;
  uint32_t cleared;   // bytes of core zeroed so far, while restarting
  int32_t settle;
  int32_t slot;
  uint32_t last_call;
  uint32_t last_block;
  uint32_t own;       // Clouds' cycles in the previous call
  uint32_t own_avg;   // smoothed (1/16 per call), for the screen's meter
  int32_t dry_wet;    // Q16, the mix's smoothed value
  HalfbandDecimator decimator[2];
  HalfbandInterpolator interpolator[2];
  int16_t half[2][knl::kBlock / 2];
  int16_t wet[2][knl::kBlock];   // half scale
};

static_assert(sizeof(CloudsCore) <= cssdram::kCloudsSlot, "cs_sdram: CloudsCore");
static_assert(knl::kNumPositions == cssdram::kNumCloudsSlots, "cs_sdram: Clouds slots");
Instance instances[knl::kNumPositions];
bool instances_ready;
op1clouds::SoftLimitTable soft_limit;
bool soft_limit_ready;

Instance* Get(int32_t pos) {
  pos %= knl::kNumPositions;
  if (!instances_ready) {
    for (int32_t i = 0; i < knl::kNumPositions; ++i) {
      instances[i].core = reinterpret_cast<CloudsCore*>(cssdram::kClouds + i * cssdram::kCloudsSlot);
      instances[i].slot = -1;
    }
    instances_ready = true;
  }
  return &instances[pos];
}

bool RestartStep(Instance* in, int32_t channels) {
  const uint32_t size = sizeof(CloudsCore) & ~3u;
  uint32_t end = in->cleared + kClearBytes;
  if (end > size) end = size;
  volatile uint32_t* words = reinterpret_cast<volatile uint32_t*>(in->core);
  for (uint32_t i = in->cleared >> 2; i < end >> 2; ++i) words[i] = 0;
  in->cleared = end;
  if (end < size) return false;
  in->core->Init(channels, false);
  in->core->set_external_mix(true);
  for (int32_t c = 0; c < 2; ++c) {
    in->decimator[c].Init();
    in->interpolator[c].Init();
  }
  in->dry_wet = 0;
  if (!soft_limit_ready) {
    op1clouds::InitSoftLimitTable(&soft_limit);
    soft_limit_ready = true;
  }
  in->governor.Init(channels == 1 ? 40 : 32, kNominalPeriod);
  in->core->granular()->set_max_grains(in->governor.cap());
  in->core->granular()->set_clock(op1::Cycles);
  in->running = true;
  in->settle = kSettleCalls;
  in->own = 0;
  return true;
}

inline int16_t FadeOut(int32_t dry_wet) {
  return static_cast<int16_t>(op1clouds::InterpolateQ16(op1clouds::kXfadeOutQ31, dry_wet, 4) >> 16);
}

inline int16_t WetGain(int32_t dry_wet) {
  using namespace op1clouds;
  return static_cast<int16_t>(MulQ31(InterpolateQ16(kXfadeInQ31, dry_wet, 4), Q(0.6, 31)) >> 16);
}

void Render(Instance* in, int16_t* l, int16_t* r) {
  using namespace op1clouds;
  const int32_t n = knl::kBlock;
  const int32_t channels = r ? 2 : 1;
  in->decimator[0].Process(l, in->half[0], n / 2);
  if (r) in->decimator[1].Process(r, in->half[1], n / 2);
  if (r) {
    in->core->ProcessStereo(in->half[0], in->half[1], n / 2);
  } else {
    in->core->ProcessMono(in->half[0], n / 2);
  }
  for (int32_t c = 0; c < channels; ++c) {
    int16_t* w = in->half[c];
    const int32_t* wet = in->core->wet(c);
    for (int32_t i = 0; i < n / 2; ++i) w[i] = static_cast<int16_t>(Sat16(wet[i] >> 13));
    in->interpolator[c].Process(w, in->wet[c], n / 2);
  }
  const int32_t target = in->core->mutable_parameters()->dry_wet;
  const int32_t increment = (target - in->dry_wet) / n;
  if (increment == 0) {
    in->dry_wet = target;
    const int16_t fade_out = FadeOut(target);
    const int16_t wet_gain = WetGain(target);
    MixSoftLimit(l, in->wet[0], n, fade_out, wet_gain, &soft_limit);
    if (r) MixSoftLimit(r, in->wet[1], n, fade_out, wet_gain, &soft_limit);
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
}

// The eight parameter values (0..32767, page-major) from the words.
void Values(const int16_t* p, int32_t v[8]) {
  knl::Page1(p, v);
  if (kMode == op1clouds::MODE_GRANULAR) {
    v[4] = knl::Word(p, 5);   // texture
    v[5] = knl::Word(p, 6);   // dry/wet
    v[6] = 0;                 // feedback
    v[7] = knl::Word(p, 7);   // reverb
  } else {
    v[4] = 16384;             // texture: neutral
    v[5] = knl::Word(p, 5);   // dry/wet
    v[6] = knl::Word(p, 6);   // feedback
    v[7] = knl::Word(p, 7);   // reverb
  }
}

void Process(int32_t pos, int16_t* l, int16_t* r, const int16_t* p) {
  Instance* in = Get(pos);
  const int32_t channels = r ? 2 : 1;
  const int32_t slot = pos == 0 ? op1_current_slot() : -1;
  const uint32_t start = op1::Cycles();
  const uint32_t interval = start - in->last_call;
  if (in->running && (interval > kGapCycles || slot != in->slot)) in->running = false;
  if (!in->running) {
    if (slot != in->slot) {
      in->cleared = 0;
      in->slot = slot;
    }
    if (RestartStep(in, channels)) in->cleared = 0;
    in->last_call = op1::Cycles();
    in->last_block = Read<uint32_t>(kBlockCounter);
    return;
  }
  in->last_call = start;
  const uint32_t block = Read<uint32_t>(kBlockCounter);
  const uint32_t blocks = block - in->last_block;
  in->last_block = block;

  if (in->settle) {
    --in->settle;
  } else {
    op1clouds::GranularPlayer* granular = in->core->granular();
    granular->set_max_grains(in->governor.Update(blocks, interval, in->own, granular->TakeOverlapCycles(),
                                                 granular->active_grains(), 0));
  }

  int32_t v[8];
  Values(p, v);
  op1clouds::CloudsParams* q = in->core->mutable_parameters();
  op1clouds::ApplyParams(v, q);
  q->freeze = knl::Pressed(pos, 3);
  in->core->set_playback_mode(kMode);

  const uint32_t t1 = op1::Cycles();
  Render(in, l, r);
  in->own = op1::Cycles() - t1;
  in->own_avg += static_cast<int32_t>(in->own - in->own_avg) >> 4;
}

void Init(int pos) {
  fine.Reset(pos);
  Instance* in = Get(pos);
  in->running = false;
  in->cleared = 0;   // the slot may hold the other Clouds plugin's state: clear it again
}

// Shift + a page-2 knob: quarter clicks (knl::Fine) on the three words.
void RenderMono(int pos, int16_t* buf, const int16_t* p) { fine.Apply(pos, 7); Process(pos, buf, nullptr, p); }
void RenderStereo(int pos, int16_t* l, int16_t* r, const int16_t* p) { fine.Apply(pos, 7); Process(pos, l, r, p); }

}  // namespace

namespace csclouds {

void GetScreenInfo(ScreenInfo* info) {
  const int32_t pos = knl::ScreenPos();
  const Instance* in = Get(pos);
  info->page = knl::AnyPage2(pos, 0x7) ? 1 : 0;
  info->mode = kMode;
  info->freeze = knl::Pressed(pos, 3);
  int32_t* v = info->values;
  knl::ScreenPage1(v);
  if (kMode == op1clouds::MODE_GRANULAR) {
    v[4] = knl::ScreenWord(5);
    v[5] = knl::ScreenWord(6);
    v[6] = 0;
    v[7] = knl::ScreenWord(7);
  } else {
    v[4] = 16384;
    v[5] = knl::ScreenWord(5);
    v[6] = knl::ScreenWord(6);
    v[7] = knl::ScreenWord(7);
  }
  info->pitch_q16 = op1clouds::PitchQ16(v[2]);
  info->load_percent = in->running ? static_cast<int32_t>(in->own_avg / (kNominalPeriod / 100)) : 0;
}

}  // namespace csclouds

extern "C" const op1_fx_stereo_t CS_FX_SYMBOL = {
  { Init, RenderMono, { OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_LATCH } },
  RenderStereo,
};
