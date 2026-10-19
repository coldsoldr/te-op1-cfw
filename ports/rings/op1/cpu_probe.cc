// CPU probe v3 (-DOP1_CPU_PROBE, make op1-cpuprobe): the op1-seq image plus
// timing, for Rings + Clouds (Test 28: Clouds costs 3-5x its simulated
// cycles on the device, Rings ~1.2x; the screen starves above ~40% load).
// v3 splits Clouds and Rings into stages and tests the instruction cache,
// to tell cache misses (smaller code wanted) from pipeline stalls (fewer
// branches wanted).
//
// The white click on a Rings page cycles: drawing -> page 0 -> page 1 ->
// page 2 -> drawing. Each page is ten numbers (the sequencer probe's
// seqprobe.svg, op1/gen_seqprobe_svg.py; index on the left). Values cover
// the last second (689 audio blocks) and hold when nothing renders. MHz x
// 10 = cycles per second / 100000.
//
// Page 0 (as Test 28):
//   0  core clock, MHz          1  Rings, MHz x 10 (render + note-on)
//   2  busiest block's Rings time, permille of the period
//   3  Clouds, MHz x 10 (its own cycles, both instances)
//   4  screen frames per second    5  longest gap between frames, ms
//   6  Rings voice renders per block x 10
//   7  SDRAM, 64 KB sweep at a 32-byte stride: cycles per load x 10
//   8  SDRAM, 1 KB read repeatedly: cycles per load x 10
//   9  L1 scratchpad, the same loop: cycles per load x 10
// Page 1 (Clouds, MHz x 10 unless said):
//   0  both instances' own cycles   1  master (stereo)   2  synth (mono)
//   3  decimator + interpolator + dry/wet mix (op1/clouds_fx.cc)
//   4  granular scheduling (the player less its overlap-add)
//   5  overlap-add (the grain kernels)
//   6  feedback + record write
//   7  diffuser + pitch shifter + texture filters + reverb
//   8  active grains: master x 100 + synth
//   9  the governor's grain cap: master x 100 + synth
// Page 2 (Rings and the caches):
//   0  Rings, MHz x 10        1  mode bank     2  coefficient updates
//   3  the rest of the voice (RenderMono less 1 and 2)
//   4  the glue (RingsRender less RenderMono: smoothing, upsampler, fades)
//   5  the mode bank kernel, warm (a second run on the same 28 modes):
//      cycles per mode and sample x 10 (the simulator: 237)
//   6  the same kernel cold (after a 64 KB sweep evicts the modes)
//   7  the previous kernel (no prefetch, unaligned loop tops) cold
//   8  the master governor's overruns so far (a skipped block, or a mean
//      call interval 2% over the nominal period)
//   9  the master's mean call interval, permille of the nominal period
//      (1000 = real time; more = the audio thread drops blocks)
//      (v4 had the instruction cache tests here: 16 KB, a miss 100-190
//      cycles, Test 30; v3 the data cache's size: 16 KB)

#include "op1/cpu_probe.h"

#include <cstdint>

#include "op1/op1.h"
#ifdef OP1_CLOUDS
#include "op1/clouds_fx.h"
#include "src/core/profile.h"
#endif
#include "src/modal_resonator.h"

extern "C" void ModeBankProcessAsmOld(op1rings::Mode* modes, int32_t num_modes,
                                      const int32_t* in, int32_t* previous_input,
                                      int32_t* out, int32_t* aux, int32_t size,
                                      int32_t* half_x);

