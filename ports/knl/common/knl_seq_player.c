/* The player object a kennel sequencer type hands PlayerManager (op1_feature_seq.h): a pointer to a
 * vtable of {adj, fn} pairs, offsets as R246's ok_player (vtable 0x01910E9C): +0x0c nop, +0x14
 * key_down(key 0..23, flag) -> 1 = consumed, +0x1c key_up(key) (consumed keys only), +0x24 reset,
 * +0x2c tick(counter), +0x34 nop, +0x3c stop. The entries are the plugin's knl_seq_* functions
 * (common/knl_seq.h). In C because a function address cast to a word is a constant here; in C++ it
 * would be a static initialiser, which the loader never runs. */
#include <stdint.h>

int knl_seq_key_down(void *self, int key, int flag);
void knl_seq_key_up(void *self, int key);
void knl_seq_reset(void *self);
void knl_seq_tick(void *self, int t);
void knl_seq_stop(void *self);

static int nop(void *self) { (void)self; return 0; }

static const uint32_t vt[16] = {
    0, 0,  0, (uint32_t)nop,  0, (uint32_t)knl_seq_key_down,  0, (uint32_t)knl_seq_key_up,
    0, (uint32_t)knl_seq_reset,  0, (uint32_t)knl_seq_tick,  0, (uint32_t)nop,  0, (uint32_t)knl_seq_stop };
static struct { const uint32_t *vt; uint32_t pad[7]; } player = { vt, { 0 } };

void *knl_seq_player_object(void) { return &player; }
