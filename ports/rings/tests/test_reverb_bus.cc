// op1/reverb_bus.h on the host: the shared reverb runs once per OP-1 block
// whatever order the voices render in, every carrier's dry signal goes into
// it, and the carriers together play its wet signal once (checked against a
// second SharedReverb fed the same sends). Also: no runs without carriers,
// a restart after a gap, and voices joining and leaving.
//
//   make test-reverb-bus   (exits 1 on a failure)

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "op1/reverb_bus.h"
#include "src/ffloat.h"

namespace {

using op1::ReverbBus;
using op1rings::SharedReverb;
using op1rings::VoicePatch;

const int32_t kBlock = ReverbBus::kBlock;
const uint32_t kBlockCycles = 580500;   // one OP-1 block at 400 MHz

int32_t Q31(double x) { return op1rings::FFloat(x).ToQ31(); }

const VoicePatch kPatch = { Q31(0.4), Q31(0.6), Q31(0.7), Q31(0.3) };

int failures = 0;

void Check(bool ok, const char* what) {
  printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

// Drives a ReverbBus as op1/engine.cc does, with a reference SharedReverb
// fed the same sends, and compares the carriers' summed output with it.
class Harness {
 public:
  void Init() {
    bus_.Init();
    ref_.Init();
    now_ = 12345;
    rng_ = 1;
    last_runs_ = last_clears_ = 0;
    have_expected_ = false;
    for (int i = 0; i < kBlock; ++i) seg_send_[i] = seg_out_[i] = expected_[i] = 0;
    seg_carriers_ = prev_carriers_ = prev2_carriers_ = 0;
    worst_steady_ = 0;
    worst_transition_ = 0;
    peak_expected_ = 0;
    steady_segments_ = 0;
    untouched_ = true;
  }

  // One OP-1 block: the voices in `order` (count n) render; carrier[v].
  void Block(const int32_t* order, int n, const bool* carrier) {
    for (int k = 0; k < n; ++k) {
      Render(order[k], carrier[order[k]]);
      now_ += 1000;
    }
    now_ += kBlockCycles - 1000 * n;
  }

  void Pause(uint32_t cycles) { now_ += cycles; }

  // Forget the comparisons so far (the first block after silence ramps the
  // wet signal in from nothing, which the reference doesn't).
  void ResetStats() {
    worst_steady_ = worst_transition_ = 0;
    steady_segments_ = 0;
  }

  // As op1/engine.cc does: the bus first (it may end the block and run),
  // then the voice sends its dry signal and mixes its share in.
  void Render(int32_t v, bool carrier) {
    int32_t share[kBlock];
    const int32_t kUntouched = 0x5a5a5a5a;
    for (int i = 0; i < kBlock; ++i) share[i] = kUntouched;
    const bool shared = bus_.Render(v, now_, kPatch, carrier, share);
    if (bus_.clears() != last_clears_) {
      ref_.Clear();
      last_clears_ = bus_.clears();
    }
    if (bus_.runs() != last_runs_) Segment();
    if (carrier) {
      // A plucked-ish signal per voice: noise bursts, ~0.3 full scale.
      int32_t* send = bus_.send();
      for (int i = 0; i < kBlock; ++i) {
        rng_ = rng_ * 1664525u + 1013904223u;
        const int32_t dry = (static_cast<int32_t>(rng_) >> 6) * ((v + 1) & 3) / 3;
        send[i] += dry;
        seg_send_[i] += dry;
      }
      seg_carriers_ |= 1u << v;
    }
    for (int i = 0; i < kBlock; ++i) {
      if (shared) {
        seg_out_[i] += share[i];
      } else if (share[i] != kUntouched) {
        untouched_ = false;   // no share: the buffer must be left alone
      }
    }
    if (shared && !carrier) untouched_ = false;
  }

  ReverbBus& bus() { return bus_; }
  int32_t worst_steady() const { return worst_steady_; }
  int32_t worst_transition() const { return worst_transition_; }
  int32_t peak_expected() const { return peak_expected_; }
  int32_t steady_segments() const { return steady_segments_; }
  bool untouched() const { return untouched_; }

 private:
  // The bus has just run on the segment that ended: check what the segment
  // played against the reference's wet signal of the one before, then run
  // the reference on this segment's sends.
  void Segment() {
    if (have_expected_) {
      int32_t worst = 0;
      for (int i = 0; i < kBlock; ++i) {
        const int32_t e = abs(seg_out_[i] - expected_[i]);
        if (e > worst) worst = e;
        if (abs(expected_[i]) > peak_expected_) peak_expected_ = abs(expected_[i]);
      }
      const bool steady = seg_carriers_ == prev_carriers_ && prev_carriers_ == prev2_carriers_;
      if (steady) {
        ++steady_segments_;
        if (worst > worst_steady_) worst_steady_ = worst;
      } else if (worst > worst_transition_) {
        worst_transition_ = worst;
      }
    }
    for (int i = 0; i < kBlock; ++i) expected_[i] = seg_send_[i];
    ref_.Process(kPatch, expected_);
    have_expected_ = true;
    for (int i = 0; i < kBlock; ++i) seg_send_[i] = seg_out_[i] = 0;
    prev2_carriers_ = prev_carriers_;
    prev_carriers_ = seg_carriers_;
    seg_carriers_ = 0;
    last_runs_ = bus_.runs();
  }

  ReverbBus bus_;
  SharedReverb ref_;
  uint32_t now_;
  uint32_t rng_;
  int32_t last_runs_, last_clears_;
  bool have_expected_;
  int32_t seg_send_[kBlock], seg_out_[kBlock], expected_[kBlock];
  uint32_t seg_carriers_, prev_carriers_, prev2_carriers_;
  int32_t worst_steady_, worst_transition_, peak_expected_, steady_segments_;
  bool untouched_;
};

Harness h;

// Within a few LSBs (Q27): the shares' rounding.
const int32_t kTolerance = 16;

void SteadyShuffled() {
  printf("3 carriers and a fading voice, a new render order every block:\n");
  h.Init();
  bool carrier[6] = { true, false, true, false, true, false };
  int32_t order[4] = { 0, 2, 4, 5 };
  uint32_t rng = 7;
  const int kBlocks = 2000;   // ~2.9 s: several wraps of the reverb's buffer
  for (int b = 0; b < kBlocks; ++b) {
    for (int k = 3; k > 0; --k) {
      rng = rng * 1664525u + 1013904223u;
      const int j = static_cast<int>((rng >> 16) % (k + 1));
      const int32_t t = order[k]; order[k] = order[j]; order[j] = t;
    }
    h.Block(order, 4, carrier);
  }
  char line[96];
  snprintf(line, sizeof(line), "one reverb run per block (%d runs, %d blocks)", h.bus().runs(), kBlocks);
  Check(h.bus().runs() == kBlocks - 1, line);
  Check(h.bus().clears() == 1, "cleared once, at the start");
  snprintf(line, sizeof(line), "carriers' sum = the reverb of their sum (worst %d LSB)", h.worst_steady());
  Check(h.worst_steady() <= kTolerance && h.steady_segments() > kBlocks - 10, line);
  snprintf(line, sizeof(line), "the wet signal isn't silent (peak %.3f)", h.peak_expected() / 134217728.0);
  Check(h.peak_expected() > (1 << 22), line);
  Check(h.untouched(), "the fading voice gets no share");
}

void JoinAndLeave() {
  printf("voices joining and leaving (renders in index order):\n");
  h.Init();
  // Carrier sets, 300 blocks each: 1 joins below 3, 0 leaves, and so on.
  const uint32_t kSets[] = { 0x01, 0x09, 0x0b, 0x0a, 0x02, 0x3f, 0x02 };
  const int kPhases = sizeof(kSets) / sizeof(kSets[0]);
  int blocks = 0;
  for (int p = 0; p < kPhases; ++p) {
    bool carrier[6];
    int32_t order[6];
    int n = 0;
    for (int v = 0; v < 6; ++v) {
      carrier[v] = (kSets[p] >> v) & 1;
      if (carrier[v]) order[n++] = v;
    }
    for (int b = 0; b < 300; ++b, ++blocks) h.Block(order, n, carrier);
  }
  char line[96];
  snprintf(line, sizeof(line), "one reverb run per block (%d runs, %d blocks)", h.bus().runs(), blocks);
  Check(h.bus().runs() == blocks - 1, line);
  snprintf(line, sizeof(line), "steady stretches match the reference (worst %d LSB)", h.worst_steady());
  Check(h.worst_steady() <= kTolerance, line);
  // At a change, a carrier that left is missing from one block's sum (its
  // envelope had closed on the OP-1), and the rest ramp to their new share.
  snprintf(line, sizeof(line), "changes stay within the wet level (worst %.3f of peak %.3f)",
           h.worst_transition() / 134217728.0, h.peak_expected() / 134217728.0);
  Check(h.worst_transition() <= h.peak_expected(), line);
}

// Carriers joining while others play: the shares sum to 1 throughout (the
// new one waits a block, then all ramp together), so the sum matches the
// reference even across the joins.
void JoinNoSwell() {
  printf("a new carrier doesn't swell the reverb:\n");
  h.Init();
  bool carrier[6] = { true, false, false, false, false, false };
  int32_t order[6] = { 0, 1, 2, 3, 4, 5 };
  for (int b = 0; b < 400; ++b) h.Block(order, 1, carrier);
  const int32_t steady_before = h.worst_steady();
  h.ResetStats();
  carrier[3] = true;
  int32_t order2[2] = { 0, 3 };
  for (int b = 0; b < 400; ++b) h.Block(order2, 2, carrier);
  carrier[1] = true;
  int32_t order3[3] = { 0, 1, 3 };
  for (int b = 0; b < 400; ++b) h.Block(order3, 3, carrier);
  char line[96];
  snprintf(line, sizeof(line), "joins match the reference throughout (worst %d LSB, steady %d)",
           h.worst_transition(), steady_before);
  Check(h.worst_transition() <= kTolerance && h.worst_steady() <= kTolerance, line);
}

// Steals (polyphony 1 and 2): the new voice takes over the stolen one's
// share with its ring (ReverbBus::Transfer), and the stolen voice fades out
// without one: the sum stays whole through every note.
void Steals() {
  printf("steals hand the share over:\n");
  h.Init();
  bool carrier[6] = { true, false, false, false, false, false };
  int32_t order[1] = { 0 };
  for (int b = 0; b < 300; ++b) h.Block(order, 1, carrier);
  h.ResetStats();
  // Polyphony 1: each note on the next OP-1 voice steals the last one,
  // which fades for 4 blocks (and is still rendered, without a share).
  int32_t last = 0;
  for (int note = 1; note <= 12; ++note) {
    const int32_t v = note % 6;
    h.bus().Transfer(last, v);
    carrier[last] = false;
    carrier[v] = true;
    for (int b = 0; b < 40; ++b) {
      int32_t both[2] = { last, v };
      int32_t alone[1] = { v };
      if (b < 4) h.Block(both, 2, carrier); else h.Block(alone, 1, carrier);
    }
    last = v;
  }
  char line[96];
  snprintf(line, sizeof(line), "polyphony 1: whole through 12 steals (worst %d LSB)",
           h.worst_transition() > h.worst_steady() ? h.worst_transition() : h.worst_steady());
  Check(h.worst_transition() <= kTolerance && h.worst_steady() <= kTolerance, line);
  Check(h.untouched(), "fading voices get no share");
}

void NoCarriers() {
  printf("no carriers (other models, or fading voices only):\n");
  h.Init();
  bool carrier[6] = { false, false, false, false, false, false };
  int32_t order[6] = { 0, 1, 2, 3, 4, 5 };
  for (int b = 0; b < 500; ++b) h.Block(order, 6, carrier);
  Check(h.bus().runs() == 0 && h.bus().clears() == 0, "the reverb never runs");
  Check(h.untouched(), "no voice gets a share");
}

void Gaps() {
  printf("gaps with nothing rendered:\n");
  h.Init();
  bool carrier[6] = { true, false, false, false, false, false };
  int32_t order[1] = { 0 };
  for (int b = 0; b < 200; ++b) h.Block(order, 1, carrier);
  h.Pause(4000000);   // 10 ms: the tail carries on
  for (int b = 0; b < 200; ++b) h.Block(order, 1, carrier);
  Check(h.bus().clears() == 1, "10 ms: no restart");
  h.Pause(40000000);   // 100 ms: restarts empty
  for (int b = 0; b < 200; ++b) h.Block(order, 1, carrier);
  Check(h.bus().clears() == 2, "100 ms: restarts empty");
  char line[96];
  snprintf(line, sizeof(line), "matches the reference throughout (worst %d LSB)", h.worst_steady());
  Check(h.worst_steady() <= kTolerance, line);
}

}  // namespace

int main() {
  SteadyShuffled();
  JoinAndLeave();
  JoinNoSwell();
  Steals();
  NoCarriers();
  Gaps();
  printf("%s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
