// op1/voice_allocator.h: Rings polyphony on the OP-1's six voices.
//
//   make test-alloc   (exits 1 on a failure)

#include <cstdint>
#include <cstdio>

#include "op1/coefficient_budget.h"
#include "op1/settings.h"
#include "op1/voice_allocator.h"

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    ++failures;
    printf("FAIL: %s\n", what);
  }
}

// Renders `samples` samples on every voice (what the engine does per call).
void Advance(op1::VoiceAllocator& a, int32_t samples) {
  for (int32_t v = 0; v < op1::VoiceAllocator::kNumVoices; ++v) {
    if (a.silent(v)) continue;
    a.AdvanceFade(v, samples);
    a.Rendered(v);
  }
}

}  // namespace

int main() {
  using op1::VoiceAllocator;
  VoiceAllocator a;

  // "All": every OP-1 voice keeps its own resonator.
  a.Init();
  for (int32_t v = 0; v < 6; ++v) {
    VoiceAllocator::NoteOnAction n = a.NoteOn(v, op1::POLY_ALL);
    Check(n.copy_from == -1 && !n.reset, "all: no stealing");
  }
  for (int32_t v = 0; v < 6; ++v) Check(!a.fading(v) && !a.silent(v), "all: nothing fades");

  // Polyphony 1: each note takes over the previous one's resonator.
  a.Init();
  VoiceAllocator::NoteOnAction n = a.NoteOn(0, op1::POLY_1);
  Check(n.copy_from == -1 && !n.reset, "poly 1: first note is fresh");
  Advance(a, 128);   // voice 0 sounds
  n = a.NoteOn(1, op1::POLY_1);
  Check(n.copy_from == 0, "poly 1: second note takes over voice 0");
  Check(a.fading(0) && !a.fading(1), "poly 1: voice 0 fades");
  Check(a.fade_gain(0) == VoiceAllocator::kFullGain, "fade starts at full gain");
  for (int32_t i = 0; i < 3; ++i) Advance(a, 128);
  Check(a.fading(0) && !a.silent(0), "fade lasts 512 samples (not over after 384)");
  Advance(a, 128);
  Check(!a.fading(0) && a.silent(0), "fade over after 512 samples: voice 0 silent");
  n = a.NoteOn(0, op1::POLY_1);
  Check(n.copy_from == 1 && !n.reset && !a.silent(0), "a silent voice struck again takes over");

  // Polyphony 2 and 4: steal the oldest only once all are busy.
  a.Init();
  Check(a.NoteOn(0, op1::POLY_2).copy_from == -1, "poly 2: voice 0 fresh");
  Check(a.NoteOn(1, op1::POLY_2).copy_from == -1, "poly 2: voice 1 fresh");
  Check(a.NoteOn(2, op1::POLY_2).copy_from == 0, "poly 2: voice 2 steals the oldest (0)");
  Check(a.NoteOn(3, op1::POLY_2).copy_from == 1, "poly 2: voice 3 steals the next oldest (1)");
  Check(a.NoteOn(2, op1::POLY_2).copy_from == -1, "poly 2: re-striking an active voice steals nothing");
  Check(a.num_active() == 2 && a.active(0) == 3 && a.active(1) == 2, "poly 2: re-struck voice is newest");
  a.Init();
  for (int32_t v = 0; v < 4; ++v) Check(a.NoteOn(v, op1::POLY_4).copy_from == -1, "poly 4: four fresh");
  Check(a.NoteOn(4, op1::POLY_4).copy_from == 0, "poly 4: fifth steals the oldest");

  // At most two fades at full length; a third shortens the others.
  a.Init();
  a.NoteOn(0, op1::POLY_1);
  Advance(a, 128);
  a.NoteOn(1, op1::POLY_1);   // 0 fades
  Advance(a, 1);
  a.NoteOn(2, op1::POLY_1);   // 1 fades
  Check(a.fading(0) && a.fading(1), "two fades");
  Check(a.fade_step(1) == VoiceAllocator::kFullGain / VoiceAllocator::kFadeSamples, "two fades: full length");
  Advance(a, 1);
  a.NoteOn(3, op1::POLY_1);   // 2 fades: 0 and 1 are shortened
  Advance(a, 128);
  Check(a.silent(0) && a.silent(1), "older fades finish within 128 samples");
  Check(a.fading(2) && !a.silent(2), "newest fade keeps its full length");

  // A voice stolen before it rendered goes silent at once (a chord).
  a.Init();
  for (int32_t v = 0; v < 6; ++v) a.NoteOn(v, op1::POLY_1);
  for (int32_t v = 0; v < 5; ++v) Check(a.silent(v) && !a.fading(v), "chord: unsounded voices go silent");
  Check(!a.silent(5) && a.num_active() == 1, "chord: the last note plays");

  // A fading voice struck again stops fading.
  a.Init();
  a.NoteOn(0, op1::POLY_1);
  Advance(a, 128);
  a.NoteOn(1, op1::POLY_1);   // 0 fades
  Advance(a, 128);
  a.NoteOn(0, op1::POLY_1);   // 0 takes over 1; 1 fades
  Check(!a.fading(0) && a.fading(1), "re-struck voice stops fading");

  // A polyphony change starts a fresh voice list.
  a.Init();
  a.NoteOn(0, op1::POLY_1);
  Check(a.NoteOn(1, op1::POLY_2).copy_from == -1, "poly change: no steal from the old list");
  // A silent voice struck in "all" is reset (its state is frozen).
  a.Init();
  a.NoteOn(0, op1::POLY_1);
  Advance(a, 128);
  a.NoteOn(1, op1::POLY_1);
  for (int32_t i = 0; i < 4; ++i) Advance(a, 128);
  Check(a.silent(0), "voice 0 silent");
  n = a.NoteOn(0, op1::POLY_ALL);
  Check(n.copy_from == -1 && n.reset, "silent voice struck in 'all' is reset");

  // Settings encoding: 0 is modal / "all"; values are setting x 4096.
  const int16_t zero[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  Check(op1::ModelSetting(zero) == 0 && op1::PolySetting(zero) == op1::POLY_ALL, "0 = modal, all");
  int16_t k[8] = { 0, 0, 0, 0, op1::EncodeSetting(3), op1::EncodeSetting(op1::POLY_4), 0, 0 };
  Check(op1::ModelSetting(k) == 3 && op1::PolySetting(k) == op1::POLY_4, "encode/decode");
  k[4] = 32767;
  k[5] = -5;
  Check(op1::ModelSetting(k) == 5 && op1::PolySetting(k) == op1::POLY_ALL, "out-of-range values clamp");

  // Click stepping: models cycle through the implemented ones, polyphony
  // through all -> 1 -> 2 -> 4.
  Check(op1::StepModel(0, 1) == 1 && op1::StepModel(1, 1) == 2 && op1::StepModel(2, 1) == 3 &&
        op1::StepModel(3, 1) == 4 && op1::StepModel(4, 1) == 5 && op1::StepModel(5, 1) == 0,
        "model: 0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 0");
  Check(op1::StepModel(0, -1) == 5 && op1::StepModel(3, -1) == 2, "model: backwards");
  Check(op1::StepModel(7, 1) == 0, "model: outside the cycle steps to the first");
  Check(op1::StepPoly(op1::POLY_ALL, 1) == op1::POLY_1 && op1::StepPoly(op1::POLY_1, 1) == op1::POLY_2 &&
        op1::StepPoly(op1::POLY_2, 1) == op1::POLY_4 && op1::StepPoly(op1::POLY_4, 1) == op1::POLY_ALL,
        "poly: all -> 1 -> 2 -> 4 -> all");
  Check(op1::StepPoly(op1::POLY_ALL, -1) == op1::POLY_4, "poly: backwards");

  // Engines: 0 (every stock preset) is stock phase; the orange click
  // toggles phase <-> Rings either way.
  Check(op1::EngineSetting(zero) == op1::ENGINE_PHASE, "0 = stock phase");
  k[6] = op1::EncodeSetting(op1::ENGINE_RINGS);
  Check(op1::EngineSetting(k) == op1::ENGINE_RINGS, "engine: encode/decode");
  k[6] = 32767;
  Check(op1::EngineSetting(k) == op1::ENGINE_RINGS, "engine: out of range clamps");
  Check(op1::StepEngine(op1::ENGINE_PHASE, 1) == op1::ENGINE_RINGS &&
        op1::StepEngine(op1::ENGINE_RINGS, 1) == op1::ENGINE_PHASE &&
        op1::StepEngine(op1::ENGINE_PHASE, -1) == op1::ENGINE_RINGS, "engine: phase <-> rings");

  // A voice that switches to stock phase leaves Rings' list: nothing steals
  // it, and it's reset when next struck as Rings.
  a.Init();
  a.NoteOn(0, op1::POLY_1);
  Advance(a, 128);
  a.Forget(0);
  Check(a.num_active() == 0 && !a.fading(0), "forget: out of the list, not fading");
  n = a.NoteOn(1, op1::POLY_1);
  Check(n.copy_from == -1, "forget: the next Rings note steals nothing");
  n = a.NoteOn(0, op1::POLY_ALL);
  Check(n.reset, "forget: struck again as Rings, it's reset");
  a.Init();
  a.NoteOn(0, op1::POLY_1);
  Advance(a, 128);
  a.NoteOn(1, op1::POLY_1);   // voice 0 fades
  a.Forget(0);
  Check(!a.fading(0), "forget: a fading voice stops fading");

  // Coefficient budget (op1/coefficient_budget.h): one block = one render
  // per sounding voice.
  {
    op1::CoefficientBudget budget;
    // Six 16-mode voices turning, rendered in index order (as the OP-1
    // does): two updates per call, each voice every third call, also after
    // the sounding voices change (a block starting at voice 3).
    budget.Init();
    int32_t grants[6] = { 0, 0, 0, 0, 0, 0 };
    bool every_third = true, two_per_call = true;
    int32_t last[6] = { -9, -9, -9, -9, -9, -9 };
    for (int32_t v = 3; v < 6; ++v) {
      budget.Allow(v);
      budget.Report(v, 16, false);
    }
    for (int32_t b = 0; b < 200; ++b) {
      int32_t n = 0;
      for (int32_t v = 0; v < 6; ++v) {
        if (budget.Allow(v)) {
          ++n;
          ++grants[v];
          if (b > 3 && b - last[v] != 3) every_third = false;
          last[v] = b;
        }
        budget.Report(v, 16, false);
      }
      if (b > 1 && n != 2) two_per_call = false;
    }
    Check(two_per_call, "budget: six 16-mode voices, two updates per call");
    Check(every_third, "budget: each voice every third call");
    Check(grants[0] >= 66 && grants[5] >= 66, "budget: fair");

    // In another render order blocks are shorter, never longer: each voice
    // still updates in at least 30% of calls and never waits more than 16
    // calls (23 ms, half the knob smoothing). Measured: 43-48%, 8-12 calls.
    budget.Init();
    bool no_starving = true;
    int32_t since[6] = { 0, 0, 0, 0, 0, 0 };
    int32_t updates[6] = { 0, 0, 0, 0, 0, 0 };
    uint32_t rng = 5;
    for (int32_t b = 0; b < 200; ++b) {
      int32_t order[6] = { 0, 1, 2, 3, 4, 5 };
      for (int32_t k = 5; k > 0; --k) {
        rng = rng * 1664525u + 1013904223u;
        const int32_t j = static_cast<int32_t>((rng >> 16) % (k + 1));
        const int32_t t = order[k]; order[k] = order[j]; order[j] = t;
      }
      for (int32_t k = 0; k < 6; ++k) {
        const int32_t v = order[k];
        const bool allowed = budget.Allow(v);
        since[v] = allowed ? 0 : since[v] + 1;
        updates[v] += allowed;
        if (b > 3 && since[v] > 16) no_starving = false;
        budget.Report(v, 16, false);
      }
    }
    for (int32_t v = 0; v < 6; ++v) {
      if (updates[v] < 60) no_starving = false;
    }
    Check(no_starving, "budget: shuffled order, no voice starves");

    // A single 60-mode voice (polyphony 1) always updates; two alternate.
    budget.Init();
    bool always = true;
    for (int32_t b = 0; b < 50; ++b) {
      const bool ok = budget.Allow(2);
      if (b > 0 && !ok) always = false;
      budget.Report(2, 60, false);
    }
    Check(always, "budget: one 60-mode voice updates every block");
    budget.Init();
    bool alternate = true;
    for (int32_t b = 0; b < 50; ++b) {
      const bool a0 = budget.Allow(0);
      budget.Report(0, 60, false);
      const bool a1 = budget.Allow(1);
      budget.Report(1, 60, false);
      if (b > 0 && a0 == a1) alternate = false;
    }
    Check(alternate, "budget: two 60-mode voices alternate");

    // Still voices get nothing; a voice that starts moving waits one block,
    // then is granted.
    budget.Init();
    for (int32_t b = 0; b < 5; ++b) {
      budget.Allow(0);
      budget.Report(0, 0, false);
    }
    const bool first = budget.Allow(0);
    budget.Report(0, 16, false);
    const bool second = budget.Allow(0);
    Check(!first && second, "budget: new demand waits one block");

    // While a voice fades (after a steal), nobody updates; afterwards the
    // demand is granted again.
    budget.Init();
    for (int32_t b = 0; b < 3; ++b) {
      budget.Allow(0);
      budget.Report(0, 60, false);
    }
    bool none_while_fading = true;
    for (int32_t b = 0; b < 4; ++b) {
      if (budget.Allow(0) && b > 0) none_while_fading = false;
      budget.Report(0, 60, false);
      if (budget.Allow(1)) none_while_fading = false;
      budget.Report(1, 60, true);
    }
    budget.Allow(0);
    budget.Report(0, 60, false);
    const bool after = budget.Allow(0);
    Check(none_while_fading && after, "budget: no updates while a voice fades, then again");
  }

  printf("%s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
