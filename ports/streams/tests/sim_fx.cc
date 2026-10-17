// The OP-1 entry points (op1/streams_fx.cc) called as the OP-1 calls an
// effect: init(this, position), then 64-sample process calls with the
// position's 8 parameters, on the synth (mono) and master (stereo) positions
// at once. Built with -DOP1_SIM for bfin-elf-run and for the host; "CHK"
// lines must match (make sim-fx), "CYC" lines are Blackfin cycles per call.
//
// Timeline (4 s, 689 calls per second per position):
//   0.0 s  synth: plucked vactrol (marker set); master: compressor, split
//          times; encoders static
//   1.0 s  synth: settings word changes to the follower, BP resonant;
//          master: limiter (MOD at maximum)
//   2.0 s  encoders sweep every call (SHAPE, MOD, SENSE; MIX on master)
//   2.5 s  a gap: 30 calls skipped and > 5 ms elapsed (the effect was off):
//          both positions must restart fresh
//   3.0 s  synth: fx_params as a factory preset (8000 in [4..7]): defaults
//   3.5 s  synth init(position = drum), as when the drum track takes over

#include <stdio.h>
#include <string.h>

#include "harness/signals.h"
#include "op1/streams_fx.h"

#include "../../rings/harness/cycles.h"

using namespace op1streams;
using namespace harness;

namespace {

const int32_t kCallsPerSecond = 689;
const uint32_t kCyclesPerCall = 400000000u / 689;

uint32_t Fnv(uint32_t h, uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    h ^= (v >> (8 * i)) & 0xff;
    h *= 16777619u;
  }
  return h;
}

void BurnCycles(uint32_t cycles) {
#ifdef __bfin__
  uint64_t start = ReadCycles();
  while (ReadCycles() - start < cycles) { }
#else
  g_fake_cycles += cycles;
#endif
}

}  // namespace

