// The grids plugin's glue on the host (make host-test in ports/knl): the
// player's vtable functions, the type's press / turn / key, the settings
// text, through common/knl_seq_host.h's stand-ins for the core. The core
// (ports/grids/op1/grids_seq.*) has its own checks (make sim-grids in
// ports/grids); this drives the plugin the way kennel's kn_seq.c would.
// Part of the Grids port; Grids (c) 2011-2012 Emilie Gillet, GPL v3.

#include <cstdio>
#include <cstring>

#include "common/knl_seq.h"
#include "op1/grids_seq.h"

extern "C" {
void* knl_host_player();
int knl_host_key(int mode, int code, int pressed, int shift);
void knl_host_press(int mode, int knob, int shift);
void knl_host_turn(int mode, int knob, int delta, int shift);
int knl_host_serialize(int m, char* out, int n);
int knl_host_parse(int m, const char* s, int len);
int knl_host_store_ready();
}
op1grids::GridsSeq* grids_host_seq(int m);

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

uint32_t KeysPlayed(int from) {
  uint32_t mask = 0;
  for (int i = from; i < host_log_count; ++i) {
    if (host_log[i].on && host_log[i].key >= 0 && host_log[i].key < 32) mask |= 1u << host_log[i].key;
  }
  return mask;
}

bool NotesValid(int from, int mode) {
  for (int i = from; i < host_log_count; ++i) {
    const HostNote& n = host_log[i];
    if (!n.on) continue;
    if (n.mode != mode) return false;
    if (n.vel < 1 || n.vel > 127) return false;
    if (mode == 1 && (n.key < 0 || n.key > 23)) return false;
    if (mode == 1 && n.note != 0x35 + n.key + host_drum_octave * 12) return false;
    if (mode == 0 && (n.note < 0 || n.note > 127)) return false;
  }
  return true;
}

bool SameUi(const op1grids::GridsUi& a, const op1grids::GridsUi& b) {
  bool same = a.x == b.x && a.y == b.y && a.chaos == b.chaos && a.swing == b.swing && a.accent == b.accent &&
              a.mode == b.mode && a.resolution == b.resolution && a.random_voice == b.random_voice;
  for (int i = 0; i < 3; ++i) {
    same = same && a.fill[i] == b.fill[i] && a.length[i] == b.length[i] && a.key[i] == b.key[i] &&
           a.accent_key[i] == b.accent_key[i];
  }
  return same;
}

}  // namespace

