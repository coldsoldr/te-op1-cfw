// The marbles plugin's glue on the host (make host-test in ports/knl): the
// player's vtable functions, the type's press / turn / key, the settings
// text, through common/knl_seq_host.h's stand-ins for the core. The core
// itself (ports/marbles/op1/marbles_seq.*) has its own checks (make sim-seq
// in ports/marbles); this drives the plugin the way kennel's kn_seq.c would.

#include <cstdio>
#include <cstring>

#include "common/knl_seq.h"
#include "op1/marbles_seq.h"

extern "C" {
void* knl_host_player();
int knl_host_key(int mode, int code, int pressed, int shift);
void knl_host_press(int mode, int knob, int shift);
void knl_host_turn(int mode, int knob, int delta, int shift);
int knl_host_serialize(int m, char* out, int n);
int knl_host_parse(int m, const char* s, int len);
int knl_host_store_ready();
}
op1marbles::MarblesSeq* marbles_host_seq(int m);

using namespace knl::seq;

namespace {

int failures;

void Check(bool ok, const char* what) {
  printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

int tick_count;

void Ticks(int n) {
  for (int i = 0; i < n; ++i) knl_seq_tick(nullptr, tick_count++ % 48);
}

int Ons(int from) {
  int n = 0;
  for (int i = from; i < host_log_count; ++i) n += host_log[i].on;
  return n;
}

bool NotesValid(int from, int mode) {
  for (int i = from; i < host_log_count; ++i) {
    const HostNote& n = host_log[i];
    if (!n.on) continue;
    if (n.mode != mode) return false;
    if (n.vel < 1 || n.vel > 127) return false;
    if (mode == 0 && (n.note < 0 || n.note > 127)) return false;
    if (mode == 1 && (n.key < 0 || n.key > 23)) return false;
  }
  return true;
}

bool SameSettings(const op1marbles::Settings& a, const op1marbles::Settings& b) {
  return a.page == b.page && a.pitch_source == b.pitch_source && a.t_model == b.t_model && a.scale == b.scale &&
         a.t_deja_vu == b.t_deja_vu && a.x_deja_vu == b.x_deja_vu && a.rate == b.rate && a.t_bias == b.t_bias &&
         a.jitter == b.jitter && a.deja_vu == b.deja_vu && a.spread == b.spread && a.x_bias == b.x_bias &&
         a.steps == b.steps && a.length == b.length;
}

}  // namespace

int main() {
  host_mode = 0;
  host_synth_octave = 0;
  host_drum_octave = 0;
  host_shift = false;
  HostClearLog();

  // The player: both modes initialised, the store loaded.
  Check(knl_host_player() != nullptr, "player() gives a player");
  Check(knl_host_store_ready() == 1, "player() loads the settings store");

  // Nothing plays with no key held.
  Ticks(96 * 4);
  Check(host_log_count == 0, "no key held: silent");

  // A key held: notes on the synth, within MIDI, matched offs on release.
  int from = host_log_count;
  Check(knl_seq_key_down(nullptr, 5, 0) == 1, "key_down consumes the key (the transport)");
  Ticks(96 * 8);
  Check(Ons(from) > 0, "key held: Marbles plays");
  Check(NotesValid(from, 0), "synth notes: mode 0, MIDI 0..127, velocity 1..127");
  knl_seq_key_up(nullptr, 5);
  Ticks(4);
  const int after = host_log_count;
  Ticks(96 * 2);
  Check(Ons(after) == 0 && host_sounding == 0, "last key up: stops, nothing left sounding");
  Check(host_stray_offs == 0, "no stray note-offs");

  // The octave moves the notes.
  host_synth_octave = 1;
  from = host_log_count;
  knl_seq_key_down(nullptr, 0, 0);
  Ticks(96 * 4);
  int lowest = 999;
  for (int i = from; i < host_log_count; ++i) {
    if (host_log[i].on && host_log[i].note < lowest) lowest = host_log[i].note;
  }
  Check(lowest >= 0x35 + 12, "octave 1: notes from key 0 at octave 1 upwards");
  knl_seq_key_up(nullptr, 0);
  Ticks(4);
  host_synth_octave = 0;

  // Shift latches; the next key takes over; its release stops.
  from = host_log_count;
  knl_seq_key_down(nullptr, 3, 0);
  Ticks(48);
  host_shift = true;
  Ticks(2);
  knl_seq_key_up(nullptr, 3);
  host_shift = false;
  const int latched_from = host_log_count;
  Ticks(96 * 4);
  Check(Ons(latched_from) > 0, "shift while held latches: plays on after the key up");
  knl_seq_key_down(nullptr, 7, 0);
  Ticks(48);
  knl_seq_key_up(nullptr, 7);
  Ticks(4);
  const int stopped_from = host_log_count;
  Ticks(96 * 2);
  Check(Ons(stopped_from) == 0 && host_sounding == 0, "a new key takes over and its release stops");

  // The keys as the transport: shift already down plays through.
  from = host_log_count;
  host_shift = true;
  knl_seq_key_down(nullptr, 9, 1);
  Ticks(2);
  bool through = false;
  for (int i = from; i < host_log_count; ++i) through = through || (host_log[i].on && host_log[i].note == 0x35 + 9);
  Check(through, "shift + key: the key itself sounds");
  knl_seq_key_up(nullptr, 9);
  host_shift = false;
  Ticks(4);
  knl_seq_stop(nullptr);
  Check(host_sounding == 0, "stop: silence");

  // Turns and presses on the screen.
  op1marbles::MarblesSeq* s = marbles_host_seq(0);
  const int32_t rate = s->settings().rate;
  host_dirty_count = 0;
  knl_host_turn(0, 0, 1024, 0);
  Check(s->settings().rate == rate + 1024, "turn blue: the rate, a click = 1/64 of the range");
  knl_host_turn(0, 0, 1024, 1);
  Check(s->settings().rate == rate + 1024 + 128, "shift + turn: an eighth of a click");
  knl_host_press(0, 0, 0);
  Check(s->settings().t_model == 1, "press blue: the next T model");
  knl_host_press(0, 0, 1);
  Check(s->settings().t_model == 0, "shift + press blue: the previous one");
  knl_host_press(0, 1, 0);
  Check(s->settings().t_deja_vu == op1marbles::DEJA_VU_ON, "press green: T deja vu on");
  knl_host_press(0, 2, 0);
  Check(s->settings().pitch_source == op1marbles::PITCH_KEYS, "press white: pitch from the keys");
  knl_host_press(0, 2, 1);
  Check(s->settings().page == op1marbles::PAGE_X, "shift + press white: the X page");
  knl_host_press(0, 0, 0);
  Check(s->settings().scale == 1, "press blue on the X page: the next scale");
  knl_host_turn(0, 1, 2048, 0);
  Check(s->settings().length == 0x7800 + 2048, "turn green on the X page: the length");
  Check(host_dirty_count == 9, "every edit marks the store dirty");
  Check(knl_host_key(0, 0x0a, 1, 0) == 1 && knl_host_key(0, 0x35, 1, 0) == 0,
        "key(): lift consumed, a music key passed on");

  // The settings text round-trips into the other mode's instance.
  char text[256];
  const int len = knl_host_serialize(0, text, sizeof text);
  Check(len > 0 && len < 120, "serialize: a short text");
  Check(knl_host_parse(1, text, len) == 1, "parse accepts it");
  Check(SameSettings(marbles_host_seq(0)->settings(), marbles_host_seq(1)->settings()),
        "the parsed settings equal the serialized ones");
  Check(knl_host_parse(1, "2,0,0", 3) == 0 && knl_host_parse(1, "1,0,0,9,0,0,0,0,0,0,0,0,0,0,0", 29) == 0,
        "parse refuses another version and a value out of range");

  // The drums: a pitch picks a sound.
  host_mode = 1;
  from = host_log_count;
  knl_seq_key_down(nullptr, 2, 0);
  Ticks(96 * 8);
  Check(Ons(from) > 0, "drum mode: Marbles plays the kit");
  Check(NotesValid(from, 1), "drum notes: mode 1, keys 0..23");
  knl_seq_key_up(nullptr, 2);
  Ticks(4);
  knl_seq_reset(nullptr);
  Check(host_sounding == 0 && host_stray_offs == 0, "reset: silence, no stray offs");

  printf("%s\n", failures ? "FAILED" : "all ok");
  return failures ? 1 : 0;
}
