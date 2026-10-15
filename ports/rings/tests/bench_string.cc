// Blackfin cycles of the fixed-point string (src/string_fixed.*), per
// 32-sample block at 44.1 kHz: steady (no coefficient update), with a
// coefficient update every block, with and without dispersion; and the
// plucker.
//
//   make bench-string

#include <cstdint>
#include <cstdio>

#include "harness/cycles.h"
#include "src/ffloat.h"
#include "src/string_fixed.h"
#include "src/string_kernel.h"
#include "src/reverb_fixed.h"

namespace {

const int kBlock = 32;

op1rings::StringFixed string;
op1rings::StretchLine stretch;
op1rings::PluckerFixed plucker;
int32_t in[kBlock], out[kBlock];

int32_t Q31(double x) { return op1rings::FFloat(x).ToQ31(); }

void Bench(const char* name, bool dispersion, bool update) {
  string.Init();
  stretch.Init();
  plucker.Init();
  string.set_frequency(Q31(220.0 / 44100.0));
  string.set_dispersion(dispersion ? Q31(0.3) : 0);
  string.set_brightness(Q31(0.6));
  string.set_damping(Q31(0.7));
  string.set_position(Q31(0.3));
  plucker.Trigger(Q31(220.0 / 44100.0), Q31(0.2), Q31(0.3));
  harness::CycleStats stats;
  for (int b = 0; b < 200; ++b) {
    if (update) string.set_damping(Q31(0.7) + b * 1000);
    plucker.Process(in, kBlock);
    for (int i = 0; i < kBlock; ++i) out[i] = 0;
    uint64_t t0 = harness::ReadCycles();
    string.Process(in, out, kBlock, dispersion ? &stretch : nullptr);
    if (b > 0) stats.Add(harness::ReadCycles() - t0);
  }
  stats.Print(name, kBlock);
}

}  // namespace

// StringKernelAsm against StringKernelC: same outputs, state and line, over
// blocks with moving delays and taps, signals that hit the clamp, and
// write pointers that wrap through the guard.
#ifdef __bfin__
bool CheckKernel() {
  static int32_t line_c[op1rings::kStringKernelSize + op1rings::kStringKernelGuard];
  static int32_t line_asm[op1rings::kStringKernelSize + op1rings::kStringKernelGuard];
  uint32_t rng = 1;
  for (int i = 0; i < op1rings::kStringKernelSize; ++i) {
    rng = rng * 1664525u + 1013904223u;
    line_c[i] = static_cast<int32_t>(rng) >> 5;   // +-2 in Q25
  }
  for (int i = 0; i < op1rings::kStringKernelGuard; ++i) line_c[op1rings::kStringKernelSize + i] = line_c[i];
  for (int i = 0; i < op1rings::kStringKernelSize + op1rings::kStringKernelGuard; ++i) line_asm[i] = line_c[i];
  op1rings::StringKernel c = { line_c, 5, 37 << 19, 1234567, 11 << 19, -98765, Q31(0.6), 2000, Q31(0.1),
                               -1500, Q31(0.8), 1 << 24, -(1 << 24), 1 << 23, 3 << 22 };
  op1rings::StringKernel a = c;
  a.line = line_asm;
  uint32_t checksum = 0;
  bool same = true;
  for (int b = 0; b < 300; ++b) {
    int32_t input[kBlock], out_c[kBlock], out_a[kBlock];
    for (int i = 0; i < kBlock; ++i) {
      rng = rng * 1664525u + 1013904223u;
      // Mostly small input, now and then large enough to hit the clamp.
      input[i] = static_cast<int32_t>(rng) >> ((b % 50) == 0 ? 3 : 8);
      out_c[i] = out_a[i] = i;
    }
    if (b % 40 == 0) {
      // New delays and steps, including short and long ones.
      c.loop_delay_step = a.loop_delay_step = (b % 80 == 0 ? 1 : -1) * (b * 4567);
      c.comb_delay_step = a.comb_delay_step = (b % 120 == 0 ? -1 : 1) * (b * 345);
    }
    op1rings::StringKernelC(&c, input, out_c, kBlock);
    op1rings::StringKernelAsm(&a, input, out_a, kBlock);
    for (int i = 0; i < kBlock; ++i) {
      if (out_c[i] != out_a[i]) same = false;
      checksum = checksum * 31u + static_cast<uint32_t>(out_c[i]);
    }
    // Keep the delays in range.
    if (c.loop_delay < (4 << 19) || c.loop_delay > (2040 << 19)) {
      c.loop_delay_step = a.loop_delay_step = -c.loop_delay_step;
    }
    if (c.comb_delay < (1 << 19) || c.comb_delay > (1000 << 19)) {
      c.comb_delay_step = a.comb_delay_step = -c.comb_delay_step;
    }
  }
  const int32_t* pc = &c.write_ptr;
  const int32_t* pa = &a.write_ptr;
  for (int i = 0; i < 14; ++i) {
    if (pc[i] != pa[i]) same = false;
  }
  for (int i = 0; i < op1rings::kStringKernelSize + op1rings::kStringKernelGuard; ++i) {
    if (line_c[i] != line_asm[i]) same = false;
  }
  printf("kernel: C vs assembly %s, checksum %08x\n", same ? "identical" : "DIFFERENT",
         static_cast<unsigned>(checksum));
  return same;
}

