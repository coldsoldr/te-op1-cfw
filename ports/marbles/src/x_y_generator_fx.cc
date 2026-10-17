// Marbles' X/Y generator in fixed point. See x_y_generator_fx.h.
//
// Based on Mutable Instruments Marbles (MIT, (c) 2015 Emilie Gillet).

#include "src/x_y_generator_fx.h"

namespace op1marbles {
namespace fx {

void XYGenerator::Init(RandomStream* random_stream) {
  for (int i = 0; i < kNumChannels; ++i) {
    random_sequence_[i].Init(random_stream);
    output_channel_[i].Init();
    use_shifted_sequences_[i] = false;
  }
  ramp_divider_.Init();
}

static const uint32_t hashes[kNumXChannels] = {
  0, 0xbeca55e5, 0xf0cacc1a
};

void XYGenerator::Process(ClockSource clock_source, const GroupSettings& x_settings,
                          const GroupSettings& y_settings, bool* reset,
                          const Ramps& ramps, int32_t* output, int size) {
  const uint32_t* channel_ramp[kNumChannels];
  switch (clock_source) {
    case CLOCK_SOURCE_INTERNAL_T1:
      channel_ramp[0] = channel_ramp[1] = channel_ramp[2] = ramps.slave[0];
      break;
    case CLOCK_SOURCE_INTERNAL_T2:
      channel_ramp[0] = channel_ramp[1] = channel_ramp[2] = ramps.master;
      break;
    case CLOCK_SOURCE_INTERNAL_T3:
      channel_ramp[0] = channel_ramp[1] = channel_ramp[2] = ramps.slave[1];
      break;
    default:
      channel_ramp[0] = ramps.slave[0];
      channel_ramp[1] = ramps.master;
      channel_ramp[2] = ramps.slave[1];
      break;
  }

  if (*reset) {
    ramp_divider_.Reset();
  }
  ramp_divider_.Process(y_settings.ratio, channel_ramp[1], ramps.external, size);
  channel_ramp[kNumChannels - 1] = ramps.external;

  for (int i = 0; i < kNumChannels; ++i) {
    OutputChannel& channel = output_channel_[i];
    const GroupSettings& settings = i < kNumXChannels ? x_settings : y_settings;

    ScaleOffset so;
    switch (settings.voltage_range) {
      case VOLTAGE_RANGE_NARROW: so.scale = 2; so.offset = 0; break;
      case VOLTAGE_RANGE_POSITIVE: so.scale = 5; so.offset = 0; break;
      default: so.scale = 10; so.offset = -5; break;
    }
    channel.set_scale_offset(so);

    int amount = 1;  // -1, 0 or 1: the products below are exact
    if (settings.control_mode == CONTROL_MODE_BUMP) {
      amount = i == kNumXChannels / 2 ? 1 : -1;
    } else if (settings.control_mode == CONTROL_MODE_TILT) {
      amount = i - 1;
    }
    const int32_t half = kQ16One / 2;
    channel.set_spread(half + (settings.spread - half) * amount);
    channel.set_bias(half + (settings.bias - half) * amount);
    channel.set_steps(half + (settings.steps - half) * amount);
    channel.set_scale_index(settings.scale_index);

    RandomSequence* sequence = &random_sequence_[i];
    sequence->Record();
    sequence->set_length(settings.length);
    sequence->set_deja_vu(settings.deja_vu);
    if (*reset) {
      sequence->Reset();
    }

    bool use_shifted_sequences = false;
    // When all channels follow the same clock, X2 and X3 replay X1's
    // values, hashed (non-register mode).
    if (clock_source != CLOCK_SOURCE_INTERNAL_T1_T2_T3
        && i > 0 && i < kNumXChannels) {
      sequence = &random_sequence_[0];
      sequence->ReplayPseudoRandom(hashes[i]);
    }
    if (!use_shifted_sequences && use_shifted_sequences_[i]) {
      sequence->Clone(random_sequence_[0]);
    }
    use_shifted_sequences_[i] = use_shifted_sequences;

    channel.Process(sequence, channel_ramp[i], &output[i], size, kNumChannels);
  }
}

}  // namespace fx
}  // namespace op1marbles
