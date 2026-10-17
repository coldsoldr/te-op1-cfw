// Sequencer probe v1: wraps the tombola sequencer (docs/sequencer-interface.md
// §9) to answer the device questions of its §10, in the Clouds image
// (-DOP1_SEQ_PROBE, make op1-seqprobe). Tombola plays as stock unless echo
// mode is on; its screen shows the probe's readout instead of the balls
// (seqprobe.svg, op1/gen_seqprobe_svg.py): ten numbers, index on the left.
//
//   0  status digits: version (1), echo (0/1), velocity mode (0-3),
//      instrument [0x1de4c40] (0 synth, 1 drum), synth sequencer playing,
//      drum sequencer playing (PlayerManager 0x1ab3141 / 0x1ab3140)
//   1  player Tick calls in the last 689 audio blocks (~1 s): ticks/s
//   2  tempo x 10 from PlayerManager's tick threshold [0x1b60524]
//   3  largest t passed to Tick, + 1 (expect 48)
//   4  Tick calls, total (mod 10^6)
//   5  Tick calls with t == 0 (mod 10^6)
//   6  KeyDown calls (mod 1000) x 1000 + the last key
//   7  the last KeyDown's second argument (flag), absolute value
//   8  KeyUp calls (mod 1000) x 1000 + AllNotesOff calls (mod 1000)
//   9  OnLeave calls (mod 1000) x 1000 + UiFrame calls per second
//
// Clicks on the tombola screen (they arrive only while the sequencer is
// on): orange = echo mode on/off; blue = next velocity mode (127, 64, 16,
// alternating 127/16); green = clear the counters.
//
// Echo mode: KeyDown/KeyUp are consumed and queued; Tick plays them with
// MakeMidiNote / VoiceCtl::NoteOn / NoteOff, from the sequencer thread, as
// stock tombola plays its balls. Stock tombola's Tick and KeyDown are not
// called meanwhile (its balls stop sounding).

#include "op1/seq_probe.h"

#include <cstdint>

namespace {

const int32_t kVersion = 1;

// Firmware (docs/sequencer-interface.md §2-§5).
const uint32_t kMakeMidiNote = 0x01a85014;    // (MidiNote*, key, velocity)
const uint32_t kNoteOn = 0x01a85750;          // (MidiNote*, 0)
const uint32_t kNoteOff = 0x01a85840;         // (MidiNote*)
const uint32_t kInstrument = 0x01de4c40;      // 0 synth, 1 drum
const uint32_t kDrumPlaying = 0x01ab3140;     // byte
const uint32_t kSynthPlaying = 0x01ab3141;    // byte
const uint32_t kTickThreshold = 0x01b60524;   // (2646000 / (96 bpm)) << 18
const uint32_t kBlockCounter = 0xff8016e0;    // +1 per audio block
const uint32_t kTombolaPlayer = 0x01dfc254;
const int32_t kBlocksPerSecond = 689;
const int32_t kKeys = 128;

// Screen API (docs/screen-interface.md §2; as ../clouds/op1/clouds_screen.cc).
const uint32_t kNodeFind = 0x019eb8ac;
const uint32_t kScreenDraw = 0x01a3b4c8;
const uint32_t kNew = 0x01a5e3b0;
const uint32_t kSvgFileNew = 0x01a50350;
const uint32_t kSvgFileLoad = 0x01a503d8;
const uint32_t kNodeVisible = 0x50;

const int32_t kClickBlue = 0x3b;
const int32_t kClickGreen = 0x3c;
const int32_t kClickOrange = 0x3e;

struct MidiNote {   // MakeMidiNote 0x1a85014
  int32_t note;
  int32_t velocity;
  uint8_t on;       // set to 1
  uint8_t pad[3];
  int32_t zero;
};

typedef void (*MakeMidiNoteFn)(MidiNote* n, int32_t key, int32_t velocity);
typedef void (*NoteOnFn)(MidiNote* n, int32_t flag);
typedef void (*NoteOffFn)(MidiNote* n);
typedef int32_t (*KeyDownFn)(void* player, int32_t key, int32_t flag);
typedef void (*KeyUpFn)(void* player, int32_t key);
typedef void (*PlayerFn)(void* player);
typedef void (*TickFn)(void* player, int32_t t);
typedef int32_t (*CtlKeyFn)(void* controller, int32_t key);
typedef void* (*NodeFindFn)(void* document, const char* id, int32_t assert_missing);
typedef void (*DrawFn)(void* screen);
typedef void (*InitFn)(void* screen);
typedef void* (*NewFn)(uint32_t size);
typedef void* (*SvgFileNewFn)(void* file, const char* name);
typedef void* (*SvgFileLoadFn)(void* file);

template <typename T>
T Read(uint32_t address) {
  return *reinterpret_cast<const volatile T*>(address);
}

// Counters (written from the key, sequencer and Gfx threads; read by the
// screen: a torn value only shows for a frame).
volatile bool echo;
volatile int32_t velocity_mode;
volatile uint32_t ticks_total, ticks_t0, max_t1;
volatile uint32_t ticks_window, ticks_per_second, tick_window_start;
volatile uint32_t key_downs, last_key, last_flag;
volatile uint32_t key_ups, all_notes_offs, on_leaves;
volatile uint32_t frames_window, frames_per_second, frame_window_start;
uint32_t echo_notes;

// Echo queues: KeyDown/KeyUp set, Tick plays and clears (as tombola).
volatile uint8_t pending_on[kKeys];
volatile uint8_t pending_velocity[kKeys];
volatile uint8_t pending_off[kKeys];
uint8_t sounding[kKeys];

void Play(int32_t key, int32_t velocity, bool on) {
  MidiNote n;
  reinterpret_cast<MakeMidiNoteFn>(kMakeMidiNote)(&n, key, velocity);
  if (n.note < 0 || n.note > 127) return;
  if (on) {
    reinterpret_cast<NoteOnFn>(kNoteOn)(&n, 0);
  } else {
    reinterpret_cast<NoteOffFn>(kNoteOff)(&n);
  }
}

void ClearCounters() {
  ticks_total = ticks_t0 = max_t1 = 0;
  ticks_window = ticks_per_second = 0;
  key_downs = last_key = last_flag = 0;
  key_ups = all_notes_offs = on_leaves = 0;
  frames_window = frames_per_second = 0;
}

int32_t EchoVelocity() {
  switch (velocity_mode) {
    case 1: return 64;
    case 2: return 16;
    case 3: return (echo_notes++ & 1) ? 16 : 127;
    default: return 127;
  }
}

}  // namespace

