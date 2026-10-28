// TAPE ECHO: a Space Echo style multi-head tape delay in fixed point, at
// 22050 Hz (the glue decimates 2:1 and interpolates the wet signal back).
// Wet only. docs/tape-echo-port.md has the design and the measurements.
//
// One tape loop per channel (stereo: L and R side by side in 32-bit
// pairs, so every read serves both channels), a record head and three
// playback heads at 1/3, 2/3 and 1 of TIME:
//
//   in x drive + feedback x (the active heads' mix)        (record amp)
//     -> low cut (one-pole high-pass, 40..110 Hz with age)
//     -> tape loss (one-pole low-pass, 9 kHz..1.8 kHz with age)
//     -> tape saturation (sin() up to pi/2, then flat; a table)
//     -> the tape
//   wet = the active heads' mix / drive
//
// Every repeat goes round once more, so it is darker and more saturated
// than the one before. The drive (1..2 with age) raises the saturation;
// the wet divides it out, so small signals come back at unity. Feedback
// above 1 self-oscillates; the loop's only gain above 1 is the feedback,
// and the saturation caps what goes on the tape, so it stays bounded.
//
// Wow and flutter: Mutable Instruments Beads' WowAndFlutter (MIT), in
// integers: a 0.23 Hz wow whose rate and depth drift with a slow random
// sine, and a 1.99 Hz flutter, up to 96 + 16 samples of read offset at
// AGE 1 (on head 3; the heads keep their proportions, as with a tape
// speed change, so a multi-head loop stays in tune with itself).
//
// TIME glides like a tape motor: the read offsets move with a velocity
// that follows the knob (93 ms time constant), at most 0.75 sample per
// sample (pitch 0.25x .. 1.75x while it moves) and with a limited
// acceleration, so the echoes bend in pitch and never click.
//
// Arithmetic: the Blackfin's 16-bit MACs (rounded with unbiased rounding,
// saturated), as the plate's. The run kernel has a C reference
// (TapeEchoRunC) and Blackfin versions (tape_echo_bfin.S, bit-exact;
// tests/test_tape_echo.cc).

#ifndef OP1FX_TAPE_ECHO_TAPE_ECHO_H_
#define OP1FX_TAPE_ECHO_TAPE_ECHO_H_

#include <cstdint>

#include "src/tape_echo/tape_echo_layout.h"

