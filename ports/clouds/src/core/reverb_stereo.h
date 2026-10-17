// Clouds' reverb (clouds/dsp/fx/reverb.h on FxEngine<16384, FORMAT_16_BIT>)
// in fixed point, stereo: fed with left + right, mixed back into each side
// (side += (wet_side - side) * amount), as upstream.
//
// Upstream's topology and order of operations per sample: the smear of AP1
// by LFO 1, 4 input allpasses, then two branches of 2 allpasses + a delay,
// the first reading del2 modulated by LFO 2. Delay lengths and offsets are
// upstream's at 32 kHz scaled to the build's rate (reverb_layout.h).
//
// The per-sample loop is a run kernel in C (ReverbRunC) and in Blackfin
// assembly (reverb_stereo_bfin.S) with the same arithmetic, the Blackfin's
// 16-bit MACs (as the grain kernels, grain_kernel.h):
//   the accumulator keeps full precision between stages; what's written to
//   the 16-bit buffer is its high half, truncated and saturated; upstream's
//   "acc *= scale" after a write scales that written value;
//   the low-pass states are 16-bit; the input is (l + r) as Q15, saturated;
//   wet = 2 acc; out = dry + (wet - dry) amount, 32 x 16 bits (Q27).
// Coefficients are Q15.
//
// The buffer is indexed without wrapping (as Rings' fixed reverb): every
// access is a fixed offset from the write pointer p in [0, RV_SIZE); reads
// go up to RV_MAX_REACH past it; writes made while p < RV_MAX_REACH are
// mirrored RV_SIZE further on, where they're read after p wraps. Runs end
// where the LFOs step (every 32 samples, FxEngine::Start) and where the
// mirroring starts.

#ifndef OP1CLOUDS_CORE_REVERB_STEREO_H_
#define OP1CLOUDS_CORE_REVERB_STEREO_H_

#include <cstddef>
#include <cstdint>

#include "src/core/config.h"
#include "src/core/reverb_layout.h"

namespace op1clouds {

// Layout shared with the assembly (reverb_layout.h's RVK_ offsets).
struct ReverbKernel {
  int32_t gain_amount;   // 0: input gain (low) | amount (high), Q15
  int32_t kap;           // 4: kap (low) | -kap (high)
  int32_t krt_klp;       // 8: reverb time (low) | low-pass (high)
  int32_t lp;            // 12: lp_1 (low) | lp_2 (high), states
  int32_t smear_off;     // 16: bytes from p to AP1's smear tap
  int32_t smear_w;       // 20: 32767 - f (low) | f (high)
  int32_t d2_off;        // 24: bytes from p to del2's modulated tap
  int32_t d2_w;          // 28
};

// p: the buffer at the write pointer; each sample first moves it down one.
// Every write also goes alias_bytes further on (0: the same place again).
extern "C" void ReverbRunAsm(ReverbKernel* k, int32_t* l, int32_t* r, int16_t* p, int32_t n,
                             int32_t alias_bytes);
void ReverbRunC(ReverbKernel* k, int32_t* l, int32_t* r, int16_t* p, int32_t n,
                int32_t alias_bytes);

class ReverbStereo {
 public:
  static const int32_t kSize = RV_SIZE;

  void Init();
  void Clear();

  // Upstream's parameters, Q31 in [0, 1).
  void set_amount(int32_t amount) { amount_ = amount; }
  void set_input_gain(int32_t gain) { input_gain_ = gain; }
  void set_time(int32_t time) { time_ = time; }
  void set_diffusion(int32_t diffusion) { diffusion_ = diffusion; }
  void set_lp(int32_t lp) { lp_ = lp; }

  // l, r: Q27 samples, replaced by the mix. r == nullptr: mono, `l` holds
  // the mono signal; the reverb is fed 2 l (upstream's l + r with l == r)
  // and l gets the mean of the two sides' mixes. size <= kBlockSize in
  // mono.
  void Process(int32_t* l, int32_t* r, size_t size);

  // Tests: the C kernel even on Blackfin.
  void set_use_asm(bool use_asm) { use_asm_ = use_asm; }
  int32_t write_ptr() const { return write_ptr_; }

 private:
  int32_t amount_;
  int32_t input_gain_;
  int32_t time_;
  int32_t diffusion_;
  int32_t lp_;
  int32_t lp_state_;       // lp_1 | lp_2
  int32_t write_ptr_;      // [1, kSize]: the next sample is at write_ptr - 1
  uint32_t lfo_phase_[2];
  int32_t lfo_value_[2];   // Q31, [0, 1]
  bool use_asm_;
  int32_t scratch_[kBlockSize];
  int16_t buffer_[RV_BUFFER_WORDS];
};

}  // namespace op1clouds

#endif  // OP1CLOUDS_CORE_REVERB_STEREO_H_
