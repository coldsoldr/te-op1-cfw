/* Audition, a kennel feature for co-op. Id audition.
 *
 * Lets a browser, over USB MIDI and out of disk mode, play a patch on the current synth slot and put the slot's own
 * sound back.
 *
 * Channel 16, CC 118: 127 the built-in test sound, 0 put back, 1 hello, 4 begin a patch, 5 apply it. The reply is
 * CC 119 on channel 16 (AU_* below). Between begin and apply, a patch frame (audition_frame.h) arrives as polyphonic
 * pressure on channel 15, two 7-bit values a message.
 *
 * Speed test: how fast could a patch's audio be sent as ordinary MIDI? SysEx never reaches a plugin, so the data is
 * polyphonic pressure on channel 15, two 7-bit bytes a message. CC 118 value 2 zeroes the count and sum, value 3
 * reports them as 7-bit groups on CC 20..24 (count) and 25..29 (sum), low first, then AU_REPORT.
 *
 * The sound changes in memory only: unlike a preset drop this never calls the patch save job, and the slot's own
 * sound is kept here as a copy of its JSON until it is put back. Touching a knob during an audition lets the stock
 * code save what is in the slot, so that is the one way the test sound can stick.
 *
 * The firmware addresses are R246's, as used by kennel's randomiser. They only hold on hundekraft 3.0.3.
 * Copyright (C) 2026 coldsoldr. GPL-3.0-or-later with the OP-1 firmware permission of the kennel SDK. */
#pragma GCC optimize ("Os", "no-tree-loop-distribute-patterns")
#include "../../../third_party/kennel/synth_sdk/op1_feature.h"
#include "../../../third_party/kennel/synth_sdk/op1_feature_sound.h"
#include "audition_frame.h"

#define SLOT          (*(volatile int *)0xFF8016D4 & 7)
#define CUR_MODE_ID   (*(volatile int *)0xFF907D58)
#define SYNTH_MODE    0
#define CYCLES_MS     400000u
#define PATCH(s)      ((uint8_t *)0x01DDD7E8 + ((s) & 7) * 0x38)        /* +0 slot, +4 the sound's JSON */
#define VOICES_RESET  ((void (*)(void))0x01A862D4)
#define PATCH_SET     ((void (*)(void *, int, const void *))0x01A052DC)
#define SAVER_JOB     (*(void *const volatile *)0x01BA29D8)
#define SAVER_RD      (*(const volatile uint32_t *)0x01BA33E4)
#define SAVER_WR      (*(const volatile uint32_t *)0x01BA33E8)
#define CUR_CTRL      ((uint32_t *(*)(void))0x019E97FC)
#define LAST_MODE     ((int (*)(void))0x019E9528)
#define SCREEN_REQ    ((void (*)(int, int, int))0x019D7C94)
#define JV_KEY        ((void *(*)(void *, const char *))0x019BFEB4)
#define JV_IDX        ((void *(*)(void *, unsigned))0x019BFAF8)
#define JV_ASSIGN     ((void *(*)(void *, const void *))0x019BDDC0)
#define JV_DTOR       ((void (*)(void *, int))0x019BDD18)
#define JV_NULL_C     ((void *(*)(void *, int))0x019BFA30)
#define JV_INT_C      ((void *(*)(void *, int))0x019BD890)
#define JV_BOOL_C     ((void *(*)(void *, int))0x019BD832)
#define JV_STR_C      ((void *(*)(void *, const char *))0x019BDC4C)

enum { MIDI_CC_CH16 = 0xBF, MIDI_PRESSURE_CH15 = 0xAE, CC_REQUEST = 118, CC_REPLY = 119, CC_COUNT = 20, CC_SUM = 25 };
enum { REQ_AUDITION = 127, REQ_BACK = 0, REQ_HELLO = 1, REQ_ZERO = 2, REQ_REPORT = 3, REQ_BEGIN = 4, REQ_APPLY = 5 };
enum { AU_APPLIED = 1, AU_PUT_BACK = 2, AU_NOT_SYNTH = 3, AU_NOTHING_TO_PUT_BACK = 4, AU_BUSY = 5, AU_HELLO = 6, AU_REPORT = 7, AU_BAD_PATCH = 8 };
enum { JV_NULL = 0, JV_VALUE_WORDS = 4 };
#define REQUEST_MS 2000u

