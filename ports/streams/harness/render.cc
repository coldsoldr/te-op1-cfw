// Streams harness (host): renders the scenarios through the float reference
// (upstream processors at 31089 Hz + ideal VCA/SVF) or the fixed-point port
// (44.1 kHz, 64-sample blocks), and compares the two.
//
//   render input DIR        the dry inputs                  -> DIR/<name>.wav
//   render ref DIR          reference                       -> DIR/<name>.wav, .ctl
//   render port DIR         port                            -> DIR/<name>.wav, .ctl
//   render compare REF PORT output SNR and level, VCA gain and cutoff tracking
//
// .ctl files: float32 pairs per frame (VCA gain in dB with the output trim;
// cutoff in octaves above 20 Hz, -1 when the filter is bypassed). The port's
// codes are converted with the reference's exact mapping, so the traces
// compare the processors (retiming) alone.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "harness/ref_model.h"
#include "harness/scenarios.h"
#include "harness/signals.h"
#include "src/core/streams_core.h"

#include "../../rings/harness/wav.h"

using namespace harness;

namespace {

void MakeInput(const Scenario& s, std::vector<int16_t>* l, std::vector<int16_t>* r) {
  Signal signal;
  signal.Init(s.material, 44100, s.stereo);
  size_t n = static_cast<size_t>(kSeconds) * 44100;
  n -= n % op1streams::kBlockSize;
  l->resize(n);
  r->resize(n);
  for (size_t i = 0; i < n; ++i) signal.Next(&(*l)[i], &(*r)[i]);
}

void WriteWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r) {
  FloatWavWriter w;
  if (!w.Open(path.c_str(), 44100)) {
    fprintf(stderr, "can't write %s\n", path.c_str());
    return;
  }
  w.Write(l.data(), r.data(), l.size());
}

void WriteCtl(const std::string& path, const std::vector<float>& g, const std::vector<float>& c) {
  FILE* fp = fopen(path.c_str(), "wb");
  if (!fp) return;
  for (size_t i = 0; i < g.size(); ++i) {
    fwrite(&g[i], 4, 1, fp);
    fwrite(&c[i], 4, 1, fp);
  }
  fclose(fp);
}

bool ReadWav(const std::string& path, std::vector<float>* l, std::vector<float>* r) {
  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp) return false;
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 44, SEEK_SET);  // FloatWavWriter's fixed header
  size_t frames = static_cast<size_t>(size - 44) / 8;
  l->resize(frames);
  r->resize(frames);
  for (size_t i = 0; i < frames; ++i) {
    if (fread(&(*l)[i], 4, 1, fp) != 1 || fread(&(*r)[i], 4, 1, fp) != 1) break;
  }
  fclose(fp);
  return true;
}

bool ReadCtl(const std::string& path, std::vector<float>* g, std::vector<float>* c) {
  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp) return false;
  float pair[2];
  while (fread(pair, 4, 2, fp) == 2) {
    g->push_back(pair[0]);
    c->push_back(pair[1]);
  }
  fclose(fp);
  return true;
}

int RenderInput(const std::string& dir) {
  for (int k = 0; k < kNumScenarios; ++k) {
    const Scenario& s = kScenarios[k];
    std::vector<int16_t> xl, xr;
    MakeInput(s, &xl, &xr);
    std::vector<float> l(xl.size()), r(xr.size());
    for (size_t i = 0; i < xl.size(); ++i) {
      l[i] = xl[i] / 32768.0f;
      r[i] = xr[i] / 32768.0f;
    }
    WriteWav(dir + "/" + s.name + ".wav", l, r);
  }
  return 0;
}

int RenderRef(const std::string& dir) {
  static RefEffect ref;
  for (int k = 0; k < kNumScenarios; ++k) {
    const Scenario& s = kScenarios[k];
    Controls c;
    ScenarioControls(s, &c);
    std::vector<int16_t> xl, xr;
    MakeInput(s, &xl, &xr);
    RefOutput out;
    ref.Render(c, s.stereo, xl, xr, &out);
    WriteWav(dir + "/" + s.name + ".wav", out.l, out.r);
    WriteCtl(dir + "/" + s.name + ".ctl", out.gain_db, out.cutoff_oct);
    printf("ref  %-22s %zu frames\n", s.name, out.l.size());
  }
  return 0;
}