extern "C" int32_t SeqProbeKeyDown(void* player, int32_t key, int32_t flag) {
  ++key_downs;
  last_key = static_cast<uint32_t>(key);
  last_flag = static_cast<uint32_t>(flag < 0 ? -flag : flag);
  if (echo && key >= 0 && key < kKeys) {
    pending_velocity[key] = static_cast<uint8_t>(EchoVelocity());
    pending_on[key] = 1;
    return 1;
  }
  return reinterpret_cast<KeyDownFn>(op1seq::kStockKeyDown)(player, key, flag);
}

extern "C" void SeqProbeKeyUp(void* player, int32_t key) {
  ++key_ups;
  if (echo && key >= 0 && key < kKeys) {
    pending_off[key] = 1;
    return;
  }
  reinterpret_cast<KeyUpFn>(op1seq::kStockKeyUp)(player, key);
}

extern "C" void SeqProbeOnLeave(void* player) {
  ++on_leaves;
  reinterpret_cast<PlayerFn>(op1seq::kStockOnLeave)(player);
}

extern "C" void SeqProbeTick(void* player, int32_t t) {
  ++ticks_total;
  if (t == 0) ++ticks_t0;
  if (static_cast<uint32_t>(t + 1) > max_t1) max_t1 = static_cast<uint32_t>(t + 1);
  ++ticks_window;
  const uint32_t blocks = Read<uint32_t>(kBlockCounter);
  if (blocks - tick_window_start >= static_cast<uint32_t>(kBlocksPerSecond)) {
    ticks_per_second = ticks_window;
    ticks_window = 0;
    tick_window_start = blocks;
  }
  if (!echo) {
    reinterpret_cast<TickFn>(op1seq::kStockTick)(player, t);
    return;
  }
  for (int32_t k = 0; k < kKeys; ++k) {
    if (pending_off[k]) {
      pending_off[k] = 0;
      if (sounding[k]) Play(k, 0, false);
      sounding[k] = 0;
    }
  }
  for (int32_t k = 0; k < kKeys; ++k) {
    if (pending_on[k]) {
      pending_on[k] = 0;
      if (sounding[k]) Play(k, 0, false);
      Play(k, pending_velocity[k], true);
      sounding[k] = 1;
    }
  }
}

extern "C" void SeqProbeUiFrame(void* player, int32_t ticks) {
  ++frames_window;
  const uint32_t blocks = Read<uint32_t>(kBlockCounter);
  if (blocks - frame_window_start >= static_cast<uint32_t>(kBlocksPerSecond)) {
    frames_per_second = frames_window;
    frames_window = 0;
    frame_window_start = blocks;
  }
  if (!echo) reinterpret_cast<TickFn>(op1seq::kStockUiFrame)(player, ticks);
}

