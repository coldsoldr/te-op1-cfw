// PLATE: Dattorro's figure-of-eight plate reverb ("Effect Design Part 1",
// JAES 1997) in fixed point, at 22050 Hz (half the OP-1's rate: the glue
// decimates its input 2:1 and interpolates the wet signal back, as
// op1/clouds_fx.cc does for Clouds). Wet only. docs/plate-port.md has the
// design, the formats and the measurements.
//
// Signal path per sample (mono sum in, stereo out):
//   pre-delay (0..200 ms, its own ring, gliding read tap)
//   -> low cut (one-pole high-pass, 20..500 Hz) -> input gain (0.25; the
//      tank's headroom, made up at the output as Clouds' reverb does;
//      ramps to 0 on freeze) -> 4 input allpasses (diffusion)
//   -> tank, two halves in a figure of eight; each half: + decay x the
//      other half's end -> modulated allpass (-dd1) -> delay -> damping
//      low-pass -> x decay -> allpass (dd2) -> delay
//   -> 7 + 7 output taps (Dattorro's table), x 0.6, x 2 (the makeup: the
//      wet is half Dattorro's level, as Clouds' reverb's 0.2 in, x 2 out).
//
// Arithmetic: the Blackfin's 16-bit MACs, as Clouds' reverb kernel: int16
// buffers; a 40-bit accumulator between stages; everything written to the
// buffers is the accumulator's high half, rounded (the MAC's default,
// unbiased rounding: no DC creeps into a frozen tank) and saturated.
// Coefficients Q15. The run kernels have C references (PlateInputRunC,
// PlateTankRunC, PlateXfadeRunC) and Blackfin versions (plate_bfin.S,
// bit-exact; tests/test_plate.cc).
//
// Parameters are mapped once per call (PlateControl, integer tables),
// smoothed per call and ramped per sample where it matters (input gain,
// pre-delay and modulation taps).
//
// Changing `size`: the wet fades out (4 calls); then, the output muted,
// the tank's reads (its six tails and both modulated allpasses) crossfade
// from the old lengths to the new ones over 256 samples (8 calls), so the
// recirculating signal never jumps; then the wet fades back in (8 calls).
// The tail carries on at the new size (a frozen one too).

#ifndef OP1FX_PLATE_PLATE_H_
#define OP1FX_PLATE_PLATE_H_

#include <cstdint>

#include "src/plate/plate_layout.h"

namespace op1fx {

struct PlateParams {
  int32_t decay;       // 0..32767 -> tank gain 0.2..0.98
  int32_t damp;        // 0..32767 bright -> dark
  int32_t predelay;    // 0..32767 -> 0..200 ms
  int32_t size;        // 0..7, tank length 0.6..1.0 (geometric steps)
  int32_t diffusion;   // 0..32767 -> input allpass gains 0..Dattorro's
  int32_t motion;      // 0..32767 -> excursion 0..2x Dattorro's
  int32_t lowcut;      // 0..32767 -> 20..500 Hz (log)
  bool freeze;         // decay 1, damping off, input off: the tail holds
};

const int32_t kPlateMaxBlock = 64;
const int32_t kPlateNumSizes = 8;

// Run kernel blocks (plate_layout.h's PTK_ / PXK_ / PIK_ offsets).
struct PlateTankKernel {
  int32_t decay_klp;
  int32_t dd1;
  int32_t dd2;
  int32_t gout;
  int32_t lp;
  int32_t pos1, step1;
  int32_t pos2, step2;
  int32_t off[PT_NUM_OFF];
};

// A size crossfade: the new size's reads, and the weight of the new one.
struct PlateXfadeKernel {
  int32_t xf, xf_step;              // Q31
  int32_t pos1, step1;              // the new size's modulated delays, Q16
  int32_t pos2, step2;
  int32_t off[6];                   // the new size's tails
  int32_t w, tmp;                   // the assembly's scratch
};

struct PlateInputKernel {
  int32_t g1;
  int32_t g2;
  int32_t khp;
  int32_t gain, gstep;
  int32_t hp;
};

// p: the buffer at the write pointer; each sample first moves it down one.
// Every write also goes alias_bytes further on (0: the same place again).
// n > 0.
extern "C" void PlateInputRunAsm(PlateInputKernel* k, const int16_t* x, int16_t* out, int16_t* p,
                                 int32_t n, int32_t alias_bytes);
extern "C" void PlateTankRunAsm(PlateTankKernel* k, const int16_t* x, int16_t* l, int16_t* r,
                                int16_t* p, int32_t n, int32_t alias_bytes);
extern "C" void PlateTankRunMonoAsm(PlateTankKernel* k, const int16_t* x, int16_t* l,
                                    int16_t* p, int32_t n, int32_t alias_bytes);
extern "C" void PlateXfadeRunAsm(PlateTankKernel* k, PlateXfadeKernel* xk, const int16_t* x,
                                 int16_t* p, int32_t n, int32_t alias_bytes);
// The pre-delay: (l + r) >> 1 into ring[w..w+n) (no wrap: the caller
// splits), and x[i] = the ring read d_i samples back (Q16, d_i = d + i
// step), two taps, weights 32767 - f and f, rounded as the MAC (the read
// wraps round PL_PRE_WORDS). Mono: r = l.
extern "C" void PlatePreRunAsm(const int16_t* l, const int16_t* r, int16_t* ring, int16_t* x,
                               int32_t w, int32_t d, int32_t step, int32_t n);
void PlatePreRunC(const int16_t* l, const int16_t* r, int16_t* ring, int16_t* x, int32_t w,
                  int32_t d, int32_t step, int32_t n);
void PlateInputRunC(PlateInputKernel* k, const int16_t* x, int16_t* out, int16_t* p, int32_t n,
                    int32_t alias_bytes);
// r == nullptr: the left taps only.
void PlateTankRunC(PlateTankKernel* k, const int16_t* x, int16_t* l, int16_t* r, int16_t* p,
                   int32_t n, int32_t alias_bytes);
// The tank with every size-dependent read crossfaded from k's size to xk's;
// no output taps (the output is muted meanwhile).
void PlateXfadeRunC(PlateTankKernel* k, PlateXfadeKernel* xk, const int16_t* x, int16_t* p,
                    int32_t n, int32_t alias_bytes);

// One call's control, from the parameters: the coefficients (Q15), the
// per-sample ramps (start and step), and the tank size. The fixed-point
// plate and the float reference (tests/plate_float_ref.cc) run from the
// same values.
struct PlateCoefs {
  int32_t decay, dd1, dd2, klp;     // Q15
  int32_t g1, g2, khp;              // Q15
  int32_t gain0, gain_step;         // input gain, Q31
  int32_t pre0, pre_step;           // pre-delay, Q16 samples
  int32_t mod0[2], mod_step[2];     // modulated allpass delays, Q16 samples
  int32_t fade0, fade_step;         // wet fade, Q31 (kFadeOne and 0: none)
  int32_t size;                     // 0..7
  // A size crossfade (muted output): to size_new, its modulated delays,
  // the new size's weight (Q31).
  bool xfade;
  int32_t size_new;
  int32_t xmod0[2], xmod_step[2];
  int32_t xf0, xf_step;
};

struct PlateSizeTable {
  int32_t a1, a3;                   // the modulated allpasses' delays
  int32_t tail[6];                  // D4, D1, A2, D2, D3, A4 (PT_ order)
  int32_t tap_l[7], tap_r[7];       // Dattorro's taps, in his order
};
extern const PlateSizeTable kPlateSizes[kPlateNumSizes];
// The sign of each tap (+1 / -1), the same for both sides.
extern const int8_t kPlateTapSign[7];

class PlateControl {
 public:
  static const int32_t kFadeOne = 0x7fff0000;
  static const int32_t kXfadeSamples = 256;
  void Init();
  // n even, 2..kPlateMaxBlock.
  void Update(const PlateParams& p, int32_t n, PlateCoefs* c);
  // Tests: false switches sizes at once (no fade, no crossfade).
  void set_size_fade(bool fade) {
    size_fade_ = fade;
    steady_ = false;
  }