int RenderPort(const std::string& dir) {
  static op1streams::StreamsCore core;
  for (int k = 0; k < kNumScenarios; ++k) {
    const Scenario& s = kScenarios[k];
    Controls c;
    ScenarioControls(s, &c);
    std::vector<int16_t> xl, xr;
    MakeInput(s, &xl, &xr);
    const size_t n = xl.size();
    std::vector<float> l(n), r(n), g(n), co(n);
    std::vector<double> gcodes(n), fcodes(n);
    core.Init();
    const int f = c.function;
    const bool bypass = f == op1streams::PROCESSOR_FUNCTION_COMPRESSOR ||
        (f == op1streams::PROCESSOR_FUNCTION_FOLLOWER && c.alternate);
    const bool unity = f == op1streams::PROCESSOR_FUNCTION_FILTER_CONTROLLER;
    const double response = f == op1streams::PROCESSOR_FUNCTION_COMPRESSOR ? 0.0 : c.response / 32767.0;
    const double trim = std::pow(2.0, (c.output - 16384) / 8192.0);
    std::vector<int16_t> bl(op1streams::kBlockSize), br(op1streams::kBlockSize);
    for (size_t start = 0; start < n; start += op1streams::kBlockSize) {
      for (int i = 0; i < op1streams::kBlockSize; ++i) {
        bl[i] = xl[start + i];
        br[i] = xr[start + i];
      }
      if (s.stereo) {
        core.ProcessStereo(c, bl.data(), br.data(), op1streams::kBlockSize);
      } else {
        core.ProcessMono(c, bl.data(), op1streams::kBlockSize);
        br = bl;
      }
      for (int i = 0; i < op1streams::kBlockSize; ++i) {
        l[start + i] = bl[i] / 32768.0f;
        r[start + i] = br[i] / 32768.0f;
        gcodes[start + i] = core.gain_codes()[i];
        fcodes[start + i] = core.cutoff_codes()[i];
        double code = unity ? 32767.0 : core.gain_codes()[i];
        double gg = RefVcaGain(code, response) * trim;
        if (gg > 8.0) gg = 8.0;
        g[start + i] = static_cast<float>(gg > 1e-6 ? 20.0 * std::log10(gg) : -120.0);
        if (bypass) {
          co[start + i] = -1.0f;
        } else {
          double fc = op1streams::kCutoffMinHz *
              std::pow(2.0, op1streams::kCutoffOctaves * core.cutoff_codes()[i] / 65536.0);
          double cap = op1streams::kCutoffMaxRatio * op1streams::kSampleRate;
          co[start + i] = static_cast<float>(std::log2((fc > cap ? cap : fc) / op1streams::kCutoffMinHz));
        }
      }
    }
    WriteWav(dir + "/" + s.name + ".wav", l, r);
    WriteCtl(dir + "/" + s.name + ".ctl", g, co);
    // The port's own codes through the float VCA/SVF: the error of the
    // fixed-point audio path alone.
    RefOutput fl;
    RefEffect::Analog(c, xl, xr, gcodes, fcodes, &fl);
    double sig = 0, err = 0;
    for (size_t i = 0; i < n; ++i) {
      sig += fl.l[i] * fl.l[i] + fl.r[i] * fl.r[i];
      double el = l[i] - fl.l[i], er = r[i] - fl.r[i];
      err += el * el + er * er;
    }
    printf("port %-22s %zu frames, audio path vs float on the same codes: SNR %.1f dB\n",
           s.name, n, err > 0 ? 10 * std::log10(sig / err) : 999.0);
  }
  return 0;
}

double Percentile(std::vector<double> v, double p) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  return v[static_cast<size_t>(p * (v.size() - 1))];
}

int Compare(const std::string& ref_dir, const std::string& port_dir) {
  printf("%-22s %8s %8s | %22s | %22s\n", "scenario", "SNR dB", "level", "VCA gain |d| dB",
         "cutoff |d| octaves");
  printf("%-22s %8s %8s | %10s %11s | %10s %11s\n", "", "", "dB", "rms", "p99", "rms", "p99");
  int missing = 0;
  for (int k = 0; k < kNumScenarios; ++k) {
    const Scenario& s = kScenarios[k];
    std::vector<float> rl, rr, pl, pr, rg, rc, pg, pc;
    if (!ReadWav(ref_dir + "/" + s.name + ".wav", &rl, &rr) ||
        !ReadWav(port_dir + "/" + s.name + ".wav", &pl, &pr) ||
        !ReadCtl(ref_dir + "/" + s.name + ".ctl", &rg, &rc) ||
        !ReadCtl(port_dir + "/" + s.name + ".ctl", &pg, &pc)) {
      printf("%-22s missing files\n", s.name);
      ++missing;
      continue;
    }
    size_t n = std::min(rl.size(), pl.size());
    double sig = 0, err = 0, ps = 0;
    for (size_t i = 0; i < n; ++i) {
      sig += rl[i] * rl[i] + rr[i] * rr[i];
      ps += pl[i] * pl[i] + pr[i] * pr[i];
      double el = pl[i] - rl[i], er = pr[i] - rr[i];
      err += el * el + er * er;
    }
    double snr = err > 0 ? 10 * std::log10(sig / err) : 999.0;
    double level = 10 * std::log10((ps + 1e-20) / (sig + 1e-20));
    std::vector<double> dg, dc;
    size_t nc = std::min(rg.size(), pg.size());
    for (size_t i = 0; i < nc; ++i) {
      if (rg[i] > -40.0f) dg.push_back(std::fabs(pg[i] - rg[i]));
      if (rc[i] >= 0.0f && pc[i] >= 0.0f) dc.push_back(std::fabs(pc[i] - rc[i]));
    }
    double gr = 0, cr = 0;
    for (double v : dg) gr += v * v;
    for (double v : dc) cr += v * v;
    gr = dg.empty() ? 0 : std::sqrt(gr / dg.size());
    cr = dc.empty() ? 0 : std::sqrt(cr / dc.size());
    printf("%-22s %8.1f %8.2f | %10.3f %11.3f |", s.name, snr, level, gr, Percentile(dg, 0.99));
    if (dc.empty()) printf(" %10s %11s\n", "bypass", "");
    else printf(" %10.3f %11.3f\n", cr, Percentile(dc, 0.99));
  }
  return missing ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 3 && !strcmp(argv[1], "input")) return RenderInput(argv[2]);
  if (argc == 3 && !strcmp(argv[1], "ref")) return RenderRef(argv[2]);
  if (argc == 3 && !strcmp(argv[1], "port")) return RenderPort(argv[2]);
  if (argc == 4 && !strcmp(argv[1], "compare")) return Compare(argv[2], argv[3]);
  fprintf(stderr, "usage: render input|ref|port DIR | render compare REFDIR PORTDIR\n");
  return 2;
}
