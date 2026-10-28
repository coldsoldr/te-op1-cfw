// Host stand-ins for what a sequencer plugin takes from the kennel core
// (common/knl_seq.h with -DOP1_HOST): the mode, the octaves and shift as
// variables, the notes into a log, the settings store as a text per mode
// (ports/knl/tests/knl_seq_host.cc). Tests only; nothing here reaches the
// device.

#ifndef OP1KNL_COMMON_KNL_SEQ_HOST_H_
#define OP1KNL_COMMON_KNL_SEQ_HOST_H_

#include <stdint.h>

extern "C" {
int op1_seq_note_on(int mode, int note, int key, int vel);
void op1_seq_note_off(int id);

// seq/seq_text.h's store, the fields the plugin fills.
typedef struct {
  uint8_t dirty, saving, pad[2];
  const uint32_t* vt;
  uint32_t more[2];
  const char* name;
  int (*ser)(int pos, char* out, int n);
  int (*parse)(int pos, const char* s, int len);
  char* sql;
  int16_t cap, maxlen;
  uint8_t ready;
} sq_store_t;
void sq_store_load(sq_store_t* st);
void sq_store_dirty(sq_store_t* st);
int sq_join(char* out, int n, const int* v, int k);
int sq_split(const char* s, int len, int* v, int max);
}

namespace knl {
namespace seq {

extern int host_mode;           // 0 synth, 1 drum
extern int host_synth_octave;
extern int host_drum_octave;
extern bool host_shift;
extern int host_dirty_count;    // sq_store_dirty calls
extern const char* host_store_text[2];   // what sq_store_load parses per mode (0 = nothing)

inline int Mode() { return host_mode; }
inline int SynthOctave() { return host_synth_octave; }
inline int DrumOctave() { return host_drum_octave; }
inline bool ShiftHeld() { return host_shift; }
inline uint32_t Entropy() { return 0x2545f491u; }

// The note log: every op1_seq_note_on / off, in order.
struct HostNote {
  int mode, note, key, vel, on;
};
const int kHostLogMax = 65536;
extern HostNote host_log[kHostLogMax];
extern int host_log_count;
extern int host_sounding;       // ons minus offs
extern int host_stray_offs;     // an off for a note that isn't on
void HostClearLog();

}  // namespace seq
}  // namespace knl

#endif  // OP1KNL_COMMON_KNL_SEQ_HOST_H_