/* A real sound: the cluster patch from co-op's test fixtures, values as the firmware wrote them. */
static const int16_t ADSR[8] = { 64, 64, 0, 64, 14336, 64, 4000, 4000 };
static const int16_t FX[8] = { 64, -14337, 4515, 7232, 0, 0, 0, 0 };
static const int16_t KNOBS[8] = { 3072, 0, 512, 3, 0, 0, 0, 0 };
static const int16_t LFO[8] = { 4608, 32767, 8448, 15360, 0, 0, 0, 0 };

static uint32_t saved[JV_VALUE_WORDS];                          /* a copy of the slot's JSON */
static uint8_t saved_ready, have_saved;
static int saved_slot;
static volatile uint8_t request;                                /* 0 none, else the CC value + 1 */
static volatile uint32_t request_t0;
static volatile uint32_t speed_count, speed_sum;
static uint8_t wire[AU_WIRE_MAX];                               /* the frame as it arrives, 7-bit values */
static volatile uint16_t wire_len;
static volatile uint8_t wire_open;
static uint8_t frame[AU_PATCH_MAX + 8];

static uint32_t cycles(void) { uint32_t c; __asm__ volatile ("%0 = CYCLES;" : "=d"(c)); return c; }
static int saver_busy(void) { return SAVER_JOB != 0 || SAVER_RD != SAVER_WR; }

static void send_cc(int cc, int value)
{
    const uint8_t msg[3] = { MIDI_CC_CH16, (uint8_t)cc, (uint8_t)(value & 127) };
    op1_midi_send(msg, 3);
}
static void reply(int code) { send_cc(CC_REPLY, code); }
static void send_word(int cc, uint32_t word)
{
    for (int i = 0; i < 5; i++) send_cc(cc + i, (int)(word >> (7 * i)));
}

static void set_value(void *json, const char *key, const void *value) { JV_ASSIGN(JV_KEY(json, key), value); }
static void set_int(void *json, const char *key, int n)
{ uint32_t v[JV_VALUE_WORDS]; JV_INT_C(v, n); set_value(json, key, v); JV_DTOR(v, 2); }
static void set_bool(void *json, const char *key, int b)
{ uint32_t v[JV_VALUE_WORDS]; JV_BOOL_C(v, b); set_value(json, key, v); JV_DTOR(v, 2); }
static void set_str(void *json, const char *key, const char *s)
{ uint32_t v[JV_VALUE_WORDS]; JV_STR_C(v, s); set_value(json, key, v); JV_DTOR(v, 2); }
static void set_words(void *json, const char *key, const int16_t *w)
{
    uint32_t v[JV_VALUE_WORDS];
    void *array = JV_KEY(json, key);
    for (unsigned i = 0; i < 8; i++) { JV_INT_C(v, w[i]); JV_ASSIGN(JV_IDX(array, i), v); JV_DTOR(v, 2); }
}

static void page_refresh(int slot)                              /* as the stock key dispatch after a key */
{
    uint32_t *c = CUR_CTRL();
    const uint32_t *vt = (const uint32_t *)c[1];
    int id = ((int (*)(void *))vt[0x44 / 4])((uint8_t *)c + (int16_t)vt[0x40 / 4]);
    SCREEN_REQ(id, slot, LAST_MODE());
}

static void load(int slot, const void *json)                    /* a preset drop without its save job */
{
    uint8_t *patch = PATCH(slot);
    VOICES_RESET();
    PATCH_SET(patch, slot, json);
    op1_sound_apply((int *)patch);
    page_refresh(SLOT);
}

static int keep_original(int slot)                               /* a second audition keeps the first copy */
{
    if (!have_saved) {
        JV_ASSIGN(saved, PATCH(slot) + 4);
        saved_slot = slot;
        have_saved = 1;
    }
    return slot;
}

static void audition(void)
{
    int slot = keep_original(SLOT);
    uint32_t json[JV_VALUE_WORDS];
    JV_NULL_C(json, JV_NULL);
    set_words(json, "adsr", ADSR);
    set_bool(json, "fx_active", 1);
    set_words(json, "fx_params", FX);
    set_str(json, "fx_type", "nitro");
    set_words(json, "knobs", KNOBS);
    set_bool(json, "lfo_active", 0);
    set_words(json, "lfo_params", LFO);
    set_str(json, "lfo_type", "value");
    set_str(json, "name", "audition");
    set_int(json, "octave", 0);
    set_int(json, "synth_version", 2);
    set_str(json, "type", "cluster");
    load(slot, json);
    JV_DTOR(json, 2);
}

