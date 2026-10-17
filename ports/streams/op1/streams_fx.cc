// The Streams effect's OP-1 entry points (op1/streams_fx.h).
//
// State: one StreamsCore per position (synth, drum, master) in this image's
// .bss, which the injected LDR zero-fills (SDRAM). Nothing goes into the L1
// effect object: it belongs to the host effect (phone), which must keep
// working when the user swaps back to it. The mono object at 0xffb00600 is
// shared by the synth and drum positions (only the live instrument's effect
// exists); init(this, position) tells which one is live.
//
// No constructors run (the image has no startup code): the cores live in raw
// storage and are set up by StreamsCore::Init on the first call, after an
// init, and after a gap in calls.

#include "op1/streams_fx.h"

namespace op1streams {

uint32_t g_fake_cycles;
uint32_t g_restarts;

namespace {

struct PositionState {
  bool started;
  uint32_t last_call;
  StreamsCore core;
};

// Raw, zero-filled storage: no static constructor.
union Storage {
  uint8_t bytes[sizeof(PositionState)];
  uint32_t align;
};
Storage g_storage[POSITION_LAST];
int32_t g_mono_position;  // from init: POSITION_SYNTH or POSITION_DRUM

inline PositionState* State(int32_t position) {
  return reinterpret_cast<PositionState*>(g_storage[position].bytes);
}

inline uint32_t Now() {
#ifdef __bfin__
  uint32_t c;
  __asm__ __volatile__("%0 = CYCLES;" : "=d"(c));
  return c;
#else
  return g_fake_cycles;
#endif
}

inline int16_t Clamp15(int32_t v) {
  return static_cast<int16_t>(v < 0 ? 0 : (v > 32767 ? 32767 : v));
}

// First call, after init, or after a gap: a fresh core.
PositionState* Begin(int32_t position) {
  PositionState* s = State(position);
  uint32_t now = Now();
  if (!s->started || now - s->last_call > kGapCycles) {
    s->core.Init();
    s->started = true;
    ++g_restarts;
  }
  s->last_call = now;
  return s;
}

}  // namespace

// (32767 << 14) / (max - min), folded by the compiler: no run-time division.
#define ENCODER_SCALE(k) ((32767 << 14) / (kEncoderMax[k] - kEncoderMin[k]))

int16_t NormaliseEncoder(int k, int16_t raw) {
  static const int32_t kScale[4] = {
    ENCODER_SCALE(0), ENCODER_SCALE(1), ENCODER_SCALE(2), ENCODER_SCALE(3) };
  int32_t v = raw - kEncoderMin[k];
  if (v <= 0) return 0;
  return Clamp15((v * kScale[k] + (1 << 13)) >> 14);
}

int16_t EncoderRaw(int k, int16_t value) {
  int32_t range = kEncoderMax[k] - kEncoderMin[k];
  return static_cast<int16_t>(kEncoderMin[k] + (Clamp15(value) * range + 16383) / 32767);
}

bool IsStreamsSettings(const int16_t* params) {
  return (static_cast<uint16_t>(params[4]) >> 12) == kSettingsMarker;
}

void DecodeControls(const int16_t* params, Controls* c) {
  DefaultControls(c);
  c->shape = NormaliseEncoder(0, params[0]);
  c->mod = NormaliseEncoder(1, params[1]);
  c->sense = NormaliseEncoder(2, params[2]);
  c->mix = NormaliseEncoder(3, params[3]);
  if (!IsStreamsSettings(params)) {
    return;
  }
  uint16_t w = static_cast<uint16_t>(params[4]);
  uint8_t function = w & 7;
  c->function = function < PROCESSOR_FUNCTION_LAST ? function : 0;
  c->alternate = (w >> 3) & 1;
  c->split = (w >> 4) & 1;
  c->filter_mode = (w >> 5) & 3;
  c->detector = (w >> 7) & 1;
  c->attack = Clamp15(params[5]);
  c->decay = Clamp15(params[6]);
  uint16_t ro = static_cast<uint16_t>(params[7]);
  int32_t response = (ro >> 8) & 127;
  c->response = static_cast<int16_t>(response == 127 ? 32767 : response << 8);
  c->output = static_cast<int16_t>((ro & 255) << 7);
}

int16_t EncodeSettings(const Controls& c, int page) {
  uint16_t w = static_cast<uint16_t>(kSettingsMarker << 12);
  w |= c.function & 7;
  w |= (c.alternate & 1) << 3;
  w |= (c.split & 1) << 4;
  w |= (c.filter_mode & 3) << 5;
  w |= (c.detector & 1) << 7;
  w |= (page & 1) << 8;
  return static_cast<int16_t>(w);
}

int16_t EncodeResponseOutput(const Controls& c) {
  int32_t response = c.response >= 32767 ? 127 : (c.response >> 8);
  int32_t output = c.output >> 7;
  return static_cast<int16_t>((response << 8) | (output & 255));
}

}  // namespace op1streams

using namespace op1streams;

extern "C" void StreamsFxInit(void* self, int32_t position) {
  (void)self;
  if (position < 0 || position >= POSITION_LAST) return;
  if (position != POSITION_MASTER) g_mono_position = position;
  State(position)->started = false;  // fresh core on the next call
}

extern "C" void StreamsFxMono(void* self, int16_t* buf, const int16_t* params) {
  (void)self;
  Controls c;
  DecodeControls(params, &c);
  Begin(g_mono_position)->core.ProcessMono(c, buf, kBlockSize);
}

extern "C" void StreamsFxStereo(void* self, int16_t* l, int16_t* r, const int16_t* params) {
  (void)self;
  Controls c;
  DecodeControls(params, &c);
  Begin(POSITION_MASTER)->core.ProcessStereo(c, l, r, kBlockSize);
}