namespace op1fx {

struct TapeEchoParams {
  int32_t time;        // 0..32767 -> 30 ms .. 1.5 s (exponential): head 3's delay
  int32_t feedback;    // 0..32767 -> 0..1.1 (linear; 1.0 at 29789)
  int32_t age;         // 0..32767: new tape -> worn (wow/flutter, loss, drive)
  int32_t heads;       // the head pattern, 0..kTapeEchoNumPatterns - 1
  bool hold;           // input off, feedback 1: the tape loops what it holds
};

// Head patterns (blue click), in order: head 3; heads 2+3; heads 1+2+3;
// heads 1+3.
const int32_t kTapeEchoNumPatterns = 4;
// Bit h set: playback head h + 1 is active (bit 0 = head 1, nearest the
// record head).
extern const uint8_t kTapeEchoPatternHeads[kTapeEchoNumPatterns];
// The heads' positions along the tape, as a fraction of TIME (Q15:
// 1/3, 2/3, 1): head h's delay is time x kTapeEchoHeadPos[h] / 32768.
const int32_t kTapeEchoNumHeads = 3;
extern const int32_t kTapeEchoHeadPos[kTapeEchoNumHeads];

const int32_t kTapeEchoMaxBlock = 64;

// TIME's mapping, for screens and tempo locks: the word's delay (head 3)
// in Q4 samples at 22050 Hz (30 ms x 50^(word / 32767)), and its inverse,
// the word for a delay (-1 if the delay is outside the range).
int32_t TapeEchoTimeSamplesQ4(int32_t word);
int32_t TapeEchoTimeWord(int32_t samples_q4);

// The run kernel's block (tape_echo_layout.h's TEK_ offsets).
struct TapeEchoKernel {
  int32_t pos[3];      // head delays, Q15 samples
  int32_t step[3];
  int32_t w[3];        // out weight (low, Q15) | feedback weight (high, Q15)
  int32_t ws[3];       // per-sample steps, per half
  int32_t gain;        // input gain, Q31 (high half used)
  int32_t gstep;
  int32_t hp_l, hp_r;  // low cut states
  int32_t lp;          // tape loss states, L | R
  int32_t k;           // low cut k (low) | tape loss k (high), Q15
  int32_t adj[3];      // per run: the wrap of each head's reads, bytes (0 or -4 N)
  int32_t sat;         // the assembly's scratch
};

// x: n stereo pairs (L low, R high), the input; replaced by the wet pairs.
// tape: the ring's first pair; p: the write position (pair index in
// 1..TE_TAPE_SIZE; each sample first moves it down one, and the run
// doesn't wrap). Mono: only the low halves of the tape are recorded (the
// wet's high halves are left over). n > 0. The C reference wraps each read
// itself; the assembly takes the wrap from k->adj (TapeEchoPrepareRun).
void TapeEchoRunC(TapeEchoKernel* k, int32_t* x, int32_t* tape, int32_t p, int32_t n, bool mono);
extern "C" void TapeEchoRunAsm(TapeEchoKernel* k, int32_t* x, int32_t* tape, int32_t* p,
                               int32_t n);
// Before a run of up to m samples at write position p: sets k->adj (each
// head's wrap, for the assembly) and returns the run's length (<= m, > 0),
// cut where a head's read crosses the tape's wrap. Each head's step must be
// under one sample per sample, so its read index never rises.
int32_t TapeEchoPrepareRun(TapeEchoKernel* k, int32_t p, int32_t m);
// Mono, on 16-bit arrays (in == wet is fine): the same as TapeEchoRunC
// with mono = true and x[i] = in[i] (sign-extended), wet[i] = the low half.
void TapeEchoRunMonoC(TapeEchoKernel* k, const int16_t* in, int16_t* wet, int32_t* tape,
                      int32_t p, int32_t n);
extern "C" void TapeEchoRunMonoAsm(TapeEchoKernel* k, const int16_t* in, int16_t* wet,
                                   int32_t* tape, int32_t* p, int32_t n);

// The tape saturation: the 16-bit record level at half scale (-32768..32767
// is -2..2 full scale) -> 32767 sin(2 x) up to 2 x = pi/2, then flat; a
// 257-point table, linear interpolation (floor). Slope 1 at 0.
int32_t TapeEchoSaturate(int32_t x);
extern const int16_t kTapeEchoSatTable[257];

class TapeEcho {
 public:
  void Init();
  // n even, 2..64. in_r null: mono input; wet_r null: mono output. Wet at
  // the input's scale, saturated. In-place calls are fine.
  void Process(const int16_t* in_l, const int16_t* in_r, int16_t* wet_l, int16_t* wet_r,
               int32_t n, const TapeEchoParams& p);

  // The screen: head 3's delay now (samples, gliding), the target, and
  // each head's presence (0..32767, ramps on pattern changes).
  int32_t time_samples() const { return t_ >> 15; }
  int32_t time_target_samples() const { return t_target_ >> 15; }
  int32_t head_level(int32_t h) const { return cur_out_[h]; }

  // Tests.
  void set_use_asm(bool use_asm) { use_asm_ = use_asm; }
  const int32_t* tape() const { return tape_; }
  const TapeEchoKernel& kernel() const { return k_; }
  int32_t velocity() const { return v_; }

 private:
  void Control(const TapeEchoParams& p, int32_t n);

  TapeEchoKernel k_;
  // Time glide: t_ (Q15 samples, head 3), v_ its velocity (Q15 samples
  // per sample).
  int32_t t_, t_target_, v_;
  // Wow and flutter (Beads): phases, the random sine's rate, the LCG.
  uint32_t wow_phase_, flutter_phase_, rand_phase_, rand_inc_, rng_;
  int32_t mod_;                      // the last call's read offset, Q15 samples
  // Smoothed knobs (Q15 << 8) and the ramps' tracked values.
  int32_t fb_, age_;
  int32_t cur_out_[3], cur_fb_[3];   // normalised head weights, Q15
  int32_t w_out_[3], w_fb_[3];       // the kernel's weights at the call's end
  int32_t gain_;                     // Q31
  int32_t ptr_;                      // [1, TE_TAPE_SIZE]
  // Cached: the last TIME, smoothed AGE and HOLD, and what follows from
  // them.
  int32_t last_time_, last_age_;
  bool last_hold_;
  int32_t inv_drive_, gain_target_, coefs_;
  bool started_;
  bool settled_;                     // the last call left every smoother at its target
  TapeEchoParams last_;
  bool use_asm_;
  int32_t x_[kTapeEchoMaxBlock];     // stereo: input pairs, then wet pairs
  int16_t mix_[kTapeEchoMaxBlock];   // mono out, stereo in: the mid
  int32_t tape_[TE_TAPE_PAIRS];
};

}  // namespace op1fx

#endif  // OP1FX_TAPE_ECHO_TAPE_ECHO_H_