static int audition_received(void)                              /* 0, or -1 when the frame is not whole */
{
    au_patch p;
    int len = au_unpack(frame, sizeof frame, wire, wire_len);
    if (len < 0 || au_parse(&p, frame, len)) return -1;
    int slot = keep_original(SLOT);
    uint32_t json[JV_VALUE_WORDS];
    JV_NULL_C(json, JV_NULL);
    set_words(json, "adsr", p.adsr);
    set_bool(json, "fx_active", p.fx_active);
    set_words(json, "fx_params", p.fx_params);
    set_str(json, "fx_type", p.fx_type);
    set_words(json, "knobs", p.knobs);
    set_bool(json, "lfo_active", p.lfo_active);
    set_words(json, "lfo_params", p.lfo_params);
    set_str(json, "lfo_type", p.lfo_type);
    set_str(json, "name", p.name);
    set_int(json, "octave", p.octave);
    set_int(json, "synth_version", p.synth_version);
    set_str(json, "type", p.type);
    load(slot, json);
    JV_DTOR(json, 2);
    return 0;
}

static void put_back(void)
{
    load(saved_slot, saved);
    have_saved = 0;
}

static void service(void)
{
    int req = request;
    if (!saved_ready) { JV_NULL_C(saved, JV_NULL); saved_ready = 1; }
    if (!req) return;
    int value = req - 1;
    if (value == REQ_HELLO) { reply(AU_HELLO); request = 0; return; }
    if (value == REQ_ZERO) { speed_count = 0; speed_sum = 0; reply(AU_HELLO); request = 0; return; }
    if (value == REQ_REPORT) { send_word(CC_COUNT, speed_count); send_word(CC_SUM, speed_sum); reply(AU_REPORT); request = 0; return; }
    if (CUR_MODE_ID != SYNTH_MODE || op1_sound_editor_open()) { op1_sound_fade(0); reply(AU_NOT_SYNTH); request = 0; return; }
    if (value == REQ_BACK && !have_saved) { reply(AU_NOTHING_TO_PUT_BACK); request = 0; return; }
    if (cycles() - request_t0 > REQUEST_MS * CYCLES_MS) { op1_sound_fade(0); reply(AU_BUSY); request = 0; return; }
    if (saver_busy()) return;                                   /* the saver writes sounds without a lock */
    if (op1_sound_fade(1)) return;                              /* until the audio thread confirms silence */
    if (value == REQ_AUDITION) { audition(); reply(AU_APPLIED); }
    else if (value == REQ_APPLY) reply(audition_received() ? AU_BAD_PATCH : AU_APPLIED);
    else { put_back(); reply(AU_PUT_BACK); }
    op1_sound_fade(0);
    request = 0;
}

/* Timer context: only notes the request. */
static void midi_raw(const uint8_t *msg, int len)
{
    if (len != 3) return;
    if (msg[0] == MIDI_PRESSURE_CH15) {
        speed_count++;
        speed_sum += (uint32_t)msg[1] << 7 | msg[2];
        if (wire_open && wire_len + 2 <= AU_WIRE_MAX) { wire[wire_len] = msg[1]; wire[wire_len + 1] = msg[2]; wire_len = (uint16_t)(wire_len + 2); }
        return;
    }
    if (msg[0] != MIDI_CC_CH16 || msg[1] != CC_REQUEST || request) return;
    if (msg[2] == REQ_BEGIN) { wire_len = 0; wire_open = 1; return; }
    if (msg[2] == REQ_APPLY) wire_open = 0;
    else if (msg[2] != REQ_AUDITION && msg[2] != REQ_BACK && msg[2] != REQ_HELLO && msg[2] != REQ_ZERO && msg[2] != REQ_REPORT) return;
    request_t0 = cycles();
    request = (uint8_t)(msg[2] + 1);
}

static void event(int id, int32_t arg) { (void)arg; if (id == OP1_EV_SERVICE) service(); }

static const char *const claims[] = { 0 };
const op1_feature_t FEATURE_audition = {
    .api = OP1_FEATURE_API, .priority = 600, .name = "audition", .event = event, .midi_raw = midi_raw, .claims = claims,
};