int main() {
  EnableCycleCounter();
  // Fake L1 objects (never read by our code) and parameter blocks.
  static uint32_t mono_object[36], stereo_object[100];
  int16_t synth[8], master[8];

  Controls cs, cm;
  DefaultControls(&cs);
  cs.function = PROCESSOR_FUNCTION_VACTROL; cs.alternate = 1; cs.detector = DETECTOR_ENVELOPE;
  cs.shape = 9000; cs.mod = 26000; cs.sense = 12000; cs.mix = 32767;
  DefaultControls(&cm);
  cm.function = PROCESSOR_FUNCTION_COMPRESSOR; cm.split = 1;
  cm.shape = 10000; cm.mod = 20000; cm.sense = 0; cm.mix = 32767;
  cm.attack = 6000; cm.decay = 14000;

  // Round trip of the parameter encoding.
  {
    int16_t p[8] = { EncoderRaw(0, cs.shape), EncoderRaw(1, cs.mod), EncoderRaw(2, cs.sense),
                     EncoderRaw(3, cs.mix), EncodeSettings(cs, 1),
                     cs.attack, cs.decay, EncodeResponseOutput(cs) };
    Controls d;
    DecodeControls(p, &d);
    bool same = d.function == cs.function && d.alternate == cs.alternate &&
        d.split == cs.split && d.filter_mode == cs.filter_mode && d.detector == cs.detector &&
        d.shape - cs.shape < 3 && cs.shape - d.shape < 3 && d.mod - cs.mod < 3 &&
        cs.mod - d.mod < 3 && d.attack == cs.attack && d.response == cs.response &&
        d.output == cs.output;
    int16_t f[8] = { 100, 200, 300, 400, 8000, 8000, 8000, 8000 };
    Controls e, def;
    DecodeControls(f, &e);
    DefaultControls(&def);
    bool factory = !IsStreamsSettings(f) && e.function == def.function && e.attack == def.attack;
    // Every word in 0..32767 (the FX ranges of knobs 4..7).
    bool in_range = EncodeSettings(cs, 1) >= 0 && EncodeResponseOutput(cs) >= 0;
    Controls hi = cs;
    hi.response = 32767; hi.output = 32767; hi.function = 5; hi.alternate = 1; hi.split = 1;
    hi.filter_mode = 3; hi.detector = 1;
    in_range = in_range && EncodeSettings(hi, 1) >= 0 && EncodeResponseOutput(hi) >= 0;
    // Encoder normalisation: ends map to 0 and 32767.
    bool ends = true;
    for (int k = 0; k < 4; ++k) {
      ends = ends && NormaliseEncoder(k, kEncoderMin[k]) == 0 &&
          NormaliseEncoder(k, kEncoderMax[k]) == 32767;
    }
    printf("CHK encoding round trip %s, factory 8000s give defaults %s, words in 0..32767 %s,"
           " encoder ends %s, settings word %04x\n",
           same ? "ok" : "FAILED", factory ? "ok" : "FAILED", in_range ? "ok" : "FAILED",
           ends ? "ok" : "FAILED",
           static_cast<unsigned>(static_cast<uint16_t>(EncodeSettings(cs, 1))));
  }

  StreamsFxInit(mono_object, POSITION_SYNTH);
  StreamsFxInit(stereo_object, POSITION_MASTER);

  Signal mono_signal, stereo_signal;
  mono_signal.Init(MAT_RIFF, 44100, false);
  stereo_signal.Init(MAT_MIX, 44100, true);
  int16_t buf[64], l[64], r[64], dummy;
  uint32_t hm = 2166136261u, hs = 2166136261u;
  CycleStats mono_stats, stereo_stats;
  const int32_t total = 4 * kCallsPerSecond;
  for (int32_t call = 0; call < total; ++call) {
    const int32_t t = call * 1000 / kCallsPerSecond;  // ms
    if (t >= 1000 && t < 3000) {
      cs.function = PROCESSOR_FUNCTION_FOLLOWER; cs.alternate = 0;
      cs.filter_mode = FILTER_BP_RESONANT; cs.sense = 6000;
      cm.mod = 32767;
    }
    if (t >= 2000) {
      cs.shape = static_cast<int16_t>((call * 37) & 32767);
      cs.mod = static_cast<int16_t>(16000 + ((call * 11) & 16383));
      cm.mix = static_cast<int16_t>(16384 + ((call * 23) & 16383));
    }
    synth[0] = EncoderRaw(0, cs.shape); synth[1] = EncoderRaw(1, cs.mod);
    synth[2] = EncoderRaw(2, cs.sense); synth[3] = EncoderRaw(3, cs.mix);
    synth[4] = EncodeSettings(cs, 0); synth[5] = cs.attack; synth[6] = cs.decay;
    synth[7] = EncodeResponseOutput(cs);
    if (t >= 3000) {
      synth[4] = synth[5] = synth[6] = synth[7] = 8000;  // a factory preset
    }
    master[0] = EncoderRaw(0, cm.shape); master[1] = EncoderRaw(1, cm.mod);
    master[2] = EncoderRaw(2, cm.sense); master[3] = EncoderRaw(3, cm.mix);
    master[4] = EncodeSettings(cm, 0); master[5] = cm.attack; master[6] = cm.decay;
    master[7] = EncodeResponseOutput(cm);
    if (call == total * 7 / 8) {
      StreamsFxInit(mono_object, POSITION_DRUM);
    }
    if (call == total * 5 / 8) {
      for (int k = 0; k < 30; ++k) {  // the effect is off: no calls
        for (int i = 0; i < 64; ++i) {
          mono_signal.Next(&buf[i], &dummy);
          stereo_signal.Next(&l[i], &r[i]);
        }
      }
      BurnCycles(3000000);
    }

    for (int i = 0; i < 64; ++i) {
      mono_signal.Next(&buf[i], &dummy);
      stereo_signal.Next(&l[i], &r[i]);
    }
    uint64_t t0 = ReadCycles();
    StreamsFxMono(mono_object, buf, synth);
    uint64_t t1 = ReadCycles();
    StreamsFxStereo(stereo_object, l, r, master);
    uint64_t t2 = ReadCycles();
    if (call > 0) {
      mono_stats.Add(t1 - t0);
      stereo_stats.Add(t2 - t1);
    }
#ifndef __bfin__
    g_fake_cycles += kCyclesPerCall;
#endif
    for (int i = 0; i < 64; ++i) {
      hm = Fnv(hm, static_cast<uint16_t>(buf[i]));
      hs = Fnv(hs, static_cast<uint16_t>(l[i]) | (static_cast<uint32_t>(static_cast<uint16_t>(r[i])) << 16));
    }
    if ((call + 1) % (kCallsPerSecond / 2) == 0) {
      printf("CHK t=%4d ms mono %08lx stereo %08lx restarts %lu\n", static_cast<int>(t),
             static_cast<unsigned long>(hm), static_cast<unsigned long>(hs),
             static_cast<unsigned long>(g_restarts));
    }
  }
#ifdef __bfin__
  mono_stats.Print("CYC StreamsFxMono", 64);
  stereo_stats.Print("CYC StreamsFxStereo", 64);
#endif
  return 0;
}