extern "C" void SeqProbeAllNotesOff(void* player) {
  ++all_notes_offs;
  for (int32_t k = 0; k < kKeys; ++k) {
    pending_on[k] = 0;
    if (sounding[k]) pending_off[k] = 1;   // played by the next Tick
  }
  reinterpret_cast<PlayerFn>(op1seq::kStockAllNotesOff)(player);
}

extern "C" int32_t SeqProbeCtlKeyDown(void* controller, int32_t key) {
  switch (key & 0xff) {
    case kClickOrange:
      if (!echo) {
        // Silence stock tombola's balls before taking its notes over.
        reinterpret_cast<PlayerFn>(op1seq::kStockAllNotesOff)(reinterpret_cast<void*>(kTombolaPlayer));
      } else {
        for (int32_t k = 0; k < kKeys; ++k) {
          pending_on[k] = 0;
          if (sounding[k]) pending_off[k] = 1;
        }
      }
      echo = !echo;
      return 1;
    case kClickBlue:
      velocity_mode = (velocity_mode + 1) & 3;
      return 1;
    case kClickGreen:
      ClearCounters();
      return 1;
    default:
      return reinterpret_cast<CtlKeyFn>(op1seq::kStockCtlKeyDown)(controller, key);
  }
}

// The readout.
namespace {

const int32_t kNumbers = 10;
const int32_t kDigits = 6;
const char kSegmentNames[] = "abcdefg";
const uint8_t kGlyphs[10] = {   // bit s = segment "abcdefg"[s]
  0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };

void* stock_document;
void* probe_document;
void* segments[kNumbers][kDigits][7];
bool ready;

void*& DocumentOf(void* screen) { return *reinterpret_cast<void**>(screen); }

void* LoadDocument(const char* name) {
  void* file = reinterpret_cast<NewFn>(kNew)(8);
  if (!file) return nullptr;
  file = reinterpret_cast<SvgFileNewFn>(kSvgFileNew)(file, name);
  return file ? reinterpret_cast<SvgFileLoadFn>(kSvgFileLoad)(file) : nullptr;
}

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

extern "C" void SeqProbeScreenInit(void* self) {
  stock_document = DocumentOf(self);
  if (stock_document) reinterpret_cast<InitFn>(op1seq::kStockScreenInit)(self);
  probe_document = LoadDocument("seqprobe.svg");
  if (!probe_document) return;
  char id[8] = { 'n', '0', 'd', '0', 'a', 0 };
  for (int32_t i = 0; i < kNumbers; ++i) {
    for (int32_t j = 0; j < kDigits; ++j) {
      for (int32_t s = 0; s < 7; ++s) {
        id[1] = static_cast<char>('0' + i);
        id[3] = static_cast<char>('0' + j);
        id[4] = kSegmentNames[s];
        segments[i][j][s] = reinterpret_cast<NodeFindFn>(kNodeFind)(probe_document, id, 0);
        if (!segments[i][j][s]) return;
      }
    }
  }
  ready = true;
}

extern "C" void SeqProbeScreenUpdate(void* self, int32_t ticks) {
  if (!ready) {
    // No readout: the stock screen.
    reinterpret_cast<void (*)(void*, int32_t)>(op1seq::kStockScreenUpdate)(self, ticks);
    return;
  }
  DocumentOf(self) = probe_document;
  const uint32_t threshold = Read<uint32_t>(kTickThreshold) >> 8;
  ShowNumber(0, kVersion * 100000 + (echo ? 10000 : 0) + velocity_mode * 1000 +
                    (Read<int32_t>(kInstrument) & 7) * 100 + (Read<uint8_t>(kSynthPlaying) ? 10 : 0) +
                    (Read<uint8_t>(kDrumPlaying) ? 1 : 0));
  ShowNumber(1, ticks_per_second);
  // bpm x 10 = 2646000 x 10 / 96 x 2^18 / threshold = 275625 x 1024 / (threshold >> 8).
  ShowNumber(2, threshold ? 282240000u / threshold : 0);
  ShowNumber(3, max_t1);
  ShowNumber(4, ticks_total % 1000000);
  ShowNumber(5, ticks_t0 % 1000000);
  ShowNumber(6, (key_downs % 1000) * 1000 + last_key % 1000);
  ShowNumber(7, last_flag % 1000000);
  ShowNumber(8, (key_ups % 1000) * 1000 + all_notes_offs % 1000);
  ShowNumber(9, (on_leaves % 1000) * 1000 + frames_per_second % 1000);
  reinterpret_cast<DrawFn>(kScreenDraw)(self);
}