// StringKernelStretchAsm against StringKernelStretchC, with stretch amounts
// that switch the allpass on and off (both branches of the loop).
bool CheckStretchKernel() {
  static int32_t line_c[op1rings::kStringKernelSize + op1rings::kStringKernelGuard];
  static int32_t line_asm[op1rings::kStringKernelSize + op1rings::kStringKernelGuard];
  static int32_t stretch_c[op1rings::kStretchKernelSize];
  static int32_t stretch_asm[op1rings::kStretchKernelSize];
  uint32_t rng = 11;
  for (int i = 0; i < op1rings::kStringKernelSize; ++i) {
    rng = rng * 1664525u + 1013904223u;
    line_c[i] = line_asm[i] = static_cast<int32_t>(rng) >> 5;
  }
  for (int i = 0; i < op1rings::kStringKernelGuard; ++i) {
    line_c[op1rings::kStringKernelSize + i] = line_asm[op1rings::kStringKernelSize + i] = line_c[i];
  }
  for (int i = 0; i < op1rings::kStretchKernelSize; ++i) {
    rng = rng * 1664525u + 1013904223u;
    stretch_c[i] = stretch_asm[i] = static_cast<int32_t>(rng) >> 6;
  }
  op1rings::StringKernel c = { line_c, 9, 61 << 19, 0, 23 << 19, 0, Q31(0.7), 0, Q31(0.05),
                               0, Q31(0.6), 1 << 23, -(1 << 23), 1 << 22, 1 << 21 };
  c.stretch_line = stretch_c;
  c.stretch_ptr = 1000;
  c.stretch = 0;
  c.stretch_step = 0;
  c.ap_gain = Q31(-0.3);
  c.ap_gain_step = 0;
  op1rings::StringKernel a = c;
  a.line = line_asm;
  a.stretch_line = stretch_asm;
  uint32_t checksum = 0;
  bool same = true;
  for (int b = 0; b < 300; ++b) {
    int32_t input[kBlock], out_c[kBlock], out_a[kBlock];
    for (int i = 0; i < kBlock; ++i) {
      rng = rng * 1664525u + 1013904223u;
      input[i] = static_cast<int32_t>(rng) >> ((b % 40) == 0 ? 3 : 9);
      out_c[i] = out_a[i] = -i;
    }
    // Stretch ramps between 0 (allpass too short: plain reads) and ~0.4.
    const int32_t target = (b / 30) % 2 ? Q31(0.4) : 0;
    c.stretch_step = a.stretch_step = (target - c.stretch) >> 5;
    c.ap_gain_step = a.ap_gain_step = (b % 25 == 0) ? 3000 : -1000;
    c.loop_delay_step = a.loop_delay_step = (b % 60 < 30 ? 1 : -1) * 9000;
    c.comb_delay_step = a.comb_delay_step = (b % 44 < 22 ? 1 : -1) * 3000;
    op1rings::StringKernelStretchC(&c, input, out_c, kBlock);
    op1rings::StringKernelStretchAsm(&a, input, out_a, kBlock);
    for (int i = 0; i < kBlock; ++i) {
      if (out_c[i] != out_a[i]) same = false;
      checksum = checksum * 31u + static_cast<uint32_t>(out_c[i]);
    }
  }
  const int32_t* pc = &c.write_ptr;
  const int32_t* pa = &a.write_ptr;
  for (int i = 0; i < 14; ++i) {
    if (pc[i] != pa[i]) same = false;
  }
  if (c.stretch_ptr != a.stretch_ptr || c.stretch != a.stretch || c.ap_gain != a.ap_gain) same = false;
  for (int i = 0; i < op1rings::kStringKernelSize + op1rings::kStringKernelGuard; ++i) {
    if (line_c[i] != line_asm[i]) same = false;
  }
  for (int i = 0; i < op1rings::kStretchKernelSize; ++i) {
    if (stretch_c[i] != stretch_asm[i]) same = false;
  }
  printf("stretch kernel: C vs assembly %s, checksum %08x\n", same ? "identical" : "DIFFERENT",
         static_cast<unsigned>(checksum));
  return same;
}
#endif

