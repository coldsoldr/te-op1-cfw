// The fixed-point port against the float copy of upstream, side by side with
// the same seeds: every harness scenario, plus a bias sweep per T model and
// randomized settings, over many seeds and long runs.
//
// Per group it reports:
//   - runs whose gates (T1, T2, T3) are identical on every sample, and the
//     earliest sample where any run's gates first differ;
//   - the voltages: max |port - ref| over samples whose gates agree, and the
//     fraction of samples more than 1 mV apart;
//   - distributions, ref vs port: rising edges per channel, mean and std of
//     each voltage channel, and the L1 distance of the X histograms (64 bins
//     over -5..5 V), which stay meaningful even if the runs part.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define MARBLES_NS op1marbles
#include "upstream/random/random_generator.h"
#include "upstream/random/random_stream.h"
#include "upstream/random/t_generator.h"
#include "upstream/random/x_y_generator.h"
#include "harness/float_driver.h"
#include "harness/fixed_driver.h"

using namespace harness;

static FloatDriver ref;
static FixedDriver port;

struct Stats {
  long samples, edges[3], hist[4][64];
  double sum[4], sum2[4];
  void Clear() { memset(this, 0, sizeof(*this)); }
  void Add(const Frame& f, uint8_t previous_gates) {
    ++samples;
    for (int k = 0; k < 3; ++k) {
      if ((f.gates >> k & 1) && !(previous_gates >> k & 1)) ++edges[k];
    }
    for (int c = 0; c < 4; ++c) {
      double v = f.voltage[c];
      sum[c] += v;
      sum2[c] += v * v;
      int b = static_cast<int>((v + 5.0) / 10.0 * 64.0);
      if (b < 0) b = 0;
      if (b > 63) b = 63;
      ++hist[c][b];
    }
  }
};

struct Group {
  const char* name;
  int runs, identical_runs;
  long first_divergence;  // earliest sample index with differing gates, -1 if none
  double max_dv;
  long samples, samples_over_1mv;
  long new_values, new_values_differing;  // ref voltage steps (|step| > 1 mV)
  Stats r, p;
};

static uint32_t rng = 777;
static uint32_t Rand() {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}
static float RandUnit() { return static_cast<float>(Rand() % 65537) / 65536.0f; }

static void Run(Group* g, const Scenario& s, double seconds) {
  long ticks = static_cast<long>(seconds * kTickRate);
  ref.Init(s.seed);
  port.Init(s.seed);
  Frame fr[kSubsteps], fp[kSubsteps];
  uint8_t prev_r = 0, prev_p = 0;
  float prev_v[4] = { 0, 0, 0, 0 };
  bool identical = true;
  for (long t = 0; t < ticks; ++t) {
    Params p = At(s, ticks > 1 ? static_cast<double>(t) / (ticks - 1) : 0.0);
    ref.Process(p, fr);
    port.Process(p, fp);
    for (int k = 0; k < kSubsteps; ++k) {
      long index = t * kSubsteps + k;
      g->r.Add(fr[k], prev_r);
      g->p.Add(fp[k], prev_p);
      prev_r = fr[k].gates;
      prev_p = fp[k].gates;
      ++g->samples;
      if (fr[k].gates != fp[k].gates) {
        if (identical && (g->first_divergence < 0 || index < g->first_divergence)) {
          g->first_divergence = index;
        }
        identical = false;
      }
      bool over = false;
      for (int c = 0; c < 4; ++c) {
        double dv = std::fabs(static_cast<double>(fr[k].voltage[c]) - fp[k].voltage[c]);
        if (dv > 1e-3) over = true;
        if (identical && dv > g->max_dv) g->max_dv = dv;
      }
      if (over) ++g->samples_over_1mv;
      for (int c = 0; c < 4; ++c) {
        if (std::fabs(fr[k].voltage[c] - prev_v[c]) > 1e-3f) {
          ++g->new_values;
          if (std::fabs(static_cast<double>(fr[k].voltage[c]) - fp[k].voltage[c]) > 1e-3) {
            ++g->new_values_differing;
          }
        }
        prev_v[c] = fr[k].voltage[c];
      }
    }
  }
  ++g->runs;
  if (identical) ++g->identical_runs;
}