int main() {
  host_mode = 1;
  host_synth_octave = 0;
  host_drum_octave = 0;
  host_shift = false;
  HostClearLog();

  Check(knl_host_player() != nullptr, "player() gives a player");
  Check(knl_host_store_ready() == 1, "player() loads the settings store");

  Ticks(96 * 4);
  Check(host_log_count == 0, "no key held: silent");

  // A key held: the bar plays on the default kit keys (BD 0, SD 2, HH 8).
  int from = host_log_count;
  Check(knl_seq_key_down(nullptr, 4, 0) == 1, "key_down consumes the key (the transport)");
  Ticks(96 * 4 * 2);   // two bars
  Check(Ons(from) > 0, "key held: Grids plays");
  Check(NotesValid(from, 1), "drum notes: mode 1, keys 0..23 at their own pitch");
  Check(KeysPlayed(from) == ((1u << 0) | (1u << 2) | (1u << 8)), "the parts on keys 0, 2 and 8 only");
  knl_seq_key_up(nullptr, 4);
  Ticks(12);
  const int after = host_log_count;
  Ticks(96 * 4);
  Check(Ons(after) == 0 && host_sounding == 0, "last key up: stops, nothing left sounding");
  Check(host_stray_offs == 0, "no stray note-offs");

  // The drum octave travels in the note (the sound stays the key).
  host_drum_octave = 1;
  from = host_log_count;
  knl_seq_key_down(nullptr, 4, 0);
  Ticks(96 * 4);
  Check(NotesValid(from, 1), "drum octave 1: note = 0x35 + key + 12, sound = key");
  knl_seq_key_up(nullptr, 4);
  Ticks(12);
  host_drum_octave = 0;

  // Shift latches; the next key takes over; its release stops.
  from = host_log_count;
  knl_seq_key_down(nullptr, 1, 0);
  Ticks(48);
  host_shift = true;
  Ticks(2);
  knl_seq_key_up(nullptr, 1);
  host_shift = false;
  const int latched_from = host_log_count;
  Ticks(96 * 4);
  Check(Ons(latched_from) > 0, "shift while held latches: plays on after the key up");
  knl_seq_key_down(nullptr, 7, 0);
  Ticks(48);
  knl_seq_key_up(nullptr, 7);
  Ticks(12);
  const int stopped_from = host_log_count;
  Ticks(96 * 4);
  Check(Ons(stopped_from) == 0 && host_sounding == 0, "a new key takes over and its release stops");
  knl_seq_stop(nullptr);

  // Turns and presses on the screen.
  op1grids::GridsSeq* s = grids_host_seq(1);
  host_dirty_count = 0;
  const int32_t x = s->ui().x;
  knl_host_turn(1, 0, 1024, 0);
  Check(s->ui().x == x + 1024, "turn blue: map X");
  const int32_t fill = s->ui().fill[1];
  knl_host_turn(1, 1, -2048, 1);
  Check(s->ui().fill[1] == fill - 2048, "shift + turn green: SD fill");
  knl_host_press(1, 0, 0);
  Check(s->ui().mode == op1grids::OUTPUT_MODE_EUCLIDEAN, "press blue: Euclidean");
  knl_host_press(1, 0, 1);
  Check(s->ui().resolution == op1grids::RESOLUTION_32ND, "shift + press blue: 32nds");
  knl_host_press(1, 2, 0);
  Check(s->ui().assign == op1grids::ASSIGN_BD, "press white: assign BD");
  knl_host_press(1, 2, 1);
  Check(s->ui().random_voice == 1, "shift + press white: the random voice on");
  knl_host_press(1, 3, 0);
  Check(host_dirty_count == 6, "every edit marks the store dirty; a plain orange press does nothing");
  Check(knl_host_key(1, 0x0b, 1, 0) == 1 && knl_host_key(1, 0x35, 1, 0) == 0,
        "key(): drop consumed, a music key passed on");

  // Assign: the next key becomes BD's sound.
  knl_host_press(1, 0, 0);   // back to drums
  knl_host_press(1, 0, 1);   // back to 16ths
  s->mutable_ui()->assign = op1grids::ASSIGN_BD;
  knl_seq_key_down(nullptr, 5, 0);
  knl_seq_key_up(nullptr, 5);
  Ticks(4);
  Check(s->ui().key[0] == 5, "assign BD, press a key: BD's sound is that key");
  Check(s->ui().assign == op1grids::ASSIGN_BD, "the assign stays armed until white cycles it off");
  s->mutable_ui()->assign = op1grids::ASSIGN_OFF;   // the keys are the transport again
  from = host_log_count;
  knl_seq_key_down(nullptr, 10, 0);
  Ticks(96 * 4 * 2);
  knl_seq_key_up(nullptr, 10);
  Ticks(12);
  Check((KeysPlayed(from) & 1u) == 0 && (KeysPlayed(from) & (1u << 5)) != 0, "the kick plays on key 5 now");

  // The settings text round-trips into the other mode's instance.
  char text[256];
  const int len = knl_host_serialize(1, text, sizeof text);
  Check(len > 0 && len < 120, "serialize: a short text");
  Check(knl_host_parse(0, text, len) == 1, "parse accepts it");
  Check(SameUi(grids_host_seq(1)->ui(), grids_host_seq(0)->ui()), "the parsed settings equal the serialized ones");
  Check(knl_host_parse(0, "1,0,0", 3) == 0, "parse refuses a short text");
  Check(knl_host_parse(0, "1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,24,2,8,255,255,255", 48) == 0,
        "parse refuses a key out of range");
  Check(grids_host_seq(0)->ui().key[0] == 5, "a refused text leaves the settings alone");

  // The synth: the parts as pitches.
  host_mode = 0;
  HostClearLog();
  from = host_log_count;
  knl_seq_key_down(nullptr, 0, 0);
  Ticks(96 * 4);
  Check(Ons(from) > 0 && NotesValid(from, 0), "synth mode: the parts play as notes");
  knl_seq_key_up(nullptr, 0);
  Ticks(12);
  knl_seq_reset(nullptr);
  Check(host_sounding == 0 && host_stray_offs == 0, "reset: silence, no stray offs");

  printf("%s\n", failures ? "FAILED" : "all ok");
  return failures ? 1 : 0;
}
