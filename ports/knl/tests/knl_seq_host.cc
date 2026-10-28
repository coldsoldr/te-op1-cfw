// The host side of common/knl_seq_host.h: the core services a sequencer
// plugin imports, as plain data and a note log. sq_join / sq_split follow
// seq/seq_text.h (numbers 0..9999 joined with ','), so a text the host
// round-trips is one the core would.

#include "common/knl_seq_host.h"

#include <cstdio>

namespace knl {
namespace seq {

int host_mode;
int host_synth_octave;
int host_drum_octave;
bool host_shift;
int host_dirty_count;
const char* host_store_text[2];
HostNote host_log[kHostLogMax];
int host_log_count;
int host_sounding;
int host_stray_offs;

namespace {
uint8_t on_[2][128];
}

void HostClearLog() {
  host_log_count = 0;
  host_sounding = 0;
  host_stray_offs = 0;
  for (int m = 0; m < 2; ++m) {
    for (int i = 0; i < 128; ++i) on_[m][i] = 0;
  }
}

}  // namespace seq
}  // namespace knl

using namespace knl::seq;

extern "C" int op1_seq_note_on(int mode, int note, int key, int vel) {
  // kn_seq.c: the synth takes `note` as the MIDI note; the drum the sound
  // `key` (its MIDI note is key + 0x35) at the pitch `note`.
  const int midi = mode ? key + 0x35 : note;
  if (midi < 0 || midi > 127) return -1;
  if (host_log_count < kHostLogMax) {
    HostNote& n = host_log[host_log_count++];
    n.mode = mode;
    n.note = note;
    n.key = key;
    n.vel = vel;
    n.on = 1;
  }
  if (!on_[mode][midi]) ++host_sounding;
  on_[mode][midi] = 1;
  return midi;
}

extern "C" void op1_seq_note_off(int id) {
  if (id < 0 || id > 127) return;
  if (host_log_count < kHostLogMax) {
    HostNote& n = host_log[host_log_count++];
    n.mode = host_mode;
    n.note = id;
    n.key = id - 0x35;
    n.vel = 0;
    n.on = 0;
  }
  // A note-off carries no mode: it's whichever mode has it on.
  if (on_[host_mode][id]) {
    on_[host_mode][id] = 0;
    --host_sounding;
  } else if (on_[host_mode ^ 1][id]) {
    on_[host_mode ^ 1][id] = 0;
    --host_sounding;
  } else {
    ++host_stray_offs;
  }
}

extern "C" void sq_store_load(sq_store_t* st) {
  if (st->ready) return;
  for (int pos = 0; pos < 2; ++pos) {
    const char* text = host_store_text[pos];
    if (!text) continue;
    int len = 0;
    while (text[len]) ++len;
    if (len && len < st->maxlen) st->parse(pos, text, len);
  }
  st->ready = 1;
}

extern "C" void sq_store_dirty(sq_store_t* st) {
  if (st->ready) ++host_dirty_count;
}

extern "C" int sq_join(char* out, int n, const int* v, int k) {
  int o = 0;
  for (int i = 0; i < k; i++) {
    char d[4];
    int m = 0, x = v[i];
    do {
      d[m++] = (char)('0' + x % 10);
      x /= 10;
    } while (x && m < 4);
    if (o + m + 2 >= n) break;
    if (i) out[o++] = ',';
    while (m) out[o++] = d[--m];
  }
  out[o] = 0;
  return o;
}

extern "C" int sq_split(const char* s, int len, int* v, int max) {
  int k = 0, x = -1;
  for (int i = 0; i <= len; i++) {
    char ch = i < len ? s[i] : ',';
    if (ch >= '0' && ch <= '9') {
      x = (x < 0 ? 0 : x * 10) + (ch - '0');
      if (x > 9999) return -1;
    } else if (ch == ',') {
      if (x < 0 || k >= max) return -1;
      v[k++] = x;
      x = -1;
    } else {
      return -1;
    }
  }
  return k;
}

extern "C" void* knl_seq_player_object(void) {
  static int dummy;
  return &dummy;
}
