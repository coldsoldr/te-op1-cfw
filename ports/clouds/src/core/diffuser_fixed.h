// Clouds' diffuser (clouds/dsp/fx/diffuser.h: 4 allpasses per side,
// coefficient 0.625, on FxEngine<2048, FORMAT_32_BIT>) in fixed point:
// upstream's order of operations, its delay layout scaled to the build's
// rate (diffuser_layout.h), a 16-bit buffer. Mono runs the left network
// only.
//
// The per-sample loop is a run kernel in C (DiffuserRunC) and in Blackfin
// assembly (diffuser_bfin.S) with the same arithmetic, the 16-bit MACs as
// the reverb's (reverb_stereo.h): the network works in Q14 (6 dB of
// headroom over full scale: dense grains can exceed it), the accumulator
// at full precision between stages, the buffer stores its high half;
// out = dry + (wet - dry) amount at 32 x 16 bits, so the dry signal keeps
// its precision.
//
// The buffer is indexed without wrapping, with mirrored writes near the
// wrap, as the reverb's.

#ifndef OP1CLOUDS_CORE_DIFFUSER_FIXED_H_
#define OP1CLOUDS_CORE_DIFFUSER_FIXED_H_

#include <cstddef>
#include <cstdint>

#include "src/core/config.h"
#include "src/core/diffuser_layout.h"

namespace op1clouds {

struct DiffuserKernel {
  int32_t kap;      // 0: kap (low) | -kap (high), Q15
  int32_t amount;   // 4: Q15 (low)
};

// p: the buffer at the write pointer; each sample first moves it down one.
// r == nullptr: mono (left network).
extern "C" void DiffuserRunStereoAsm(const DiffuserKernel* k, int32_t* l, int32_t* r, int16_t* p,
                                     int32_t n, int32_t alias_bytes);
extern "C" void DiffuserRunMonoAsm(const DiffuserKernel* k, int32_t* l, int16_t* p, int32_t n,
                                   int32_t alias_bytes);
void DiffuserRunC(const DiffuserKernel* k, int32_t* l, int32_t* r, int16_t* p, int32_t n,
                  int32_t alias_bytes);

class DiffuserFixed {
 public:
  static const int32_t kSize = DF_SIZE;

  void Init(bool clear_buffers = true);
  void Clear(bool clear_buffer = true);

  // Q31.
  void set_amount(int32_t amount) { amount_ = amount; }
  int32_t amount() const { return amount_; }

  // r == nullptr: mono (left network only).
  void Process(int32_t* l, int32_t* r, size_t size);

  // Tests: the C kernel even on Blackfin.
  void set_use_asm(bool use_asm) { use_asm_ = use_asm; }

 private:
  int32_t amount_;
  int32_t write_ptr_;   // [1, kSize]
  bool use_asm_;
  int16_t buffer_[DF_BUFFER_WORDS];
};

}  // namespace op1clouds

#endif  // OP1CLOUDS_CORE_DIFFUSER_FIXED_H_
