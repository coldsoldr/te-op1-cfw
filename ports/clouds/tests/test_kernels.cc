// Kernel checks (make test): each Blackfin assembly kernel against its C
// reference on random runs, sample for sample; and a checksum of the C
// outputs, which the host build prints too (C on Blackfin = C on the host).
//
// Grain kernels (src/core/grain_kernel.h): random read positions, pitch
// increments, envelope ramps, pan gains, n = 1..64, buffers with full-scale
// and extreme samples; the right channel at an arbitrary offset. Each run's
// reads stay inside the buffer (the caller's contract), which is checked
// by guard words after it.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "src/core/diffuser_fixed.h"
#include "src/core/dsp_fixed.h"
#include "src/core/grain_kernel.h"
#include "src/core/halfband.h"
#include "src/core/reverb_stereo.h"

namespace {

uint32_t rng = 12345;
uint32_t Next() {
  rng = rng * 1664525u + 1013904223u;
  return rng;
}
int32_t Range(int32_t lo, int32_t hi) {   // [lo, hi]
  return lo + static_cast<int32_t>(Next() % static_cast<uint32_t>(hi - lo + 1));
}

const int32_t kBufferWords = 4096;
const int32_t kGuardWords = 16;
const int16_t kGuard = 0x5a5a;
int16_t samples[2 * kBufferWords + 2 * kGuardWords];

uint32_t checksum = 2166136261u;
void Hash(uint32_t x) {
  checksum ^= x;
  checksum *= 16777619u;
}

int failures = 0;

void Fill() {
  for (int32_t i = 0; i < 2 * kBufferWords + 2 * kGuardWords; ++i) samples[i] = kGuard;
  for (int32_t c = 0; c < 2; ++c) {
    int16_t* s = samples + c * (kBufferWords + kGuardWords);
    for (int32_t i = 0; i < kBufferWords; ++i) {
      const uint32_t r = Next();
      if ((r & 15) == 0) {
        s[i] = (r & 16) ? 32767 : -32768;
      } else {
        s[i] = static_cast<int16_t>(r >> 16);
      }
    }
  }
}

bool GuardsIntact() {
  for (int32_t c = 0; c < 2; ++c) {
    const int16_t* g = samples + c * (kBufferWords + kGuardWords) + kBufferWords;
    for (int32_t i = 0; i < kGuardWords; ++i) {
      if (g[i] != kGuard) return false;
    }
  }
  return true;
}

typedef void (*StereoFn)(op1clouds::GrainKernel*, int32_t*, int32_t*, int32_t);
typedef void (*MonoFn)(op1clouds::GrainKernel*, int32_t*, int32_t);

void TestGrains(const char* name, bool stereo, bool hermite, StereoFn c_stereo, StereoFn asm_stereo,
                MonoFn c_mono, MonoFn asm_mono) {
  int run_failures = 0;
  for (int trial = 0; trial < 3000; ++trial) {
    op1clouds::GrainKernel k;
    const int32_t n = Range(1, 64);
    k.increment = Range(1 << 13, 1 << 18);                 // 1/8 .. 4
    const int32_t span = ((n - 1) * k.increment >> 16) + 4; // samples read
    const int32_t start = Range(0, kBufferWords - span - 1);
    k.base = samples;
    k.r_offset = (kBufferWords + kGuardWords) * 2;
    k.phase = (start << 16) | Range(0, 0xffff);
    k.env = static_cast<int32_t>(Next() >> 1);
    k.env_step = static_cast<int32_t>(Next()) >> Range(6, 20);
    k.gains_a = Range(0, 16383) | (Range(0, 16383) << 16);
    k.gains_b = Range(0, 16383) | (Range(0, 16383) << 16);
    if (trial % 7 == 0) k.gains_a = 16383 | (16383 << 16);   // full gains
    int32_t out_c[2][64], out_a[2][64];
    for (int32_t i = 0; i < 64; ++i) {
      out_c[0][i] = out_a[0][i] = static_cast<int32_t>(Next()) >> 4;
      out_c[1][i] = out_a[1][i] = static_cast<int32_t>(Next()) >> 4;
    }
    op1clouds::GrainKernel kc = k, ka = k;
    if (stereo) {
      c_stereo(&kc, out_c[0], out_c[1], n);
    } else {
      c_mono(&kc, out_c[0], n);
    }
#ifdef __bfin__
    if (stereo) {
      asm_stereo(&ka, out_a[0], out_a[1], n);
    } else {
      asm_mono(&ka, out_a[0], n);
    }
    bool ok = kc.phase == ka.phase && kc.env == ka.env;
    for (int32_t i = 0; i < 64 && ok; ++i) {
      ok = out_c[0][i] == out_a[0][i] && out_c[1][i] == out_a[1][i];
    }
    if (!ok) {
      if (run_failures < 5) {
        printf("%s: MISMATCH trial %d n %ld\n", name, trial, static_cast<long>(n));
        for (int32_t i = 0; i < n && i < 4; ++i) {
          printf("  [%ld] c %ld %ld asm %ld %ld\n", static_cast<long>(i), static_cast<long>(out_c[0][i]),
                 static_cast<long>(out_c[1][i]), static_cast<long>(out_a[0][i]), static_cast<long>(out_a[1][i]));
        }
      }
      ++run_failures;
    }
#else
    (void)asm_stereo;
    (void)asm_mono;
    (void)ka;
#endif
    for (int32_t i = 0; i < 64; ++i) {
      Hash(static_cast<uint32_t>(out_c[0][i]));
      Hash(static_cast<uint32_t>(out_c[1][i]));
    }
    Hash(static_cast<uint32_t>(kc.phase));
    Hash(static_cast<uint32_t>(kc.env));
  }
  if (!GuardsIntact()) {
    printf("%s: GUARD WORDS CHANGED\n", name);
    ++run_failures;
  }
  printf("%-24s %s\n", name, run_failures ? "FAIL" : "ok");
  failures += run_failures;
}

// The reverb (src/core/reverb_stereo.*): an instance on the C run kernel
// and one on the assembly, fed the same random blocks (32 samples, as the
// core) for more than a trip round the buffer, so the runs cover the LFO
// steps, the mirrored writes and the wrap; parameters change every 64
// blocks. Stereo, then mono.
op1clouds::ReverbStereo reverb_c;
op1clouds::ReverbStereo reverb_asm;

void TestReverb(const char* name, bool mono) {
  reverb_c.Init();
  reverb_asm.Init();
  reverb_c.set_use_asm(false);
  reverb_asm.set_use_asm(true);
  int run_failures = 0;
  const int32_t blocks = op1clouds::ReverbStereo::kSize / 32 + 300;
  for (int32_t b = 0; b < blocks; ++b) {
    if (b % 64 == 0) {
      const int32_t amount = Range(0, 0x7fffffff >> 1);
      const int32_t time = Range(0x40000000, 0x7d000000);
      const int32_t lp = Range(0x40000000, 0x7c000000);
      for (op1clouds::ReverbStereo* r : { &reverb_c, &reverb_asm }) {
        r->set_amount(amount);
        r->set_time(time);
        r->set_lp(lp);
        r->set_diffusion(op1clouds::Q(0.7, 31));
        r->set_input_gain(op1clouds::Q(0.2, 31));
      }
    }
    int32_t lc[32], rc[32], la[32], ra[32];
    for (int32_t i = 0; i < 32; ++i) {
      // Mostly music-level, sometimes loud (Q27: +-16 of headroom).
      const int32_t shift = (Next() & 63) == 0 ? 1 : 4;
      lc[i] = la[i] = static_cast<int32_t>(Next()) >> shift;
      rc[i] = ra[i] = static_cast<int32_t>(Next()) >> shift;
    }
    reverb_c.Process(lc, mono ? nullptr : rc, 32);
#ifdef __bfin__
    reverb_asm.Process(la, mono ? nullptr : ra, 32);
    bool ok = true;
    for (int32_t i = 0; i < 32 && ok; ++i) ok = lc[i] == la[i] && (mono || rc[i] == ra[i]);
    if (!ok) {
      if (run_failures < 5) {
        printf("%s: MISMATCH block %ld (write pointer %ld)\n", name, static_cast<long>(b),
               static_cast<long>(reverb_c.write_ptr()));
        for (int32_t i = 0; i < 3; ++i) {
          printf("  [%ld] c %ld %ld asm %ld %ld\n", static_cast<long>(i), static_cast<long>(lc[i]),
                 static_cast<long>(rc[i]), static_cast<long>(la[i]), static_cast<long>(ra[i]));
        }
      }
      ++run_failures;
    }
#else
    (void)la;
    (void)ra;
#endif
    for (int32_t i = 0; i < 32; ++i) {
      Hash(static_cast<uint32_t>(lc[i]));
      if (!mono) Hash(static_cast<uint32_t>(rc[i]));
    }
  }
  printf("%-24s %s\n", name, run_failures ? "FAIL" : "ok");
  failures += run_failures;
}

// The diffuser (src/core/diffuser_fixed.*): C against assembly as the
// reverb, for more than a trip round its buffer; inputs up to 4x full
// scale (Q27) to reach the Q14 saturation; amount changes every 16 blocks.
op1clouds::DiffuserFixed diffuser_c;
op1clouds::DiffuserFixed diffuser_asm;

void TestDiffuser(const char* name, bool mono) {
  diffuser_c.Init();
  diffuser_asm.Init();
  diffuser_c.set_use_asm(false);
  diffuser_asm.set_use_asm(true);
  int run_failures = 0;
  const int32_t blocks = op1clouds::DiffuserFixed::kSize / 32 * 3 + 50;
  for (int32_t b = 0; b < blocks; ++b) {
    if (b % 16 == 0) {
      const int32_t amount = Range(0, 0x7fffffff);
      diffuser_c.set_amount(amount);
      diffuser_asm.set_amount(amount);
    }
    int32_t lc[32], rc[32], la[32], ra[32];
    for (int32_t i = 0; i < 32; ++i) {
      const int32_t shift = (Next() & 31) == 0 ? 2 : 5;
      lc[i] = la[i] = static_cast<int32_t>(Next()) >> shift;
      rc[i] = ra[i] = static_cast<int32_t>(Next()) >> shift;
    }
    diffuser_c.Process(lc, mono ? nullptr : rc, 32);
#ifdef __bfin__
    diffuser_asm.Process(la, mono ? nullptr : ra, 32);
    bool ok = true;
    for (int32_t i = 0; i < 32 && ok; ++i) ok = lc[i] == la[i] && (mono || rc[i] == ra[i]);
    if (!ok) {
      if (run_failures < 5) {
        printf("%s: MISMATCH block %ld\n", name, static_cast<long>(b));
        for (int32_t i = 0; i < 3; ++i) {
          printf("  [%ld] c %ld %ld asm %ld %ld\n", static_cast<long>(i), static_cast<long>(lc[i]),
                 static_cast<long>(rc[i]), static_cast<long>(la[i]), static_cast<long>(ra[i]));
        }
      }
      ++run_failures;
    }
#else
    (void)la;
    (void)ra;
#endif
    for (int32_t i = 0; i < 32; ++i) {
      Hash(static_cast<uint32_t>(lc[i]));
      if (!mono) Hash(static_cast<uint32_t>(rc[i]));
    }
  }
  printf("%-24s %s\n", name, run_failures ? "FAIL" : "ok");
  failures += run_failures;
}

// The half-rate kernels (src/core/halfband.h): random runs with extreme
// samples, n over the whole range, random fades.
int16_t RandomSample() {
  const uint32_t r = Next();
  if ((r & 15) == 0) return (r & 16) ? 32767 : -32768;
  return static_cast<int16_t>(r >> 16);
}

void TestHalfband() {
  using namespace op1clouds;
  static SoftLimitTable table;
  InitSoftLimitTable(&table);
  int run_failures = 0;
  for (int32_t run = 0; run < 300; ++run) {
    const int32_t kind = run % 3;
    const int32_t n = kind == 2 ? Range(1, 64) : Range(1, 32);
    int16_t x[18 + 64], c[128], a[128];
    for (int32_t i = 0; i < 18 + 64; ++i) x[i] = RandomSample();
    int16_t wet[64];
    for (int32_t i = 0; i < 64; ++i) wet[i] = RandomSample();
    const int32_t fade_out = Range(0, 32767);
    const int32_t wet_gain = Range(0, 19661);
    for (int32_t i = 0; i < 128; ++i) c[i] = a[i] = static_cast<int16_t>(i);
    int32_t outputs;
    if (kind == 0) {
      HalfbandDecimateC(x, c, n);
      outputs = n;
    } else if (kind == 1) {
      HalfbandInterpolateC(x, c, n);
      outputs = 2 * n;
    } else {
      for (int32_t i = 0; i < n; ++i) c[i] = a[i] = x[i];
      MixSoftLimitC(c, wet, n, fade_out, wet_gain, &table);
      outputs = n;
    }
#ifdef __bfin__
    if (kind == 0) {
      HalfbandDecimateAsm(x, a, n);
    } else if (kind == 1) {
      HalfbandInterpolateAsm(x, a, n);
    } else {
      MixSoftLimitAsm(a, wet, n, fade_out | (wet_gain << 16), &table);
    }
    bool ok = true;
    for (int32_t i = 0; i < 128 && ok; ++i) ok = c[i] == a[i];
    if (!ok) {
      if (run_failures < 5) {
        printf("halfband kind %ld n %ld: MISMATCH\n", static_cast<long>(kind), static_cast<long>(n));
        for (int32_t i = 0; i < outputs && i < 4; ++i) {
          printf("  [%ld] c %d asm %d\n", static_cast<long>(i), c[i], a[i]);
        }
      }
      ++run_failures;
    }
#endif
    for (int32_t i = 0; i < outputs; ++i) Hash(static_cast<uint32_t>(c[i]));
  }
  printf("%-24s %s\n", "halfband + mix", run_failures ? "FAIL" : "ok");
  failures += run_failures;
}

#ifndef __bfin__
// The assembly isn't built on the host.
void NoStereo(op1clouds::GrainKernel*, int32_t*, int32_t*, int32_t) { }
void NoMono(op1clouds::GrainKernel*, int32_t*, int32_t) { }
#define ASM_STEREO(f) NoStereo
#define ASM_MONO(f) NoMono
#else
#define ASM_STEREO(f) f
#define ASM_MONO(f) f
#endif

}  // namespace

int main() {
  using namespace op1clouds;
  Fill();
  TestGrains("grain linear stereo", true, false, GrainLinearStereoC, ASM_STEREO(GrainLinearStereoAsm),
             nullptr, nullptr);
  TestGrains("grain hermite stereo", true, true, GrainHermiteStereoC, ASM_STEREO(GrainHermiteStereoAsm),
             nullptr, nullptr);
  TestGrains("grain linear mono", false, false, nullptr, nullptr, GrainLinearMonoC,
             ASM_MONO(GrainLinearMonoAsm));
  TestGrains("grain hermite mono", false, true, nullptr, nullptr, GrainHermiteMonoC,
             ASM_MONO(GrainHermiteMonoAsm));
  TestReverb("reverb stereo", false);
  TestReverb("reverb mono", true);
  TestDiffuser("diffuser stereo", false);
  TestDiffuser("diffuser mono", true);
  TestHalfband();
  printf("checksum %08lx\n", static_cast<unsigned long>(checksum));
  return failures ? 1 : 0;
}
