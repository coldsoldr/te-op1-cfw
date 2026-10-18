// CPU probe v1 (-DOP1_CPU_PROBE, make op1-cpuprobe): the op1-seq image plus
// timing, for the user's two reports after Test 25 (the tape animation gets
// choppy while recording Rings with more than three voices; Clouds and Rings
// overload together). seq10 moved Rings' per-call buffers to L1 on the
// inference that the OP-1 runs without a data cache, and barely helped
// (Test 27): this measures instead.
//
// The white click on a Rings page swaps the drawing for a readout (the
// sequencer probe's seqprobe.svg, op1/gen_seqprobe_svg.py): ten numbers,
// index on the left. Values cover the last second (689 audio blocks) and
// hold when nothing renders.
//
//   0  core clock, MHz (cycles per second, from the cycle counter)
//   1  Rings render, MHz x 10 (every MULTI render and note-on call)
//   2  the busiest block's Rings time, % of the block period x 10
//   3  Clouds, MHz x 10 (its own cycles, in the Clouds image)
//   4  screen frames per second (this screen's Update calls)
//   5  the longest gap between two frames, ms
//   6  Rings voice renders per block x 10 (voices the OP-1 renders)
//   7  SDRAM loads, a 64 KB sweep at a 32-byte stride: cycles per load x 10
//   8  SDRAM loads, 1 KB read over and over: cycles per load x 10
//      (close to 9 with a data cache, close to 7 without)
//   9  L1 scratchpad loads, the same loop: cycles per load x 10 (the loop's
//      own cost)

#include "op1/cpu_probe.h"

#include <cstdint>

#include "op1/op1.h"
#ifdef OP1_CLOUDS
#include "op1/clouds_fx.h"
#endif

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

// The audio side (RingsRender's context).
uint32_t block = 0;
uint32_t block_cycles = 0, peak_block = 0;
uint32_t window_block = 0, window_cycles = 0;
uint32_t rings_window = 0, voices_window = 0;
uint32_t measured_mark = 0;
bool started = false;

// Published once per window (read by the screen; a torn value shows for a
// frame).
volatile uint32_t clock_mhz, rings_mhz10, peak_permille, clouds_mhz10, voices10;

// The screen side.
volatile bool readout_on;
uint32_t frame_window_block = 0, frames_window = 0, last_frame = 0, longest_gap = 0;
volatile uint32_t frames_per_second, longest_gap_ms;
volatile uint32_t sweep10, hot10, l1_10;
uint32_t sweep_index = 0;
volatile uint32_t sink;
uint32_t sdram_area[16384];   // 64 KB of our image's bss (SDRAM)

// Cycles per load x 10 of 1024 loads at base[i], i += stride (words) within
// mask, best of 4 runs (an interrupt only makes a run slower). index carries
// over, so the sweep never revisits the last 32 KB.
uint32_t TimeLoads(const volatile uint32_t* base, uint32_t mask, uint32_t stride, uint32_t* index) {
  uint32_t best = 0xffffffffu;
  uint32_t i = *index;
  for (int32_t run = 0; run < 4; ++run) {
    uint32_t sum = 0;
    const uint32_t start = op1::Cycles();
    for (int32_t k = 0; k < 1024; ++k) {
      sum += base[i];
      i = (i + stride) & mask;
    }
    const uint32_t cycles = op1::Cycles() - start;
    sink = sink + sum;
    if (cycles < best) best = cycles;
  }
  *index = i;
  return best * 10 / 1024;
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

}  // namespace

namespace op1cpu {

void Rendered(uint32_t start, uint32_t end, bool rings) {
  (void)rings;
  const uint32_t now = BlockCounter();
  if (!started) {
    started = true;
    block = window_block = now;
    window_cycles = start;
#ifdef OP1_CLOUDS
    measured_mark = op1fx::measured_cycles;
#endif
  }
  if (now != block) {
    if (block_cycles > peak_block) peak_block = block_cycles;
    block_cycles = 0;
    block = now;
    const uint32_t blocks = now - window_block;
    if (blocks >= kBlocksPerWindow) {
      const uint32_t elapsed = start - window_cycles;   // < 2^32: ~1 s
      const uint32_t per_block = elapsed / blocks;
      clock_mhz = per_block * kBlocksPerWindow / 1000000;
      // MHz = cycles per window scaled to 689 blocks.
      const uint32_t scale = blocks;   // ~689
      rings_mhz10 = rings_window / 100000 * kBlocksPerWindow / scale;
      peak_permille = per_block ? peak_block / (per_block / 1000) : 0;
#ifdef OP1_CLOUDS
      const uint32_t measured = op1fx::measured_cycles - measured_mark;
      clouds_mhz10 = (measured > rings_window ? measured - rings_window : 0) / 100000 *
                     kBlocksPerWindow / scale;
      measured_mark = op1fx::measured_cycles;
#endif
      voices10 = voices_window * 10 / blocks;
      window_block = now;
      window_cycles = start;
      rings_window = voices_window = peak_block = 0;
    }
  }
  block_cycles += end - start;
  rings_window += end - start;
}

void RingsVoiceRendered() { ++voices_window; }

void Init() {
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
  const uint32_t blocks = BlockCounter() - frame_window_block;
  if (blocks >= kBlocksPerWindow) {
    frames_per_second = frames_window * kBlocksPerWindow / blocks;
    const uint32_t mhz = clock_mhz ? clock_mhz : 400;
    longest_gap_ms = longest_gap / (mhz * 1000);
    frames_window = 0;
    longest_gap = 0;
    frame_window_block = BlockCounter();
    // Once a second, on this (Gfx) thread: memory timing.
    uint32_t hot_index = 0, l1_index = 0;
    sweep10 = TimeLoads(sdram_area, 16383, 8, &sweep_index);
    hot10 = TimeLoads(sdram_area, 255, 8, &hot_index);
    l1_10 = TimeLoads(reinterpret_cast<const volatile uint32_t*>(kL1Scratch), 255, 8, &l1_index);
  }
}

void* Document() { return readout_on && ready ? document : nullptr; }

void Draw(void* screen) {
  ShowNumber(0, clock_mhz);
  ShowNumber(1, rings_mhz10);
  ShowNumber(2, peak_permille);
  ShowNumber(3, clouds_mhz10);
  ShowNumber(4, frames_per_second);
  ShowNumber(5, longest_gap_ms);
  ShowNumber(6, voices10);
  ShowNumber(7, sweep10);
  ShowNumber(8, hot10);
  ShowNumber(9, l1_10);
  reinterpret_cast<DrawFn>(kScreenDraw)(screen);
}

void Toggle() { readout_on = !readout_on; }

}  // namespace op1cpu
