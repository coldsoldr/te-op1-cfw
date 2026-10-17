// Renders every scenario with one implementation and writes the outputs:
//   -DIMPL_REF   upstream Marbles (mutableinstruments/marbles, float)
//   -DIMPL_COPY  the copy in upstream/ (namespace op1marbles, float)
//   -DIMPL_PORT  the fixed-point port in src/
//
// Output per scenario: <dir>/<name>.bin, one record per Marbles sample:
// uint8 gates (T1, T2, T3 in bits 0..2) then X1, X2, X3, Y as float32 volts
// (the port's Q16 volts convert to float exactly). A checksum (FNV-1a over
// the records) is printed per scenario, so builds can be compared without
// the files (Blackfin: `render.elf` prints the same checksums and cycles).
//
// Usage: render [outdir|""] [seconds-override] [scenario-name]

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(IMPL_REF)
#define MARBLES_NS marbles
#include "marbles/random/random_generator.h"
#include "marbles/random/random_stream.h"
#include "marbles/random/t_generator.h"
#include "marbles/random/x_y_generator.h"
#include "harness/float_driver.h"
typedef harness::FloatDriver Driver;
static const char* kImpl = "ref";
#elif defined(IMPL_COPY)
#define MARBLES_NS op1marbles
#include "upstream/random/random_generator.h"
#include "upstream/random/random_stream.h"
#include "upstream/random/t_generator.h"
#include "upstream/random/x_y_generator.h"
#include "harness/float_driver.h"
typedef harness::FloatDriver Driver;
static const char* kImpl = "copy";
#elif defined(IMPL_PORT)
#include "harness/fixed_driver.h"
typedef harness::FixedDriver Driver;
static const char* kImpl = "port";
#else
#error "define IMPL_REF, IMPL_COPY or IMPL_PORT"
#endif

#include "harness/cycles.h"

using namespace harness;

static Driver driver;  // large: keep off the stack

static uint32_t Fnv(uint32_t h, const void* data, size_t n) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  while (n--) {
    h ^= *p++;
    h *= 16777619u;
  }
  return h;
}

int main(int argc, char** argv) {
  const char* outdir = argc > 1 && argv[1][0] ? argv[1] : 0;
  float seconds_override = argc > 2 ? static_cast<float>(atof(argv[2])) : 0.0f;
  const char* only = argc > 3 ? argv[3] : 0;
#ifdef __bfin__
  EnableCycleCounter();
#endif
  uint32_t all = 2166136261u;
  for (int i = 0; i < NumScenarios(); ++i) {
    Scenario s = GetScenario(i);
    if (only && strcmp(only, s.name)) continue;
    float seconds = seconds_override > 0.0f ? seconds_override : s.seconds;
    long ticks = static_cast<long>(seconds * kTickRate);
    FILE* fp = 0;
    if (outdir) {
      char path[512];
      snprintf(path, sizeof(path), "%s/%s.bin", outdir, s.name);
      fp = fopen(path, "wb");
      if (!fp) {
        fprintf(stderr, "can't write %s\n", path);
        return 1;
      }
    }
    driver.Init(s.seed);
    Frame frames[kSubsteps];
    uint32_t h = 2166136261u;
    CycleStats stats;
    for (long t = 0; t < ticks; ++t) {
      Params p = At(s, ticks > 1 ? static_cast<double>(t) / (ticks - 1) : 0.0);
      driver.SetParams(p);
      uint64_t c0 = ReadCycles();
      driver.Render();
      stats.Add(ReadCycles() - c0);
      driver.Output(frames);
      for (int k = 0; k < kSubsteps; ++k) {
        uint8_t rec[17];
        rec[0] = frames[k].gates;
        memcpy(&rec[1], frames[k].voltage, 16);
        h = Fnv(h, rec, sizeof(rec));
        if (fp) fwrite(rec, 1, sizeof(rec), fp);
      }
    }
    if (fp) fclose(fp);
    all = Fnv(all, &h, sizeof(h));
#ifdef __bfin__
    printf("%-5s %-16s ticks %6ld checksum %08x  cycles/tick avg %6.0f max %6lu\n",
           kImpl, s.name, ticks, (unsigned)h,
           stats.n ? static_cast<double>(stats.total) / stats.n : 0.0,
           static_cast<unsigned long>(stats.max));
#else
    printf("%-5s %-16s ticks %6ld checksum %08x\n", kImpl, s.name, ticks, (unsigned)h);
#endif
  }
  printf("%-5s all scenarios checksum %08x\n", kImpl, (unsigned)all);
  return 0;
}
