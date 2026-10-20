// SHIFT: a Bode frequency shifter with barber-pole feedback and stereo
// spread, and a ring modulator (docs/fx-briefs/shift.md, docs/shift-port.md),
// in fixed point at the effect rate (44.1 kHz, up to 64 samples a call).
//
// Signal path (per sample):
//   x = (l + r) / 2 (stereo) or l (mono), x 2^10 in 32 bits
//   -> Hilbert pair: two chains of 4 allpasses in z^-2 (Niemitalo's
//      polyphase IIR pair, as Warps' quadrature_transform.h), 32-bit data,
//      Q15 coefficients; the Q chain has an extra z^-1. I + jQ is the
//      analytic signal (> 49 dB sideband rejection, 50 Hz-18 kHz)
//   -> int16 I, Q; Q x m (m 1 = frequency shift, 0 = ring modulation,
//      ramped over 256 samples on a mode change)
//   per channel (L: shift + spread, R: shift - spread):
//   -> a = (I, mQ) + g fb, saturated (g = feedback; fb = the loop's
//      complex output 256 samples ago)
//   -> w = a e^(j phase) (512-entry sin/cos table, interpolated)
//   -> out = Re w (m 1: one sideband; m 0: a cos phase, both sidebands)
//   -> fb line <- low-pass(w) (one pole, 0.6)
//   -> dry/wet mix.
// The feedback runs in the complex (analytic) domain, so one Hilbert pair
// serves both channels and each still has its own barber-pole loop.
//
// The two kernels (Hilbert, channel) have C references (ShiftHilbertC,
// ShiftChannelC) and Blackfin versions (shift_bfin.S) that match them bit
// for bit (tests/test_shift.cc).

#ifndef OP1FX_SRC_SHIFT_SHIFT_H_
#define OP1FX_SRC_SHIFT_SHIFT_H_

#include <cstddef>
#include <cstdint>

#include "src/shift/shift_layout.h"
#include "src/shift/shift_tables.h"

namespace op1fx {

struct ShiftParams {
  int32_t shift;      // 0..32767, 16384 = 0 Hz: -2 kHz .. +2 kHz, cubic
  int32_t feedback;   // 0..32767 -> loop gain 0..0.93
  int32_t spread;     // 0..32767: L at shift + spread, R at shift - spread
  int32_t mix;        // 0..32767 dry -> wet
  bool ring;          // ring modulation (carrier |shift|) instead of shifting
};

struct ShiftHilbertKernel {
  int32_t xh[3];      // x[n-1], x[n-2], x[n-3]
  int32_t y[8][2];    // per section: y[n-1], y[n-2]
  int32_t m, dm;      // Q gain, Q15, 32768 = 1 (shift), 0 = ring; its step
  int16_t coef[8];
};

struct ShiftChannelKernel {
  uint32_t phase;
  int32_t inc, dinc;
  int32_t g, dg;      // feedback, Q15, and its step per sample
  int32_t mix, dmix;  // mix, Q15, and its step
  uint32_t lp;
  int32_t pos;        // bytes
  const uint32_t* osc;   // kShiftOsc
  uint32_t delay[SH_DELAY];
};

static_assert(offsetof(ShiftHilbertKernel, y) == SHK_Y, "layout");
static_assert(offsetof(ShiftHilbertKernel, m) == SHK_M, "layout");
static_assert(offsetof(ShiftHilbertKernel, dm) == SHK_DM, "layout");
static_assert(offsetof(ShiftHilbertKernel, coef) == SHK_COEF, "layout");
static_assert(sizeof(ShiftHilbertKernel) == SHK_SIZE, "layout");
static_assert(offsetof(ShiftChannelKernel, inc) == SCK_INC, "layout");
static_assert(offsetof(ShiftChannelKernel, dinc) == SCK_DINC, "layout");
static_assert(offsetof(ShiftChannelKernel, g) == SCK_G, "layout");
static_assert(offsetof(ShiftChannelKernel, dg) == SCK_DG, "layout");
static_assert(offsetof(ShiftChannelKernel, mix) == SCK_MIX, "layout");
static_assert(offsetof(ShiftChannelKernel, dmix) == SCK_DMIX, "layout");
static_assert(offsetof(ShiftChannelKernel, lp) == SCK_LP, "layout");
static_assert(offsetof(ShiftChannelKernel, pos) == SCK_POS, "layout");
#ifdef __bfin__   // (a pointer is 8 bytes on the host; the layout is for the asm)
static_assert(offsetof(ShiftChannelKernel, osc) == SCK_OSC, "layout");
static_assert(offsetof(ShiftChannelKernel, delay) == SCK_DELAY, "layout");
static_assert(sizeof(ShiftChannelKernel) == SCK_SIZE, "layout");
#endif

// Hilbert kernel: l (and r, or null) -> iq[i] = I (low half) | mQ (high
// half), mQ = Q when m = 32768 and dm = 0, else Q min(m, 32767) >> 15
// (m stepping by dm per sample). buf: SH_SCRATCH int32 scratch. n even,
// 2..64.
void ShiftHilbertC(ShiftHilbertKernel* k, const int16_t* l, const int16_t* r, uint32_t* iq,
                   int32_t* buf, int32_t n);
// Channel kernel: iq -> io (in place over the dry signal). write 0: io is
// only read (mix 0 throughout the call), the loop still runs. scratch:
// SH_MAX_BLOCK words (the assembly's; may be the Hilbert kernel's buf).
void ShiftChannelC(ShiftChannelKernel* k, const uint32_t* iq, int16_t* io, int32_t n,
                   int32_t write, uint32_t* scratch);

#ifdef __bfin__
extern "C" void ShiftHilbertAsm(ShiftHilbertKernel* k, const int16_t* l, const int16_t* r,
                                uint32_t* iq, int32_t* buf, int32_t n);
extern "C" void ShiftChannelAsm(ShiftChannelKernel* k, const uint32_t* iq, int16_t* io,
                                int32_t n, int32_t write, uint32_t* scratch);
#endif

// Control mappings (also used by the tests).
// Phase step for a signed knob position d (Q15, -32768..32767):
// 2000 Hz x (d / 32768)^3, as 2^32 / 44100 per Hz.
int32_t ShiftIncrement(int32_t d);
// Knob 0..32767 -> d: 2 (knob - 16384), so 0 -> -32768, 16384 -> 0.
inline int32_t ShiftKnobToD(int32_t knob) { return 2 * (knob - 16384); }
// Spread 0..32767 -> the offset in d: a third of the range at the top.
inline int32_t ShiftSpreadToD(int32_t spread) { return (spread * 10923) >> 15; }   // (no division)

class FreqShift {
 public:
  void Init();
  // In place; r null = mono (the left channel's shifter only). n even 2..64.
  void Process(int16_t* l, int16_t* r, int32_t n, const ShiftParams& p);

#ifdef __bfin__
  void set_use_asm(bool use_asm) { use_asm_ = use_asm; }
#endif
  const ShiftHilbertKernel& hilbert() const { return hk_; }
  const ShiftChannelKernel& channel(int i) const { return ck_[i]; }

 private:

  ShiftHilbertKernel hk_;
  ShiftChannelKernel ck_[2];
  int32_t buf_[SH_SCRATCH];
  uint32_t iq_[SH_MAX_BLOCK];
  int32_t d_[2], inc_[2];   // the last positions and their phase steps
  bool use_asm_;
};

}  // namespace op1fx

#endif  // OP1FX_SRC_SHIFT_SHIFT_H_
