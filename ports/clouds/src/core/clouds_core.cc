// The Clouds processor in fixed point. See clouds_core.h.

#include "src/core/clouds_core.h"

#include "src/core/profile.h"

namespace op1clouds {

#ifdef OP1CLOUDS_PROFILE
uint32_t (*profile_clock)() = nullptr;
uint32_t profile_cycles[PROFILE_NUM_STAGES];
uint32_t profile_instance_cycles[2];
int32_t profile_grains[2];
int32_t profile_cap[2];
#endif

namespace {

// Q16 in [0, 1] -> Q31 (1.0 saturates).
inline int32_t Q16ToQ31(int32_t x) {
  if (x <= 0) return 0;
  if (x >= 65536) return kQ31One;
  return x << 15;
}

}  // namespace

void CloudsCore::Init(int32_t num_channels, bool clear_buffers) {
  num_channels_ = num_channels;
  mode_ = MODE_LOOPING_DELAY;
  previous_mode_ = -1;   // the first block adopts the mode (upstream: LAST)
  params_ = CloudsParams();
  freeze_lp_ = 0;
  dry_wet_ = 0;
  fb_on_ = diffuser_on_ = reverb_on_ = false;
  external_mix_ = false;
  for (int32_t c = 0; c < 2; ++c) {
    for (int32_t i = 0; i < kBlockSize; ++i) {
      in_[c][i] = out_[c][i] = fb_[c][i] = 0;
    }
  }
  ResetFilters();
  // Upstream's Prepare after a buffer reset.
  if (num_channels_ == 1) {
    buffer_[0].Init(record_, kMonoRecordWords, tail_[0], clear_buffers);
  } else {
    buffer_[0].Init(record_, kStereoRecordWords, tail_[0], clear_buffers);
    buffer_[1].Init(record_ + kStereoRecordWords, kStereoRecordWords, tail_[1], clear_buffers);
  }
  diffuser_.Init(clear_buffers);
  reverb_.Init(clear_buffers);
  pitch_shifter_.Init(pitch_shifter_buffer_, clear_buffers);
  looper_.Init(num_channels_);
  // Upstream's grain count for 16-bit quality.
  granular_.Init(num_channels_, num_channels_ == 1 ? 40 : 32);
  random_.Init();
}

void CloudsCore::ResetFilters() {
  for (int32_t c = 0; c < 2; ++c) {
    fb_filter_[c].Init();
    lp_filter_[c].Init();
    hp_filter_[c].Init();
  }
}

void CloudsCore::ProcessStereo(int16_t* l, int16_t* r, int32_t size) {
  for (int32_t i = 0; i < size; i += kBlockSize) ProcessBlock(l + i, r + i);
}

void CloudsCore::ProcessMono(int16_t* in_out, int32_t size) {
  for (int32_t i = 0; i < size; i += kBlockSize) ProcessBlock(in_out + i, nullptr);
}

void CloudsCore::ProcessBlock(int16_t* l, int16_t* r) {
  const int32_t n = kBlockSize;
  const int32_t channels = r ? 2 : 1;

  // A mode change: one block of silence, then the filters and the pitch
  // shifter restart (upstream's Process, then Prepare's benign change).
  if (previous_mode_ < 0) previous_mode_ = mode_;
  if (mode_ != previous_mode_) {
    for (int32_t i = 0; i < n; ++i) {
      l[i] = 0;
      if (r) r[i] = 0;
      out_[0][i] = out_[1][i] = 0;
    }
    ResetFilters();
    pitch_shifter_.Clear();
    previous_mode_ = mode_;
    return;
  }

  const CloudsParams& p = params_;
  OP1CLOUDS_PROFILE_MARK(profile_t);
  for (int32_t i = 0; i < n; ++i) {
    in_[0][i] = static_cast<int32_t>(l[i]) << 12;
    if (r) in_[1][i] = static_cast<int32_t>(r[i]) << 12;
  }

  // Feedback, high-passed against low-frequency build-ups.
  freeze_lp_ += MulQ31Fast((p.freeze ? kQ31One : 0) - freeze_lp_, kFreezeSmoothingQ31);
  const FFloat feedback = Q16ToFFloat(p.feedback);
  {
    static constexpr FFloat k20 = FFloat(20.0);
    static constexpr FFloat k100 = FFloat(100.0);
    static constexpr FFloat kInvRate = FFloat(1.0 / kSampleRate);
    static constexpr FFloat kOne = FFloat(1.0);
    // Skipped while feedback is 0 (it then has no effect on the output);
    // the filters restart from rest when it comes back.
    if (p.feedback > 0) {
      if (!fb_on_) {
        fb_filter_[0].Init();
        fb_filter_[1].Init();
        fb_on_ = true;
      }
      const SvfCoefficients c = MakeSvfCoefficients((k20 + k100 * feedback * feedback) * kInvRate, kOne);
      for (int32_t ch = 0; ch < channels; ++ch) {
        fb_filter_[ch].Process<SVF_HIGH_PASS>(c, fb_[ch], n);
      }
    } else {
      fb_on_ = false;
    }
  }
  const int32_t fb_gain = MulQ31(Q16ToQ31(p.feedback), kQ31One - freeze_lp_);
  if (fb_gain) {
    const int32_t fb_gain_14 = MulQ31(fb_gain, Q(0.7, 31));   // fb_gain x 1.4, Q30
    for (int32_t ch = 0; ch < channels; ++ch) {
      int32_t* in = in_[ch];
      const int32_t* fb = fb_[ch];
      for (int32_t i = 0; i < n; ++i) {
        const int32_t x = (MulQ31Fast(fb[i], fb_gain_14) << 1) + in[i];
        in[i] += MulQ31Fast(SoftLimitQ27(x) - in[i], fb_gain);
      }
    }
  }

  // The record buffer, then the player.
  for (int32_t ch = 0; ch < channels; ++ch) {
    buffer_[ch].WriteFade(in_[ch], n, 1, !p.freeze);
  }
  OP1CLOUDS_PROFILE_ADD(PROFILE_RECORD, profile_t);
  switch (mode_) {
    case MODE_LOOPING_DELAY:
      looper_.Play(buffer_, p, out_[0], out_[1], n);
      break;
    case MODE_GRANULAR:
      granular_.Play(buffer_, p, &random_, out_[0], r ? out_[1] : nullptr, n);
      break;
    default:
      for (int32_t ch = 0; ch < channels; ++ch) {
        for (int32_t i = 0; i < n; ++i) out_[ch][i] = 0;
      }
      break;
  }
  int32_t* const out_r = r ? out_[1] : nullptr;
  OP1CLOUDS_PROFILE_ADD(PROFILE_PLAYER, profile_t);

  // Diffusion and pitch shifting.
  if (mode_ != MODE_SPECTRAL) {
    int32_t diffusion;
    if (mode_ == MODE_GRANULAR) {
      diffusion = p.texture > 49152 ? (p.texture - 49152) << 2 : 0;
    } else {
      diffusion = p.density;
    }
    // Skipped at 0 (no effect on the output); cleared when it comes back.
    if (diffusion > 0) {
      if (!diffuser_on_) {
        diffuser_.Clear();
        diffuser_on_ = true;
      }
      diffuser_.set_amount(Q16ToQ31(diffusion));
      diffuser_.Process(out_[0], out_r, n);
    } else {
      diffuser_on_ = false;
    }
  }
  if (mode_ == MODE_LOOPING_DELAY && (!p.freeze || looper_.synchronized())) {
    pitch_shifter_.Prepare(SemitonesToRatio(p.pitch), p.size);
    pitch_shifter_.Process(out_[0], out_r, n);
  }

  // The texture filters.
  if (mode_ == MODE_LOOPING_DELAY || mode_ == MODE_STRETCH) {
    const int32_t cutoff = p.texture;
    static constexpr FFloat kHalf = FFloat(0.5);
    static constexpr FFloat kQuarter = FFloat(0.25);
    static constexpr FFloat kMax = FFloat(0.499);
    static constexpr FFloat kOne = FFloat(1.0);
    static constexpr FFloat kThree = FFloat(3.0);
    FFloat lp_cutoff = kHalf * SemitonesToRatio(cutoff < 32768 ? (cutoff - 32768) * 216 : 0);
    FFloat hp_cutoff = kQuarter * SemitonesToRatio((cutoff < 32768 ? -32768 : cutoff - 65536) * 216);
    lp_cutoff = FFloat::Min(lp_cutoff, kMax);
    hp_cutoff = FFloat::Min(hp_cutoff, kMax);
    const FFloat lpq = kOne + kThree * (kOne - feedback) * (kHalf - lp_cutoff);
    const SvfCoefficients lp = MakeSvfCoefficients(lp_cutoff, lpq);
    const SvfCoefficients hp = MakeSvfCoefficients(hp_cutoff, kOne);
    for (int32_t ch = 0; ch < channels; ++ch) {
      lp_filter_[ch].Process<SVF_LOW_PASS>(lp, out_[ch], n);
      hp_filter_[ch].Process<SVF_HIGH_PASS>(hp, out_[ch], n);
    }
  }

  // What is fed back (the reverb isn't).
  for (int32_t ch = 0; ch < channels; ++ch) {
    for (int32_t i = 0; i < n; ++i) fb_[ch][i] = out_[ch][i];
  }
  OP1CLOUDS_PROFILE_ADD(PROFILE_DIFFUSE, profile_t);

  // The reverb.
  {
    const int32_t fb16 = p.feedback < 0 ? 0 : (p.feedback > 65536 ? 65536 : p.feedback);
    int32_t amount = MulQ31(Q16ToQ31(p.reverb), Q(0.95, 31));
    // + feedback (2 - feedback) freeze_lp.
    const int32_t fb_term = static_cast<int32_t>((static_cast<int64_t>(fb16) * (131072 - fb16)) >> 16);
    amount += MulQ31(Q16ToQ31(fb_term), freeze_lp_);
    if (amount < 0) amount = kQ31One;   // overflow past 1.0
    const int32_t wet = MulQ31(amount, Q(0.54, 31));
    // Skipped at 0 (no effect on the output); cleared when it comes back.
    if (wet > 0) {
      if (!reverb_on_) {
        reverb_.Clear();
        reverb_on_ = true;
      }
      reverb_.set_amount(wet);
      reverb_.set_diffusion(Q(0.7, 31));
      reverb_.set_time(Q(0.35, 31) + MulQ31(amount, Q(0.63, 31)));
      reverb_.set_input_gain(Q(0.2, 31));
      reverb_.set_lp(Q(0.6, 31) + MulQ31(Q16ToQ31(p.feedback), Q(0.37, 31)));
      reverb_.Process(out_[0], out_r, n);
    } else {
      reverb_on_ = false;
    }
  }
  OP1CLOUDS_PROFILE_ADD(PROFILE_REVERB, profile_t);

  if (external_mix_) return;

  // Dry/wet: upstream's equal-power crossfade, the wet side x 1.2, then
  // SoftConvert (soft limit of x / 2, to 16 bits).
  // While the knob is still (the usual case) the fades are constant over
  // the block: computed once, with the 1.2 folded into the wet one (Q30).
  {
    const int32_t increment = (p.dry_wet - dry_wet_) / n;
    if (increment == 0) {
      const int32_t fade_out = InterpolateQ16(kXfadeOutQ31, dry_wet_, 4);
      const int32_t wet_gain = MulQ31(InterpolateQ16(kXfadeInQ31, dry_wet_, 4), Q(0.6, 31));
      for (int32_t ch = 0; ch < channels; ++ch) {
        int16_t* io = ch ? r : l;
        const int32_t* out = out_[ch];
        for (int32_t i = 0; i < n; ++i) {
          const int32_t dry = static_cast<int32_t>(io[i]) << 12;
          const int32_t x = MulQ31Fast(dry, fade_out) + (MulQ31Fast(out[i], wet_gain) << 1);
          io[i] = static_cast<int16_t>(Sat16(SoftLimitQ27(x >> 1) >> 12));
        }
      }
    } else {
      int32_t dry_wet = dry_wet_;
      for (int32_t i = 0; i < n; ++i) {
        dry_wet += increment;
        const int32_t fade_out = InterpolateQ16(kXfadeOutQ31, dry_wet, 4);
        const int32_t wet_gain = MulQ31(InterpolateQ16(kXfadeInQ31, dry_wet, 4), Q(0.6, 31));
        for (int32_t ch = 0; ch < channels; ++ch) {
          int16_t* io = ch ? r : l;
          const int32_t dry = static_cast<int32_t>(io[i]) << 12;
          const int32_t x = MulQ31Fast(dry, fade_out) + (MulQ31Fast(out_[ch][i], wet_gain) << 1);
          io[i] = static_cast<int16_t>(Sat16(SoftLimitQ27(x >> 1) >> 12));
        }
      }
      dry_wet_ = dry_wet;
    }
  }
}

}  // namespace op1clouds
