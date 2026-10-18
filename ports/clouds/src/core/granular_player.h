// Clouds' granular player (clouds/dsp/granular_sample_player.h, grain.h) in
// fixed point.
//
// Scheduling follows upstream exactly, including its random sequence (one
// LCG word per sample for the probabilistic seed, whether or not it's used,
// and one per grain for the pan), so grains start where upstream's do.
// Grain setup (sizes, start, pan, envelope slope) runs per grain in FFloat;
// the per-sample work is the overlap-add kernels.
//
// Formats: audio Q27; grain envelope phase a 32-bit fraction of [0, 2)
// (2.0 = 2^32); envelope gains Q31; the buffer play-head Q16 samples
// (upstream's phase); sizes and the size hint Q16 samples.
//
// The grain count is upstream's (32 stereo, 40 mono) unless set lower with
// set_max_grains (the OP-1's CPU budget); a lower cap only changes how many
// grains can overlap.

#ifndef OP1CLOUDS_CORE_GRANULAR_PLAYER_H_
#define OP1CLOUDS_CORE_GRANULAR_PLAYER_H_

#include <cstdint>

#include "src/core/config.h"
#include "src/core/dsp_fixed.h"
#include "src/core/params.h"
#include "src/core/record_buffer.h"

namespace op1clouds {

enum GrainQuality { GRAIN_QUALITY_LOW, GRAIN_QUALITY_MEDIUM, GRAIN_QUALITY_HIGH };

// A grain's envelope runs over samples k = 0 .. num_samples - 1 with phase
// k x envelope_increment (a 32-bit fraction of upstream's [0, 2)); upstream
// ends the grain at the first sample whose next phase would reach 2.
struct Grain {
  int32_t first_sample;       // may go negative after the buffer wrap
  int32_t phase;              // Q16 samples from first_sample
  int32_t phase_increment;    // Q16
  int32_t pre_delay;
  uint32_t envelope_increment;
  int32_t k;                  // samples played
  int32_t num_samples;
  // Where the envelope isn't linear: its peak, and the slope's clamp.
  int32_t k_peak;
  int32_t k_clamp_on;
  int32_t k_clamp_off;
  int32_t smoothness;         // Q31, 0: no window table
  int32_t slope;              // Q24
  int32_t gains_a;            // the kernel's pan gains (grain_kernel.h)
  int32_t gains_b;
  int32_t quality;
  bool active;
};

// What Play derives from density, texture and the grain size hint
// (software-float divisions), kept while they don't change.
struct GrainRate {
  int32_t density, texture, hint, max_grains;   // the inputs
  FFloat overlap;
  int32_t window_shape_q16;
  uint32_t threshold;
  bool probabilistic;
  bool use_deterministic_seed;
  int32_t space_q16;
  int32_t target_q16;
  FFloat window_factor;                         // 1 + (window gain - 1) overlap
};

// What Schedule derives from size, pitch and position (per grain
// otherwise), kept while they don't change.
struct GrainGeometry {
  int32_t size, pitch, position, buffer_size;   // the inputs
  int32_t width;
  int32_t increment_q16;
  int32_t start_offset;                         // Trunc(position available + eaten)
};

// What Schedule derives from the grain's width and the window shape.
struct GrainEnvelope {
  int32_t width, window_shape;                  // the inputs
  uint32_t envelope_increment;
  int32_t num_samples, k_peak;
  int32_t smoothness, slope;
  int32_t k_clamp_on, k_clamp_off;
};

class GranularPlayer {
 public:
  void Init(int32_t num_channels, int32_t max_num_grains);
  void set_max_grains(int32_t n);
  int32_t active_grains() const { return active_grains_; }
  // The overlap-add's cycles since the last Take (for the governor: the
  // part of the cost that scales with the grain count), when a clock is set.
  void set_clock(uint32_t (*clock)()) { clock_ = clock; }
  uint32_t TakeOverlapCycles() {
    const uint32_t c = overlap_cycles_;
    overlap_cycles_ = 0;
    return c;
  }

  // Granular parameters, derived from density and texture per block (as
  // upstream's GranularProcessor does).
  void Play(const RecordBuffer* buffer, const CloudsParams& p, Lcg* random,
            int32_t* out_l, int32_t* out_r, int32_t size);

 private:
  void Schedule(Grain* g, const CloudsParams& p, int32_t window_shape, Lcg* random,
                int32_t pre_delay, int32_t buffer_size, int32_t buffer_head, int32_t quality);
  // Upstream's envelope at sample k of the grain, Q31.
  static int32_t Envelope(const Grain* g, int32_t k);
  void OverlapAdd(Grain* g, const RecordBuffer* buffer, int32_t* out_l, int32_t* out_r,
                  int32_t size);

  int32_t num_channels_;
  int32_t max_num_grains_;
  int32_t cap_;               // set_max_grains, <= max_num_grains_
  int32_t num_midfi_grains_;
  int32_t num_grains_;        // smoothed active count, Q16
  int32_t gain_normalization_;  // Q30
  int32_t grain_size_hint_;   // Q16 samples
  int32_t grain_rate_phasor_; // Q16 samples
  int32_t active_grains_;
  Grain grains_[kMaxNumGrains];
  int32_t available_[kMaxNumGrains];
  // The derived values, recomputed only when their inputs change (the
  // software-float work was most of the player's code on the device).
  GrainRate rate_;
  GrainGeometry geometry_;
  int32_t geometry_hint_target_;   // the grain size, Q16, for the hint's one-pole
  GrainEnvelope envelope_;
  int32_t sqrt_gain_key_;     // num_grains_ the gain below was computed for
  FFloat sqrt_gain_;
  uint32_t (*clock_)();
  uint32_t overlap_cycles_;
};

}  // namespace op1clouds

#endif  // OP1CLOUDS_CORE_GRANULAR_PLAYER_H_