static void Print(const Group& g) {
  printf("%-22s runs %3d identical-gates %3d  first gate diff %8ld  max|dV| %.2e  >1mV %.5f%%\n",
         g.name, g.runs, g.identical_runs, g.first_divergence, g.max_dv,
         100.0 * g.samples_over_1mv / (g.samples ? g.samples : 1));
  printf("%22s voltage steps %ld, port differs (> 1 mV) at %ld of them\n", "",
         g.new_values, g.new_values_differing);
  printf("%22s edges T1 %ld/%ld T2 %ld/%ld T3 %ld/%ld (ref/port)\n", "",
         g.r.edges[0], g.p.edges[0], g.r.edges[1], g.p.edges[1], g.r.edges[2], g.p.edges[2]);
  const char* lab[4] = { "X1", "X2", "X3", "Y" };
  for (int c = 0; c < 4; ++c) {
    double nr = g.r.samples, np = g.p.samples;
    double mr = g.r.sum[c] / nr, mp = g.p.sum[c] / np;
    double sr = std::sqrt(g.r.sum2[c] / nr - mr * mr), sp = std::sqrt(g.p.sum2[c] / np - mp * mp);
    double l1 = 0.0;
    for (int b = 0; b < 64; ++b) l1 += std::fabs(g.r.hist[c][b] / nr - g.p.hist[c][b] / np);
    printf("%22s %-2s mean %+.5f/%+.5f  std %.5f/%.5f  hist L1 %.2e\n", "", lab[c], mr, mp, sr, sp, l1);
  }
}

static void NewGroup(Group* g, const char* name) {
  memset(g, 0, sizeof(*g));
  g->name = name;
  g->first_divergence = -1;
}

int main(int argc, char** argv) {
  int seeds = argc > 1 ? atoi(argv[1]) : 32;
  double seconds = argc > 2 ? atof(argv[2]) : 300.0;
  printf("Marbles port vs float copy: %d seeds x %.0f s per configuration, %d Marbles "
         "samples per OP-1 tick (%.2f Hz)\n\n", seeds, seconds, kSubsteps, kSampleRate);
  static Group g, total;
  NewGroup(&total, "ALL");
  // 1. Every harness scenario, several seeds.
  for (int i = 0; i < NumScenarios(); ++i) {
    Scenario s = GetScenario(i);
    NewGroup(&g, s.name);
    for (int k = 0; k < seeds; ++k) {
      s.seed = 1000u + 7919u * static_cast<uint32_t>(i) + 104729u * static_cast<uint32_t>(k);
      Run(&g, s, seconds);
    }
    Print(g);
    total.runs += g.runs; total.identical_runs += g.identical_runs;
    total.samples += g.samples; total.samples_over_1mv += g.samples_over_1mv;
    total.new_values += g.new_values; total.new_values_differing += g.new_values_differing;
    if (g.max_dv > total.max_dv) total.max_dv = g.max_dv;
  }
  // 2. Each T model with its bias swept 0..1, randomized X settings.
  static const char* model_names[7] = {
    "sweep_comp_bernoulli", "sweep_clusters", "sweep_drums", "sweep_indep_bernoulli",
    "sweep_divider", "sweep_three_states", "sweep_markov" };
  for (int m = 0; m < 7; ++m) {
    NewGroup(&g, model_names[m]);
    for (int k = 0; k < seeds; ++k) {
      Scenario s = GetScenario(0);
      s.seed = Rand();
      s.p.t_model = m;
      s.p.t_rate = static_cast<float>(static_cast<int>(Rand() % 48) - 12);
      s.p.t_jitter = (k & 1) ? RandUnit() : 0.0f;
      s.p.deja_vu = RandUnit();
      s.p.t_deja_vu = static_cast<int>(Rand() % 3);
      s.p.x_deja_vu = static_cast<int>(Rand() % 3);
      s.p.length = 1 + static_cast<int>(Rand() % 16);
      s.p.x_spread = RandUnit();
      s.p.x_bias = RandUnit();
      s.p.x_steps = RandUnit();
      s.p.x_scale = static_cast<int>(Rand() % 6);
      s.p.x_range = static_cast<int>(Rand() % 3);
      s.p.x_control_mode = static_cast<int>(Rand() % 3);
      s.p.clock_source = static_cast<int>(Rand() % 4);
      s.p.pw_mean = RandUnit();
      s.p.pw_std = (k & 2) ? RandUnit() : 0.0f;
      s.sweep = SW_T_BIAS;
      s.from = 0.0f;
      s.to = 1.0f;
      Run(&g, s, seconds);
    }
    Print(g);
    total.runs += g.runs; total.identical_runs += g.identical_runs;
    total.samples += g.samples; total.samples_over_1mv += g.samples_over_1mv;
    total.new_values += g.new_values; total.new_values_differing += g.new_values_differing;
    if (g.max_dv > total.max_dv) total.max_dv = g.max_dv;
  }
  printf("\nTOTAL: %d runs, %d with identical gates on every sample; max |dV| %.2e V; "
         "%.5f%% of samples > 1 mV apart; %ld of %ld voltage steps differ (> 1 mV)\n",
         total.runs, total.identical_runs, total.max_dv,
         100.0 * total.samples_over_1mv / (total.samples ? total.samples : 1),
         total.new_values_differing, total.new_values);
  return 0;
}
