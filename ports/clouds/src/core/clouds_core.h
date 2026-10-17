// The Clouds processor (clouds/dsp/granular_processor.cc) in fixed point,
// for the OP-1: one instance per effect position, mono (the synth effect)
// or stereo (the master effect), with all its memory inside the object.
//
// Per 32-sample block, upstream's Process: feedback (high-passed, soft
// limited) into the input, the record buffer, the mode's player, the
// diffuser, the pitch shifter (looping delay), the texture filters (looping
// delay, time stretch), the reverb and the dry/wet crossfade. Upstream's
// Prepare (a mode change: a block of silence, filters reset) is folded in.
//
// Modes so far: granular and looping delay. Time stretch and spectral
// output silence until their players are written.
//
// int16 in and out; any size that is a multiple of 32 (the OP-1 calls
// effects with 64).

#ifndef OP1CLOUDS_CORE_CLOUDS_CORE_H_
#define OP1CLOUDS_CORE_CLOUDS_CORE_H_

#include <cstdint>

#include "src/core/config.h"
#include "src/core/diffuser_fixed.h"
#include "src/core/dsp_fixed.h"
#include "src/core/granular_player.h"
#include "src/core/looping_player.h"
#include "src/core/params.h"
#include "src/core/pitch_shifter_fixed.h"
#include "src/core/record_buffer.h"
#include "src/core/reverb_stereo.h"
#include "src/core/svf_fixed.h"

namespace op1clouds {

class CloudsCore {
 public:
  // Record memory: the larger of mono's one buffer and stereo's two.
  static const int32_t kRecordPoolWords =
      kMonoRecordWords > 2 * kStereoRecordWords ? kMonoRecordWords : 2 * kStereoRecordWords;

  void Init(int32_t num_channels);

  CloudsParams* mutable_parameters() { return &params_; }
  const CloudsParams& parameters() const { return params_; }
  void set_playback_mode(int32_t mode) { mode_ = mode; }
  int32_t playback_mode() const { return mode_; }
  int32_t num_channels() const { return num_channels_; }
  GranularPlayer* granular() { return &granular_; }

  // size: a multiple of kBlockSize.
  void ProcessStereo(int16_t* l, int16_t* r, int32_t size);
  void ProcessMono(int16_t* in_out, int32_t size);

 private:
  void ProcessBlock(int16_t* l, int16_t* r);
  void ResetFilters();

  int32_t num_channels_;
  int32_t mode_;
  int32_t previous_mode_;
  CloudsParams params_;

  int32_t freeze_lp_;   // Q31
  int32_t dry_wet_;     // Q16
  // Stages skipped while their amount is 0 (restarted from rest after).
  bool fb_on_;
  bool diffuser_on_;
  bool reverb_on_;

  int32_t in_[2][kBlockSize];
  int32_t out_[2][kBlockSize];
  int32_t fb_[2][kBlockSize];

  SvfFixed fb_filter_[2];
  SvfFixed lp_filter_[2];
  SvfFixed hp_filter_[2];

  RecordBuffer buffer_[2];
  LoopingPlayer looper_;
  GranularPlayer granular_;
  Lcg random_;
  DiffuserFixed diffuser_;
  PitchShifterFixed pitch_shifter_;
  ReverbStereo reverb_;

  int16_t record_[kRecordPoolWords];
  int16_t tail_[2][kCrossFadeSize + 1];
  int16_t pitch_shifter_buffer_[PitchShifterFixed::kSize];
};

}  // namespace op1clouds

#endif  // OP1CLOUDS_CORE_CLOUDS_CORE_H_