namespace {

const uint32_t kBlockCounter = 0xff8016e0;   // +1 per audio block
const uint32_t kBlocksPerWindow = 689;       // ~1 s at 128 samples, 88.2 kHz
const uint32_t kL1Scratch = 0xffb000c0;      // src/scratch.h (read only here)

// Screen API (docs/screen-interface.md §2; as op1/seq_probe.cc).
const uint32_t kNodeFind = 0x019eb8ac;
const uint32_t kScreenDraw = 0x01a3b4c8;
const uint32_t kNew = 0x01a5e3b0;
const uint32_t kSvgFileNew = 0x01a50350;
const uint32_t kSvgFileLoad = 0x01a503d8;
const uint32_t kNodeVisible = 0x50;

typedef void* (*NodeFindFn)(void* document, const char* id, int32_t assert_missing);
typedef void (*DrawFn)(void* screen);
typedef void* (*NewFn)(uint32_t size);
typedef void* (*SvgFileNewFn)(void* file, const char* name);
typedef void* (*SvgFileLoadFn)(void* file);

uint32_t BlockCounter() { return *reinterpret_cast<const volatile uint32_t*>(kBlockCounter); }

// Cycle counters the audio thread adds to; the window publishes each one's
// delta as MHz x 10.
enum Counter {
  C_RINGS, C_VOICE, C_UPSAMPLER, C_MODE_BANK, C_UPDATES,
  C_CLOUDS_MASTER, C_CLOUDS_SYNTH,
  C_RESAMPLE_MIX, C_SCHEDULE, C_OVERLAP, C_RECORD, C_DIFFUSE_REVERB,
  C_NUM
};
uint32_t counters[C_NUM];
uint32_t counters_mark[C_NUM];
volatile uint32_t mhz10[C_NUM];

// The audio side (RingsRender's context).
uint32_t block = 0;
uint32_t block_cycles = 0, peak_block = 0;
uint32_t window_block = 0, window_cycles = 0;
uint32_t voices_window = 0;
bool started = false;

// Published once per window (read by the screen; a torn value shows for a
// frame).
volatile uint32_t clock_mhz, peak_permille, voices10;

// The screen side.
volatile int32_t page = -1;   // -1: the drawing
uint32_t frame_window_block = 0, frames_window = 0, last_frame = 0, longest_gap = 0;
volatile uint32_t frames_per_second, longest_gap_ms;
volatile uint32_t sweep10, hot10, l1_10;
volatile uint32_t bank_warm10, bank_cold10, bank_old10;
op1rings::Mode bank_modes[28];        // SDRAM, as a voice's modes
int32_t bank_in[32], bank_out[32], bank_aux[32], bank_half_x[32];
uint32_t sweep_index = 0;
volatile uint32_t sink;
uint32_t sdram_area[16384];   // 64 KB of our image's bss (SDRAM)

// Cycles per load x 10 of 1024 loads at base[i], i += stride (words) below
// limit (words), best of 4 runs (an interrupt only makes a run slower).
// index carries over, so the sweep never revisits the last 32 KB.
uint32_t TimeLoads(const volatile uint32_t* base, uint32_t limit, uint32_t stride, uint32_t* index) {
  uint32_t best = 0xffffffffu;
  uint32_t i = *index;
  for (int32_t run = 0; run < 4; ++run) {
    uint32_t sum = 0;
    const uint32_t start = op1::Cycles();
    for (int32_t k = 0; k < 1024; ++k) {
      sum += base[i];
      i += stride;
      if (i >= limit) i -= limit;
    }
    const uint32_t cycles = op1::Cycles() - start;
    sink = sink + sum;
    if (cycles < best) best = cycles;
  }
  *index = i;
  return best * 10 / 1024;
}

// The mode bank kernel on 28 modes and 32 samples (a modal voice's
// block at the "all" resolution): cycles per mode and sample x 10. Warm:
// the second of two runs back to back. Cold: after a 64 KB sweep of
// sdram_area (the data cache is 16 KB) evicts the modes; the stack
// buffers are small and warm either way. Best of 3 (an interrupt only
// makes a run slower). The modes' values don't matter for the timing
// (no data-dependent paths), so they are left at zero.
typedef void (*BankFn)(op1rings::Mode*, int32_t, const int32_t*, int32_t*, int32_t*,
                       int32_t*, int32_t, int32_t*);

uint32_t TimeBank(BankFn fn) {
  int32_t previous = 0;
  const uint32_t start = op1::Cycles();
  fn(bank_modes, 28, bank_in, &previous, bank_out, bank_aux, 32, bank_half_x);
  return op1::Cycles() - start;
}

void EvictModes() {
  uint32_t sum = 0;
  for (uint32_t i = 0; i < 16384; i += 8) sum += sdram_area[i];
  sink = sink + sum;
}

void TimeModeBank() {
  uint32_t warm = 0xffffffffu, cold = 0xffffffffu, old = 0xffffffffu;
  for (int32_t run = 0; run < 3; ++run) {
    EvictModes();
    uint32_t t = TimeBank(op1rings::ModeBankProcessAsm);
    if (t < cold) cold = t;
    t = TimeBank(op1rings::ModeBankProcessAsm);
    if (t < warm) warm = t;
    EvictModes();
    t = TimeBank(ModeBankProcessAsmOld);
    if (t < old) old = t;
  }
  bank_warm10 = warm * 10 / (28 * 32);
  bank_cold10 = cold * 10 / (28 * 32);
  bank_old10 = old * 10 / (28 * 32);
}

// The readout.
const int32_t kNumbers = 10;
const int32_t kDigits = 6;
const char kSegmentNames[] = "abcdefg";
const uint8_t kGlyphs[10] = {   // bit s = segment "abcdefg"[s]
  0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };

void* document;
void* segments[kNumbers][kDigits][7];
bool ready;

void SetVisible(void* node, bool visible) {
  *reinterpret_cast<volatile uint8_t*>(reinterpret_cast<uintptr_t>(node) + kNodeVisible) =
      visible ? 1 : 0;
}

void ShowNumber(int32_t i, uint32_t value) {
  uint32_t digits[kDigits];
  for (int32_t j = kDigits - 1; j >= 0; --j) {
    digits[j] = value % 10;
    value /= 10;
  }
  bool leading = true;
  for (int32_t j = 0; j < kDigits; ++j) {
    leading = leading && digits[j] == 0 && j < kDigits - 1;
    const uint8_t glyph = leading ? 0 : kGlyphs[digits[j]];
    for (int32_t s = 0; s < 7; ++s) SetVisible(segments[i][j][s], (glyph >> s) & 1);
  }
}

uint32_t Sub(uint32_t a, uint32_t b) { return a > b ? a - b : 0; }

// The counters Clouds keeps (src/core/profile.h) and Rings' profile
// counters, copied into counters[] (from either thread).
void Gather() {
#ifdef OP1_CLOUDS
  // Clouds' stages, kept by the core (src/core/profile.h).
  counters[C_CLOUDS_MASTER] = op1clouds::profile_instance_cycles[1];
  counters[C_CLOUDS_SYNTH] = op1clouds::profile_instance_cycles[0];
  counters[C_RESAMPLE_MIX] = op1clouds::profile_cycles[op1clouds::PROFILE_RESAMPLE_MIX];
  counters[C_SCHEDULE] = Sub(op1clouds::profile_cycles[op1clouds::PROFILE_PLAYER],
                             op1clouds::profile_cycles[op1clouds::PROFILE_OVERLAP]);
  counters[C_OVERLAP] = op1clouds::profile_cycles[op1clouds::PROFILE_OVERLAP];
  counters[C_RECORD] = op1clouds::profile_cycles[op1clouds::PROFILE_RECORD];
  counters[C_DIFFUSE_REVERB] = op1clouds::profile_cycles[op1clouds::PROFILE_DIFFUSE] +
                               op1clouds::profile_cycles[op1clouds::PROFILE_REVERB];
#endif
  counters[C_MODE_BANK] = op1rings::profile_mode_bank_cycles;
  counters[C_UPDATES] = op1rings::profile_update_cycles;
}

// The window: once a second, from the screen thread (Frame), so the
// numbers move whether or not Rings renders (v3 published them from
// RingsRender: Test 31's Clouds-alone cases showed held values).
void Window() {
  const uint32_t now = BlockCounter();
  const uint32_t cycles = op1::Cycles();
  if (!started) {
    started = true;
    window_block = now;
    window_cycles = cycles;
    Gather();
    for (int32_t c = 0; c < C_NUM; ++c) counters_mark[c] = counters[c];
    return;
  }
  const uint32_t blocks = now - window_block;
  if (blocks < kBlocksPerWindow) return;
  const uint32_t elapsed = cycles - window_cycles;   // < 2^32: ~1 s
  const uint32_t per_block = elapsed / blocks;
  clock_mhz = per_block * kBlocksPerWindow / 1000000;
  Gather();
  // MHz x 10 = cycles per block x 689 / 100000.
  for (int32_t c = 0; c < C_NUM; ++c) {
    mhz10[c] = (counters[c] - counters_mark[c]) / blocks * kBlocksPerWindow / 100000;
    counters_mark[c] = counters[c];
  }
  peak_permille = per_block ? peak_block / (per_block / 1000) : 0;
  voices10 = voices_window * 10 / blocks;
  window_block = now;
  window_cycles = cycles;
  voices_window = peak_block = 0;
}

}  // namespace

