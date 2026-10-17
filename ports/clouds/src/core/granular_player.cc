// Clouds' granular player in fixed point. See granular_player.h.

#include "src/core/granular_player.h"

#include "src/core/grain_kernel.h"

#ifdef OP1CLOUDS_GRAIN_LOG
#include <cstdio>   // diagnostics: one line per scheduled grain on stderr
#endif

namespace op1clouds {

namespace {

const int32_t kQ30One = 1 << 30;

// static_cast<int32_t>(x): truncation towards 0.
inline int32_t Trunc(FFloat x) {
  return x.is_negative() ? -(-x).Floor() : x.Floor();
}

// The grain envelope's triangle, 2 min(phase, 2 - phase) / 2 in Q31, from
// the 32-bit fraction of [0, 2).
inline int32_t Triangle(uint32_t phase) {
  const uint32_t t = phase < 0x80000000u ? phase : 0u - phase;
  return t >= 0x80000000u ? kQ31One : static_cast<int32_t>(t);
}

}  // namespace

void GranularPlayer::Init(int32_t num_channels, int32_t max_num_grains) {
  num_channels_ = num_channels;
  max_num_grains_ = max_num_grains > kMaxNumGrains ? kMaxNumGrains : max_num_grains;
  cap_ = max_num_grains_;
  num_midfi_grains_ = 3 * max_num_grains_ / 4;
  gain_normalization_ = kQ30One;
  for (int32_t i = 0; i < kMaxNumGrains; ++i) {
    grains_[i].active = false;
    grains_[i].k = grains_[i].num_samples = 0;
  }
  num_grains_ = 0;
  grain_size_hint_ = Scaled(1024) << 16;
  grain_rate_phasor_ = 0;
  active_grains_ = 0;
}

void GranularPlayer::set_max_grains(int32_t n) {
  cap_ = n < 1 ? 1 : (n > max_num_grains_ ? max_num_grains_ : n);
}

void GranularPlayer::Play(const RecordBuffer* buffer, const CloudsParams& p, Lcg* random,
                          int32_t* out_l, int32_t* out_r, int32_t size) {
  // Upstream's GranularProcessor: density and texture as meta parameters.
  static constexpr FFloat k053 = FFloat(0.53);
  static constexpr FFloat k047 = FFloat(0.47);
  static constexpr FFloat k212 = FFloat(2.12);
  static constexpr FFloat k1333 = FFloat(1.333);
  static constexpr FFloat kOne = FFloat(1.0);
  static constexpr FFloat kTwo = FFloat(2.0);
  const FFloat density = Q16ToFFloat(p.density);
  const bool use_deterministic_seed = p.density < 32768;
  FFloat overlap;
  if (density >= k053) {
    overlap = (density - k053) * k212;
  } else if (density <= k047) {
    overlap = (k047 - density) * k212;
  }
  const FFloat texture = Q16ToFFloat(p.texture);
  const FFloat window_shape = p.texture < 49152 ? texture * k1333 : kOne;
  const int32_t window_shape_q16 = window_shape.ToQ(16);

  // GranularSamplePlayer::Play.
  const FFloat overlap_3 = overlap * overlap * overlap;
  const FFloat target_num_grains = FFloat::FromInt(max_num_grains_) * overlap_3;
  const FFloat hint = FFloat::FromQ(grain_size_hint_, 16);
  // The probabilistic seed's threshold on a random word (upstream: a random
  // float in [0, 1) < target / hint), and the deterministic spacing.
  uint32_t threshold = 0;
  bool probabilistic = false;
  int32_t space_q16 = INT32_MAX;
  if (!target_num_grains.is_zero()) {
    space_q16 = (hint / target_num_grains).ToQ(16);
  }
  if (use_deterministic_seed) {
    probabilistic = false;
  } else {
    grain_rate_phasor_ = -1000 << 16;
    const FFloat prob = target_num_grains / hint;
    if (!prob.is_zero()) {
      probabilistic = true;
      threshold = prob >= kOne ? 0xffffffffu : static_cast<uint32_t>(prob.ToQ(31)) << 1;
    }
  }
  const int32_t target_q16 = target_num_grains.ToQ(16);

  // Available grains (inactive, below the cap); active ones anywhere.
  int32_t num_available = 0;
  int32_t num_active = 0;
  for (int32_t i = 0; i < max_num_grains_; ++i) {
    if (grains_[i].active) {
      ++num_active;
    } else if (i < cap_) {
      available_[num_available++] = i;
    }
  }

  // Schedule new grains.
  bool seed_trigger = p.trigger;
  const int32_t buffer_size = buffer[0].size();
  const int32_t head = buffer[0].head();
  for (int32_t t = 0; t < size; ++t) {
    grain_rate_phasor_ = grain_rate_phasor_ > INT32_MAX - 65536 ? INT32_MAX : grain_rate_phasor_ + 65536;
    const uint32_t word = random->Next();
    const bool seed_probabilistic = probabilistic && word < threshold && target_q16 > num_grains_;
    const bool seed_deterministic = grain_rate_phasor_ >= space_q16;
    if (num_available && (seed_probabilistic || seed_deterministic || seed_trigger)) {
      --num_available;
      const int32_t index = available_[num_available];
      const int32_t quality = num_available < num_midfi_grains_ ? GRAIN_QUALITY_MEDIUM : GRAIN_QUALITY_HIGH;
      Schedule(&grains_[index], p, window_shape_q16, random, t, buffer_size, head - size + t, quality);
      ++num_active;
      grain_rate_phasor_ = 0;
      seed_trigger = false;
    }
  }

  // Overlap the grains.
  for (int32_t t = 0; t < size; ++t) {
    out_l[t] = 0;
    if (out_r) out_r[t] = 0;
  }
  for (int32_t i = 0; i < max_num_grains_; ++i) {
    OverlapAdd(&grains_[i], buffer, out_l, out_r, size);
  }
  active_grains_ = num_active;

  // Gain normalisation: SLOPE(num_grains, active, 0.9, 0.2), then
  // 1 / sqrt(num_grains - 1) above 2 grains, x crossfade(1, window gain,
  // overlap).
  const int32_t error = (num_active << 16) - num_grains_;
  num_grains_ += MulQ31(error, error > 0 ? Q(0.9, 31) : Q(0.2, 31));
  FFloat gain = kOne;
  if (num_grains_ > (2 << 16)) {
    gain = FFloat::ReciprocalSqrt(FFloat::FromQ(num_grains_ - 65536, 16));
  }
  FFloat window_gain = kOne + kTwo * window_shape;
  window_gain = FFloat::Min(FFloat::Max(window_gain, kOne), kTwo);
  gain = gain * (kOne + (window_gain - kOne) * overlap);
  const int32_t target_gain = gain.ToQ(30);
  int32_t g = gain_normalization_;
  int32_t t = 0;
  // ONE_POLE(gain, target, 0.01) per sample, until it settles (the step
  // rounds to 0); then the gain is constant for the rest of the block.
  for (; t < size; ++t) {
    const int32_t step = MulQ31Fast(target_gain - g, kGainSmoothingQ31);
    if (step == 0) break;
    g += step;
    out_l[t] = MulQ31Fast(out_l[t], g) << 1;
    if (out_r) out_r[t] = MulQ31Fast(out_r[t], g) << 1;
  }
  for (; t < size; ++t) {
    out_l[t] = MulQ31Fast(out_l[t], g) << 1;
    if (out_r) out_r[t] = MulQ31Fast(out_r[t], g) << 1;
  }
  gain_normalization_ = g;
}

void GranularPlayer::Schedule(Grain* g, const CloudsParams& p, int32_t window_shape, Lcg* random,
                              int32_t pre_delay, int32_t buffer_size, int32_t buffer_head,
                              int32_t quality) {
  static constexpr FFloat kQuarter = FFloat(0.25);
  static constexpr FFloat kOne = FFloat(1.0);
  static constexpr FFloat kHalf = FFloat(0.5);
  static constexpr FFloat k001 = FFloat(0.01);
  static constexpr FFloat kScale = FFloat(kRateScale);

  // Interpolate(lut_grain_size, size, 256), in samples (scaled to the rate).
  int32_t s = p.size < 0 ? 0 : (p.size > 65536 ? 65536 : p.size);
  const int32_t index = s << 8;
  const int32_t i = index >> 16;
  const int32_t frac = index & 0xffff;
  const int32_t size_q16 = (kGrainSize[i] << 16) + (kGrainSize[i + 1] - kGrainSize[i]) * frac;
  FFloat grain_size = FFloat::FromQ(size_q16, 16);
  if (OP1CLOUDS_SAMPLE_RATE != 32000) grain_size = grain_size * kScale;

  const FFloat pitch_ratio = SemitonesToRatio(p.pitch);
  const FFloat inv_pitch_ratio = SemitonesToRatio(-p.pitch);

  // pan = 0.5 + spread (random - 0.5), Q16.
  const uint32_t word = random->Next();
  const int32_t centred = static_cast<int32_t>(word >> 16) - 32768;   // Q16, [-0.5, 0.5)
  const int32_t pan = 32768 + static_cast<int32_t>((static_cast<int64_t>(p.stereo_spread) * centred) >> 16);
  int32_t gain_l, gain_r;
  if (num_channels_ == 1) {
    gain_l = InterpolateQ16(kSinQ31, pan, 8);
    gain_r = InterpolateQ16(kSinQ31 + 256, pan, 8);
  } else if (pan < 32768) {
    gain_l = kQ31One;
    gain_r = pan >= 32768 ? kQ31One : pan << 16;
  } else {
    gain_r = kQ31One;
    const int32_t x = 65536 - pan;
    gain_l = x >= 32768 ? kQ31One : x << 16;
  }

  if (pitch_ratio > kOne) {
    const FFloat limit = FFloat::FromInt(buffer_size) * kQuarter * inv_pitch_ratio;
    grain_size = FFloat::Min(grain_size, limit);
  }
  const FFloat eaten_by_play_head = grain_size * pitch_ratio;
  const FFloat available = FFloat::FromInt(buffer_size) - eaten_by_play_head - grain_size;
  const int32_t width = grain_size.Floor() & ~1;
  const int32_t start = buffer_head - Trunc(Q16ToFFloat(p.position) * available + eaten_by_play_head);

#ifdef OP1CLOUDS_GRAIN_LOG
  fprintf(stderr, "G pre %d head %d start %d size %d inc %u q %d gl %.5f gr %.5f shape %.5f\n", (int)pre_delay, (int)buffer_head, (int)start, (int)width, (unsigned)pitch_ratio.ToQ(16), (int)quality, gain_l / 2147483648.0, gain_r / 2147483648.0, window_shape / 65536.0);
#endif
  // Grain::Start.
  g->pre_delay = pre_delay;
  int32_t first = start + buffer_size;
  while (first >= buffer_size) first -= buffer_size;
  while (first < 0) first += buffer_size;
  g->first_sample = first;
  g->phase_increment = static_cast<int32_t>(static_cast<uint32_t>(pitch_ratio.ToQ(16)));
  g->phase = 0;
  // 2 / width of [0, 2): 2^32 / width, and the envelope's sample counts.
  const uint32_t env_inc = width > 1
      ? static_cast<uint32_t>(FFloat::Reciprocal(FFloat::FromInt(width)).ToQ(31)) << 1
      : 0xffffffffu;
  g->envelope_increment = env_inc;
  g->k = 0;
  g->num_samples = static_cast<int32_t>(0xffffffffu / env_inc);
  g->k_peak = static_cast<int32_t>((0x80000000u + env_inc - 1) / env_inc);
  g->k_clamp_on = g->k_clamp_off = 0;
  if (window_shape >= 32768) {
    g->smoothness = window_shape >= 65536 ? kQ31One : (window_shape - 32768) << 16;
    g->slope = 0;
  } else {
    g->smoothness = 0;
    g->slope = (kHalf * FFloat::Reciprocal(Q16ToFFloat(window_shape) + k001)).ToQ(24);
    // The slope clamps the triangle at 1 / slope (as tri, Q31): samples
    // where it starts and stops (only chord boundaries, so an off-by-one
    // costs nothing).
    const uint32_t threshold = static_cast<uint32_t>(
        FFloat::Reciprocal(FFloat::FromQ(g->slope, 24)).ToQ(31));
    if (threshold < 0x80000000u) {
      g->k_clamp_on = static_cast<int32_t>((threshold + env_inc - 1) / env_inc);
      g->k_clamp_off = static_cast<int32_t>((0u - threshold) / env_inc) + 1;
    }
  }
  // The kernels' pan gains, Q14.
  if (num_channels_ == 1) {
    g->gains_a = ((gain_l >> 1) + (gain_r >> 1)) >> 17;
    g->gains_b = 0;
  } else {
    g->gains_a = (gain_l >> 17) | ((gain_r >> 17) << 16);
    g->gains_b = ((kQ31One - gain_r) >> 17) | (((kQ31One - gain_l) >> 17) << 16);
  }
  g->quality = quality;
  g->active = true;

  // ONE_POLE(grain_size_hint, grain_size, 0.1).
  grain_size_hint_ += MulQ31(grain_size.ToQ(16) - grain_size_hint_, Q(0.1, 31));
}

int32_t GranularPlayer::Envelope(const Grain* g, int32_t k) {
  const uint32_t phase = static_cast<uint32_t>(k) * g->envelope_increment;
  int32_t gain = Triangle(phase);
  if (g->smoothness) {
    if (g->quality == GRAIN_QUALITY_HIGH) {
      // Interpolate(lut_window, gain, 4096): 12 index bits of the Q31.
      const int32_t i = gain >> 19;
      const int32_t f = (gain & 0x7ffff) << 12;
      const int32_t window = kWindowQ30[i] + MulQ31Fast(kWindowQ30[i + 1] - kWindowQ30[i], f);
      gain += MulQ31Fast(ShlSat(window, 1) - gain, g->smoothness);
    }
  } else if (g->quality >= GRAIN_QUALITY_MEDIUM) {
    // gain x slope (Q24), at most 1.
    gain = ShlSat(MulQ31(gain, g->slope), 7);
  }
  return gain;
}

namespace {

// 1 / n, Q31 (n = 1: just under 1).
constexpr int32_t Recip(int n) { return n ? static_cast<int32_t>(2147483647.0 / n) : 0; }
#define OP1CLOUDS_R4(n) Recip(n), Recip(n + 1), Recip(n + 2), Recip(n + 3)
const int32_t kRecipQ31[kBlockSize + 4] = {
  OP1CLOUDS_R4(0), OP1CLOUDS_R4(4), OP1CLOUDS_R4(8), OP1CLOUDS_R4(12),
  OP1CLOUDS_R4(16), OP1CLOUDS_R4(20), OP1CLOUDS_R4(24), OP1CLOUDS_R4(28), OP1CLOUDS_R4(32),
};
#undef OP1CLOUDS_R4

}  // namespace

// The grain in runs over which the kernels apply: the envelope a linear
// ramp between its exact values at the run's ends (runs end at the
// envelope's kinks, so the triangle and the slope are exact; the window
// table's curve is a chord per block, within ~1e-3), and no buffer wrap.
void GranularPlayer::OverlapAdd(Grain* g, const RecordBuffer* buffer, int32_t* out_l,
                                int32_t* out_r, int32_t size) {
  if (!g->active) return;
  // The pre-delay starts grains at any sample of a block.
  const int32_t skip = g->pre_delay < size ? g->pre_delay : size;
  g->pre_delay -= skip;
  out_l += skip;
  if (out_r) out_r += skip;
  size -= skip;
  int32_t n = g->num_samples - g->k;
  if (n > size) n = size;

  const int16_t* samples = buffer[0].samples();
  const int32_t buffer_size = buffer[0].size();
  GrainKernel kernel;
  kernel.r_offset = out_r ? static_cast<int32_t>(
      reinterpret_cast<const uint8_t*>(buffer[1].samples()) -
      reinterpret_cast<const uint8_t*>(samples)) : 0;
  kernel.increment = g->phase_increment;
  kernel.gains_a = g->gains_a;
  kernel.gains_b = g->gains_b;
  const bool hermite = g->quality == GRAIN_QUALITY_HIGH;
  const int32_t increment = g->phase_increment;
  int32_t done = 0;
  while (done < n) {
    int32_t len = n - done;
    const int32_t k0 = g->k;
    if (g->k_peak > k0 && g->k_peak - k0 < len) len = g->k_peak - k0;
    if (g->k_clamp_on > k0 && g->k_clamp_on - k0 < len) len = g->k_clamp_on - k0;
    if (g->k_clamp_off > k0 && g->k_clamp_off - k0 < len) len = g->k_clamp_off - k0;
    // The buffer wraps (once per grain at most): stop the run before it.
    if (g->first_sample + ((g->phase + (len - 1) * increment) >> 16) >= buffer_size) {
      const int64_t limit = (static_cast<int64_t>(buffer_size - g->first_sample) << 16) - g->phase;
      const int32_t before = limit <= 0 ? 0
          : static_cast<int32_t>((limit + increment - 1) / increment);
      if (before == 0) {
        g->first_sample -= buffer_size;
        continue;
      }
      if (before < len) len = before;
    }
    const int32_t env_0 = Envelope(g, k0);
    const int32_t env_1 = Envelope(g, k0 + len);
    kernel.base = samples + g->first_sample;
    kernel.phase = g->phase;
    kernel.env = env_0;
    kernel.env_step = len > 1 ? MulQ31(env_1 - env_0, kRecipQ31[len]) : 0;
    if (out_r) {
      GrainStereo(hermite, &kernel, out_l + done, out_r + done, len);
    } else {
      GrainMono(hermite, &kernel, out_l + done, len);
    }
    g->phase = kernel.phase;
    g->k += len;
    done += len;
  }
  if (g->k >= g->num_samples) g->active = false;
}

}  // namespace op1clouds
