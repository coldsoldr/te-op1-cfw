// Stage timing for the device's CPU probe (ports/rings/op1/cpu_probe.cc):
// with OP1CLOUDS_PROFILE the core adds each stage's cycles to
// profile_cycles[], read by the probe's readout. Without it (every other
// build) the macros are empty and the core is unchanged.

#ifndef OP1CLOUDS_CORE_PROFILE_H_
#define OP1CLOUDS_CORE_PROFILE_H_

#include <cstdint>

namespace op1clouds {

enum ProfileStage {
  PROFILE_RECORD,        // feedback filters, feedback mix, the record write
  PROFILE_PLAYER,        // the player (looper, or granular: scheduling and overlap)
  PROFILE_OVERLAP,       // of the granular player: the overlap-add (grain kernels)
  PROFILE_DIFFUSE,       // diffuser, pitch shifter, texture filters, feedback copy
  PROFILE_REVERB,
  PROFILE_RESAMPLE_MIX,  // op1/clouds_fx.cc: decimator, interpolator, dry/wet mix
  PROFILE_NUM_STAGES
};

#ifdef OP1CLOUDS_PROFILE
extern uint32_t (*profile_clock)();
extern uint32_t profile_cycles[PROFILE_NUM_STAGES];
extern uint32_t profile_instance_cycles[2];   // [0] mono (synth), [1] stereo (master)
extern int32_t profile_grains[2];             // active grains, last call
extern int32_t profile_cap[2];                // the governor's cap, last call
extern int32_t profile_late[2];               // the governor's late calls so far
extern uint32_t profile_interval[2];          // the governor's mean call interval, permille of nominal
inline uint32_t ProfileNow() { return profile_clock ? profile_clock() : 0; }
#define OP1CLOUDS_PROFILE_MARK(var) uint32_t var = ::op1clouds::ProfileNow()
#define OP1CLOUDS_PROFILE_RESET(var) var = ::op1clouds::ProfileNow()
#define OP1CLOUDS_PROFILE_ADD(stage, var)                              \
  do {                                                                 \
    const uint32_t now_ = ::op1clouds::ProfileNow();                   \
    ::op1clouds::profile_cycles[::op1clouds::stage] += now_ - var;     \
    var = now_;                                                        \
  } while (0)
#else
#define OP1CLOUDS_PROFILE_MARK(var) do { } while (0)
#define OP1CLOUDS_PROFILE_RESET(var) do { } while (0)
#define OP1CLOUDS_PROFILE_ADD(stage, var) do { } while (0)
#endif

}  // namespace op1clouds

#endif  // OP1CLOUDS_CORE_PROFILE_H_