namespace op1cpu {

void Rendered(uint32_t start, uint32_t end) {
  const uint32_t now = BlockCounter();
  if (now != block) {
    if (block_cycles > peak_block) peak_block = block_cycles;
    block_cycles = 0;
    block = now;
  }
  block_cycles += end - start;
  counters[C_RINGS] += end - start;
}

void RingsVoiceRendered(uint32_t voice_cycles, uint32_t upsampler_cycles) {
  ++voices_window;
  counters[C_VOICE] += voice_cycles;
  counters[C_UPSAMPLER] += upsampler_cycles;
}

void Init() {
  op1rings::profile_clock = op1::Cycles;
#ifdef OP1_CLOUDS
  op1clouds::profile_clock = op1::Cycles;
#endif
  void* file = reinterpret_cast<NewFn>(kNew)(8);
  if (!file) return;
  file = reinterpret_cast<SvgFileNewFn>(kSvgFileNew)(file, "seqprobe.svg");
  document = file ? reinterpret_cast<SvgFileLoadFn>(kSvgFileLoad)(file) : nullptr;
  if (!document) return;
  char id[8] = { 'n', '0', 'd', '0', 'a', 0 };
  for (int32_t i = 0; i < kNumbers; ++i) {
    for (int32_t j = 0; j < kDigits; ++j) {
      for (int32_t s = 0; s < 7; ++s) {
        id[1] = static_cast<char>('0' + i);
        id[3] = static_cast<char>('0' + j);
        id[4] = kSegmentNames[s];
        segments[i][j][s] = reinterpret_cast<NodeFindFn>(kNodeFind)(document, id, 0);
        if (!segments[i][j][s]) return;
      }
    }
  }
  ready = true;
}

void Frame() {
  const uint32_t now = op1::Cycles();
  if (last_frame && now - last_frame > longest_gap) longest_gap = now - last_frame;
  last_frame = now;
  ++frames_window;
  Window();
  const uint32_t blocks = BlockCounter() - frame_window_block;
  if (blocks >= kBlocksPerWindow) {
    frames_per_second = frames_window * kBlocksPerWindow / blocks;
    const uint32_t mhz = clock_mhz ? clock_mhz : 400;
    longest_gap_ms = longest_gap / (mhz * 1000);
    frames_window = 0;
    longest_gap = 0;
    frame_window_block = BlockCounter();
    // Once a second, on this (Gfx) thread: memory timing. The cache tests
    // only while their page shows (they take a few ms).
    if (page == 0) {
      uint32_t hot_index = 0, l1_index = 0;
      sweep10 = TimeLoads(sdram_area, 16384, 8, &sweep_index);
      hot10 = TimeLoads(sdram_area, 256, 8, &hot_index);
      l1_10 = TimeLoads(reinterpret_cast<const volatile uint32_t*>(kL1Scratch), 256, 8, &l1_index);
    } else if (page == 2) {
      TimeModeBank();
    }
  }
}

void* Document() { return page >= 0 && ready ? document : nullptr; }

void Draw(void* screen) {
  switch (page) {
    case 0:
      ShowNumber(0, clock_mhz);
      ShowNumber(1, mhz10[C_RINGS]);
      ShowNumber(2, peak_permille);
      ShowNumber(3, mhz10[C_CLOUDS_MASTER] + mhz10[C_CLOUDS_SYNTH]);
      ShowNumber(4, frames_per_second);
      ShowNumber(5, longest_gap_ms);
      ShowNumber(6, voices10);
      ShowNumber(7, sweep10);
      ShowNumber(8, hot10);
      ShowNumber(9, l1_10);
      break;
    case 1: {
      int32_t grains = 0, cap = 0;
#ifdef OP1_CLOUDS
      grains = op1clouds::profile_grains[1] * 100 + op1clouds::profile_grains[0];
      cap = op1clouds::profile_cap[1] * 100 + op1clouds::profile_cap[0];
#endif
      ShowNumber(0, mhz10[C_CLOUDS_MASTER] + mhz10[C_CLOUDS_SYNTH]);
      ShowNumber(1, mhz10[C_CLOUDS_MASTER]);
      ShowNumber(2, mhz10[C_CLOUDS_SYNTH]);
      ShowNumber(3, mhz10[C_RESAMPLE_MIX]);
      ShowNumber(4, mhz10[C_SCHEDULE]);
      ShowNumber(5, mhz10[C_OVERLAP]);
      ShowNumber(6, mhz10[C_RECORD]);
      ShowNumber(7, mhz10[C_DIFFUSE_REVERB]);
      ShowNumber(8, static_cast<uint32_t>(grains));
      ShowNumber(9, static_cast<uint32_t>(cap));
      break;
    }
    default:
      ShowNumber(0, mhz10[C_RINGS]);
      ShowNumber(1, mhz10[C_MODE_BANK]);
      ShowNumber(2, mhz10[C_UPDATES]);
      ShowNumber(3, Sub(mhz10[C_VOICE], mhz10[C_MODE_BANK] + mhz10[C_UPDATES]));
      ShowNumber(4, Sub(mhz10[C_RINGS], mhz10[C_VOICE] + mhz10[C_UPSAMPLER]) + mhz10[C_UPSAMPLER]);
      ShowNumber(5, bank_warm10);
      ShowNumber(6, bank_cold10);
      ShowNumber(7, bank_old10);
#ifdef OP1_CLOUDS
      ShowNumber(8, static_cast<uint32_t>(op1clouds::profile_late[1]));
      ShowNumber(9, op1clouds::profile_interval[1]);
#endif
      break;
  }
  reinterpret_cast<DrawFn>(kScreenDraw)(screen);
}

void Toggle() { page = page >= 2 ? -1 : page + 1; }

}  // namespace op1cpu
