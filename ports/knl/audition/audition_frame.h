/* The patch frame co-op sends to the audition plugin. No firmware in here, so a host test can use it.
 *
 * On the wire: 7-bit values, carried two at a time. Groups of up to 7 bytes, each group led by a value holding their
 * high bits (bit k = high bit of byte k). Unpacked, little-endian:
 *
 *   u16 payload length, payload, u32 sum of the payload's bytes
 *
 * Payload: u8 format (2), u8 flags (bit 0 fx_active, bit 1 lfo_active), i8 octave, u8 synth_version,
 * i16 knobs[8], adsr[8], fx_params[8], lfo_params[8], f64 base_freq, u32 audio bytes, u32 sum of the audio's bytes,
 * then four NUL-terminated names: type, fx_type, lfo_type, name. A sampler sets the three audio fields, and its
 * audio follows on its own, packed the same way. Everything else leaves them zero.
 * Copyright (C) 2026 coldsoldr. GPL-3.0-or-later with the OP-1 firmware permission of the kennel SDK. */
#ifndef AUDITION_FRAME_H
#define AUDITION_FRAME_H
#include <stdint.h>

#define AU_FORMAT 2
#define AU_PATCH_MAX 400                                        /* a stock patch's fields take about 110 */
#define AU_WIRE_MAX ((AU_PATCH_MAX + 6) * 8 / 7 + 2)
#define AU_NAME_MAX 39
enum { AU_WORDS = 8, AU_FLAG_FX = 1, AU_FLAG_LFO = 2 };

typedef struct {
    uint8_t fx_active, lfo_active, synth_version;
    int8_t octave;
    int16_t knobs[AU_WORDS], adsr[AU_WORDS], fx_params[AU_WORDS], lfo_params[AU_WORDS];
    uint32_t base_freq_lo, base_freq_hi, audio_bytes, audio_sum;
    const char *type, *fx_type, *lfo_type, *name;               /* into the unpacked buffer */
} au_patch;

static inline int au_unpack(uint8_t *out, int cap, const uint8_t *wire, int len)
{
    int n = 0;
    for (int i = 0; i < len; ) {
        int high = wire[i++];
        for (int k = 0; k < 7 && i < len; k++) {
            if (n >= cap) return -1;
            out[n++] = (uint8_t)(wire[i++] | (((high >> k) & 1) << 7));
        }
    }
    return n;
}

static inline uint32_t au_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline void au_words(int16_t *out, const uint8_t *p)
{
    for (int i = 0; i < AU_WORDS; i++) out[i] = (int16_t)(uint16_t)(p[i * 2] | (p[i * 2 + 1] << 8));
}

/* The next name: NUL-terminated, 1..AU_NAME_MAX printable ASCII. 0 when it is not. */
static inline const char *au_name(const uint8_t **at, const uint8_t *end)
{
    const uint8_t *p = *at;
    int n = 0;
    while (p + n < end && p[n]) {
        if (p[n] < 0x20 || p[n] > 0x7E || n >= AU_NAME_MAX) return 0;
        n++;
    }
    if (p + n >= end || n == 0) return 0;
    *at = p + n + 1;
    return (const char *)p;
}

/* Checks a whole frame and fills `o`. 0 ok, -1 anything wrong. */
static inline int au_parse(au_patch *o, const uint8_t *frame, int len)
{
    if (len < 6) return -1;
    int size = frame[0] | (frame[1] << 8);
    if (size < 84 + 8 || size > AU_PATCH_MAX || size + 6 > len) return -1;
    const uint8_t *p = frame + 2, *end = p + size;
    uint32_t sum = 0, want = au_u32(end);
    for (int i = 0; i < size; i++) sum += p[i];
    if (sum != want || p[0] != AU_FORMAT) return -1;
    o->fx_active = (uint8_t)(p[1] & AU_FLAG_FX ? 1 : 0);
    o->lfo_active = (uint8_t)(p[1] & AU_FLAG_LFO ? 1 : 0);
    o->octave = (int8_t)p[2];
    o->synth_version = p[3];
    au_words(o->knobs, p + 4);
    au_words(o->adsr, p + 20);
    au_words(o->fx_params, p + 36);
    au_words(o->lfo_params, p + 52);
    o->base_freq_lo = au_u32(p + 68);
    o->base_freq_hi = au_u32(p + 72);
    o->audio_bytes = au_u32(p + 76);
    o->audio_sum = au_u32(p + 80);
    const uint8_t *at = p + 84;
    o->type = au_name(&at, end);
    o->fx_type = au_name(&at, end);
    o->lfo_type = au_name(&at, end);
    o->name = au_name(&at, end);
    return o->type && o->fx_type && o->lfo_type && o->name ? 0 : -1;
}
#endif