int main() {
  harness::EnableCycleCounter();
#ifdef __bfin__
  if (!CheckKernel()) return 1;
  if (!CheckStretchKernel()) return 1;
#endif
  Bench("string, steady", false, false);
  Bench("string, update", false, true);
  Bench("dispersion, steady", true, false);
  Bench("dispersion, update", true, true);

  harness::CycleStats pluck;
  for (int b = 0; b < 100; ++b) {
    if (b % 10 == 0) plucker.Trigger(Q31(220.0 / 44100.0), Q31(0.2), Q31(0.3));
    uint64_t t0 = harness::ReadCycles();
    plucker.Process(in, kBlock);
    pluck.Add(harness::ReadCycles() - t0);
  }
  pluck.Print("plucker", kBlock);

  // The reverb over several wraps of its write pointer, with a Clear on the
  // way and block sizes that move the runs' boundaries about. Guard words
  // around it catch stray writes (the simulator doesn't fault on them).
  static struct {
    uint32_t before[256];
    op1rings::ReverbFixed reverb;
    uint32_t after[256];
  } guarded;
  const uint32_t kGuard = 0x5a5aa5a5u;
  for (int i = 0; i < 256; ++i) guarded.before[i] = guarded.after[i] = kGuard;
  op1rings::ReverbFixed& reverb = guarded.reverb;
  reverb.Init();
  reverb.set_amount(Q31(0.4));
  reverb.set_time(Q31(0.8));
  reverb.set_lp(Q31(0.6));
  harness::CycleStats fresh, settled;
  uint32_t reverb_checksum = 0;
  bool reverb_ok = true;
  int32_t samples = 0, since_clear = 0;
  static int32_t block[64];
  for (int b = 0; b < 4000; ++b) {
    if (b == 2500) {
      reverb.Clear();
      since_clear = 0;
    }
    // Mostly 32, now and then 1 to 64.
    const int size = b % 7 == 3 ? 1 + (b * 37) % 64 : kBlock;
    for (int i = 0; i < size; ++i) block[i] = (b * 37 + i * 101) % 4096 << 12;
    uint64_t t0 = harness::ReadCycles();
    reverb.Process(block, size);
    const uint64_t t = harness::ReadCycles() - t0;
    // Fresh: the C loop runs for 6,312 samples after a Clear.
    if (size == kBlock) (since_clear + size <= 6312 ? fresh : settled).Add(t);
    samples += size;
    since_clear += size;
    for (int i = 0; i < size; ++i) reverb_checksum = reverb_checksum * 31u + static_cast<uint32_t>(block[i]);
    if (reverb.write_ptr() < 0 || reverb.write_ptr() >= op1rings::ReverbFixed::kSize) {
      if (reverb_ok) printf("reverb: write pointer %ld out of range at block %d\n",
                            static_cast<long>(reverb.write_ptr()), b);
      reverb_ok = false;
      break;
    }
  }
  for (int i = 0; i < 256; ++i) {
    if (guarded.before[i] != kGuard || guarded.after[i] != kGuard) reverb_ok = false;
  }
  // Must match the host build (make bench-string compares them).
  printf("reverb: %ld samples, guards %s, checksum %08x\n", static_cast<long>(samples),
         reverb_ok ? "intact" : "BROKEN", static_cast<unsigned>(reverb_checksum));
  fresh.Print("reverb, after a Clear", kBlock);
  settled.Print("reverb, settled", kBlock);
  return reverb_ok ? 0 : 1;
}
