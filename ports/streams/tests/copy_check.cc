// Upstream Streams processors (mutableinstruments/streams, namespace streams)
// against the copy in src/dsp (namespace op1streams), both at upstream's
// 31089 Hz, sample for sample: gain and frequency outputs must be identical.
// Built twice by the Makefile: OP1STREAMS_FAST=0 (the copy as upstream) and
// OP1STREAMS_FAST=1 (the exact speed-ups of src/dsp/retime.h).

#include <cstdio>
#include <cstring>

#include "streams/processor.h"
#include "src/dsp/processor.h"

#include "harness/drive.h"

#if OP1STREAMS_SAMPLE_RATE != 31089 || OP1STREAMS_CENTROID_STRIDE != 1
#error "copy_check compares at upstream's rate with the exact centroid"
#endif

static streams::Processor upstream;
static op1streams::Processor copy;

struct Recorder {
  uint16_t* gain;
  uint16_t* frequency;
  void operator()(int32_t i, uint16_t g, uint16_t f) {
    gain[i] = g;
    frequency[i] = f;
  }
};

int main() {
  const int32_t kSamples = 31089 * 6;
  static uint16_t ug[31089 * 6], uf[31089 * 6], cg[31089 * 6], cf[31089 * 6];
  static const char* const kFunctions[] = {
    "envelope", "vactrol", "follower", "compressor", "filter_ctrl", "lorenz" };
  int failures = 0;
  for (int k = 0; k < harness::NumDriveCases(); ++k) {
    harness::DriveCase c = harness::GetDriveCase(k);
    memset((void*)&upstream, 0, sizeof(upstream));  // upstream's are zeroed globals
    memset((void*)&copy, 0, sizeof(copy));
    Recorder ru = { ug, uf };
    Recorder rc = { cg, cf };
    harness::Drive(&upstream, c, 31089, kSamples, ru);
    harness::Drive(&copy, c, 31089, kSamples, rc);
    int32_t diffs = 0, first = -1;
    for (int32_t i = 0; i < kSamples; ++i) {
      if (ug[i] != cg[i] || uf[i] != cf[i]) {
        if (first < 0) first = i;
        ++diffs;
      }
    }
    // The copy's ProcessBlock (used by the port) must match too.
    memset((void*)&copy, 0, sizeof(copy));
    harness::DriveBlocks(&copy, c, 31089, kSamples, rc);
    for (int32_t i = 0; i < kSamples; ++i) {
      if (ug[i] != cg[i] || uf[i] != cf[i]) {
        if (first < 0) first = i;
        ++diffs;
      }
    }
    // Activity, so a pass can't come from constant outputs.
    int32_t gmin = 65535, gmax = 0, fmin = 65535, fmax = 0, changes = 0;
    for (int32_t i = 0; i < kSamples; ++i) {
      if (ug[i] < gmin) gmin = ug[i];
      if (ug[i] > gmax) gmax = ug[i];
      if (uf[i] < fmin) fmin = uf[i];
      if (uf[i] > fmax) fmax = uf[i];
      if (i && (ug[i] != ug[i - 1] || uf[i] != uf[i - 1])) ++changes;
    }
    printf("%-12s %-9s %-7s %-6s gain %5d..%5d freq %5d..%5d changes %6d : %s",
           kFunctions[c.function],
           c.alternate ? "alternate" : "normal", c.linked ? "linked" : "single",
           harness::MaterialName(c.material), gmin, gmax, fmin, fmax, changes,
           diffs ? "DIFFERENT" : "identical");
    if (diffs) {
      printf(" (%d samples, first at %d: gain %u/%u freq %u/%u)", diffs, first,
             ug[first], cg[first], uf[first], cf[first]);
      ++failures;
    }
    printf("\n");
  }
  printf("copy-check (OP1STREAMS_FAST=%d): %d of %d cases %s, %d samples each\n",
         OP1STREAMS_FAST, harness::NumDriveCases() - failures, harness::NumDriveCases(),
         "bit-identical (per sample and in 64-sample blocks)", kSamples);
  return failures ? 1 : 0;
}