 private:
  int32_t ModTarget(int32_t size, int32_t j) const;
  void UpdateMod(int32_t n, PlateCoefs* c);

  int32_t decay_, damp_, diffusion_, motion_, lowcut_;   // smoothed, Q15 << 8
  int32_t gain_;                    // Q31
  int32_t pre_;                     // Q16
  int32_t mod_[2], xmod_[2];        // Q16
  uint32_t lfo_phase_;
  int32_t size_, size_new_, fade_, xf_;
  bool xfading_;
  bool started_;
  bool size_fade_;
  // Still knobs and settled smoothers: the previous call's coefficients
  // hold (only the modulation moves).
  bool steady_;
  PlateParams last_;
  PlateCoefs cached_;
};

class PlateReverb {
 public:
  void Init();
  // n even, 2..64. in_r null: mono input; wet_r null: mono output (left
  // taps only). Wet at the input's scale, saturated.
  void Process(const int16_t* in_l, const int16_t* in_r, int16_t* wet_l, int16_t* wet_r,
               int32_t n, const PlateParams& p);

  // Tests.
  void set_use_asm(bool use_asm) { use_asm_ = use_asm; }
  void set_size_fade(bool fade) { control_.set_size_fade(fade); }
  const PlateCoefs& coefs() const { return coefs_; }
  const int16_t* tank_buffer() const { return tank_; }
  int32_t tank_write_ptr() const { return tank_ptr_; }
  const PlateTankKernel& tank_kernel() const { return tk_; }

 private:
  void Pre(const int16_t* in_l, const int16_t* in_r, int32_t n);

  PlateControl control_;
  PlateCoefs coefs_;
  PlateInputKernel ik_;
  PlateTankKernel tk_;
  PlateXfadeKernel xk_;
  int32_t tank_size_;               // the size tk_'s offsets are for
  int32_t in_ptr_;                  // [1, PL_IN_SIZE]: the next sample is at in_ptr - 1
  int32_t tank_ptr_;                // [1, PL_TANK_SIZE]
  int32_t pre_w_;                   // the pre-delay ring's next write
  bool use_asm_;
  int16_t x_[kPlateMaxBlock];       // the pre-delayed input, then the diffused input
  int16_t pre_[PL_PRE_WORDS];
  int16_t in_buf_[PL_IN_WORDS];
  int16_t tank_[PL_TANK_WORDS];
};

}  // namespace op1fx

#endif  // OP1FX_PLATE_PLATE_H_
