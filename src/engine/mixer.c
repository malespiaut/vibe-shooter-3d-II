/* Integer software mixer. See mixer.h for the gain law and threading model.
 * Integer arithmetic only. */
#include "engine/mixer.h"
#include "engine/files.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct mixer_sound {
    char name[48];
    bast_snd *snd;
};

typedef struct {
    bool active;
    int source_id;
    int vol, pan;
    int gain_l, gain_r; /* 0..64 each, from pan */
    uint32_t pos;
    uint32_t serial;
    const mixer_sound *sound;
} voice;

enum { CMD_START, CMD_UPDATE, CMD_STOP };

typedef struct {
    int type;
    int source_id;
    int vol, pan;
    const mixer_sound *sound;
} command;

#define RING_SIZE 256 /* power of two */

static command g_ring[RING_SIZE];
static atomic_uint g_ring_head; /* written by the game thread */
static atomic_uint g_ring_tail; /* written by the audio thread */

static voice g_voices[MIXER_VOICES];
static uint32_t g_serial;
static mixer_sound g_sounds[MIXER_MAX_SOUNDS];
static int g_nsounds;
static char g_err[768] = "no error";
static mixer_music_fn g_music_fn;
static void *g_music_user;

/* Distance volume, 0.5 u per entry (index = dist >> 15):
 *   i <= 4 (2 u): 64;  i >= 48 (24 u): 0;  else (64 * (48 - i) + 22) / 44
 * i.e. linear from 64 at 2 u to 0 at 24 u, rounded. */
static const uint8_t dist_table[64] = {
    64, 64, 64, 64, 64, 63, 61, 60, 58, 57, 55, 54, 52, 51, 49, 48,
    47, 45, 44, 42, 41, 39, 38, 36, 35, 33, 32, 31, 29, 28, 26, 25,
    23, 22, 20, 19, 17, 16, 15, 13, 12, 10,  9,  7,  6,  4,  3,  1,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void mixer_init(const char *assets_dir)
{
    mixer_shutdown();
    if (assets_dir)
        files_init(assets_dir);
    memset(g_voices, 0, sizeof g_voices);
    g_serial = 0;
    atomic_store(&g_ring_head, 0);
    atomic_store(&g_ring_tail, 0);
    g_music_fn = NULL;
    g_music_user = NULL;
}

void mixer_shutdown(void)
{
    for (int i = 0; i < g_nsounds; i++)
        bast_snd_free(g_sounds[i].snd);
    memset(g_sounds, 0, sizeof g_sounds);
    g_nsounds = 0;
    memset(g_voices, 0, sizeof g_voices);
}

mixer_sound *mixer_add_sound(const char *name, bast_snd *snd)
{
    if (!snd)
        return NULL;
    if (g_nsounds >= MIXER_MAX_SOUNDS) {
        bast_snd_free(snd);
        return NULL;
    }
    mixer_sound *s = &g_sounds[g_nsounds++];
    snprintf(s->name, sizeof s->name, "%s", name);
    s->snd = snd;
    return s;
}

mixer_sound *mixer_load_snd(const char *name)
{
    for (int i = 0; i < g_nsounds; i++)
        if (strcmp(g_sounds[i].name, name) == 0)
            return &g_sounds[i];
    char path[640];
    snprintf(path, sizeof path, "sfx/%s.snd", name);
    int64_t size = 0;
    uint8_t *d = files_read(path, &size);
    if (!d) {
        snprintf(g_err, sizeof g_err, "%s", files_error());
        return NULL;
    }
    bast_snd *snd = bast_snd_from_memory(d, size);
    free(d);
    if (!snd) {
        snprintf(g_err, sizeof g_err, "%s: %s", path, bast_error());
        return NULL;
    }
    mixer_sound *s = mixer_add_sound(name, snd);
    if (!s)
        snprintf(g_err, sizeof g_err, "%s: too many sounds", path);
    return s;
}

const char *mixer_error(void)
{
    return g_err;
}

uint32_t mixer_sound_length(const mixer_sound *s)
{
    return s ? s->snd->count : 0;
}

/* ---- command ring (game thread -> audio thread) ---------------------- */

static bool post(command c)
{
    unsigned head = atomic_load_explicit(&g_ring_head, memory_order_relaxed);
    unsigned tail = atomic_load_explicit(&g_ring_tail, memory_order_acquire);
    if (head - tail >= RING_SIZE)
        return false;
    g_ring[head & (RING_SIZE - 1)] = c;
    atomic_store_explicit(&g_ring_head, head + 1, memory_order_release);
    return true;
}

bool mixer_start(mixer_sound *sound, int source_id, int vol, int pan)
{
    if (!sound)
        return false;
    return post((command){CMD_START, source_id, vol, pan, sound});
}

bool mixer_update(int source_id, int vol, int pan)
{
    return post((command){CMD_UPDATE, source_id, vol, pan, NULL});
}

bool mixer_stop_source(int source_id)
{
    return post((command){CMD_STOP, source_id, 0, 0, NULL});
}

void mixer_set_music(mixer_music_fn fn, void *user)
{
    g_music_fn = fn;
    g_music_user = user;
}

/* ---- audio thread ----------------------------------------------------- */

static void set_levels(voice *v, int vol, int pan)
{
    v->vol = clampi(vol, 0, MIXER_VOL_MAX);
    v->pan = clampi(pan, MIXER_PAN_LEFT, MIXER_PAN_RIGHT);
    v->gain_l = v->pan <= 0 ? 64 : 64 * (4 - v->pan) / 4;
    v->gain_r = v->pan >= 0 ? 64 : 64 * (4 + v->pan) / 4;
}

/* Voice for a new sound: the source's own voice (cut), else a free one,
 * else the quietest, ties broken by the oldest. */
static voice *pick_voice(int source_id)
{
    if (source_id != MIXER_SOURCE_NONE)
        for (int i = 0; i < MIXER_VOICES; i++)
            if (g_voices[i].active && g_voices[i].source_id == source_id)
                return &g_voices[i];
    for (int i = 0; i < MIXER_VOICES; i++)
        if (!g_voices[i].active)
            return &g_voices[i];
    voice *best = &g_voices[0];
    for (int i = 1; i < MIXER_VOICES; i++) {
        voice *v = &g_voices[i];
        if (v->vol < best->vol || (v->vol == best->vol && v->serial < best->serial))
            best = v;
    }
    return best;
}

static void apply(const command *c)
{
    switch (c->type) {
    case CMD_START: {
        voice *v = pick_voice(c->source_id);
        v->active = true;
        v->source_id = c->source_id;
        v->sound = c->sound;
        v->pos = 0;
        v->serial = g_serial++;
        set_levels(v, c->vol, c->pan);
        break;
    }
    case CMD_UPDATE:
        if (c->source_id == MIXER_SOURCE_NONE)
            break;
        for (int i = 0; i < MIXER_VOICES; i++)
            if (g_voices[i].active && g_voices[i].source_id == c->source_id)
                set_levels(&g_voices[i], c->vol, c->pan);
        break;
    case CMD_STOP:
        if (c->source_id == MIXER_SOURCE_NONE)
            break;
        for (int i = 0; i < MIXER_VOICES; i++)
            if (g_voices[i].source_id == c->source_id)
                g_voices[i].active = false;
        break;
    default:
        break;
    }
}

static void drain(void)
{
    unsigned tail = atomic_load_explicit(&g_ring_tail, memory_order_relaxed);
    unsigned head = atomic_load_explicit(&g_ring_head, memory_order_acquire);
    while (tail != head) {
        apply(&g_ring[tail & (RING_SIZE - 1)]);
        tail++;
    }
    atomic_store_explicit(&g_ring_tail, tail, memory_order_release);
}

#define CHUNK 256

void mixer_render(int16_t *out, int frames)
{
    drain();
    int32_t acc[CHUNK * 2];
    while (frames > 0) {
        int n = frames < CHUNK ? frames : CHUNK;
        memset(acc, 0, sizeof(int32_t) * 2 * (size_t)n);

        for (int vi = 0; vi < MIXER_VOICES; vi++) {
            voice *v = &g_voices[vi];
            if (!v->active)
                continue;
            const bast_snd *snd = v->sound->snd;
            uint32_t left = snd->count - v->pos;
            int m = (uint32_t)n < left ? n : (int)left;
            /* Per-channel multiplier, 0..4096; result >> 12 (= / (64 * 64)). */
            int32_t ml = v->vol * v->gain_l, mr = v->vol * v->gain_r;
            const int8_t *src = snd->samples + v->pos;
            for (int k = 0; k < m; k++) {
                int32_t s = (int32_t)src[k] * 256;
                acc[2 * k] += (s * ml) >> 12;
                acc[2 * k + 1] += (s * mr) >> 12;
            }
            v->pos += (uint32_t)m;
            if (v->pos >= snd->count)
                v->active = false;
        }

        if (g_music_fn)
            g_music_fn(acc, n, g_music_user);

        for (int k = 0; k < 2 * n; k++)
            out[k] = (int16_t)clampi(acc[k], INT16_MIN, INT16_MAX);
        out += 2 * n;
        frames -= n;
    }
}

/* ---- spatialisation --------------------------------------------------- */

int mixer_dist_volume(fix_t dist)
{
    if (dist < 0)
        dist = 0;
    int32_t i = dist >> 15; /* 0.5 u per entry */
    return i >= 64 ? 0 : dist_table[i];
}

int mixer_pan(angle_t view, angle_t to_source)
{
    /* sin > 0: source counter-clockwise of the view = on the left. */
    fix_t s = fine_sin((angle_t)(to_source - view));
    int p = (int)((s * 4 + FIX_ONE / 2) >> FIX_SHIFT);
    return clampi(-p, MIXER_PAN_LEFT, MIXER_PAN_RIGHT);
}

void mixer_get_voice(int i, mixer_voice_info *out)
{
    const voice *v = &g_voices[i];
    out->active = v->active;
    out->source_id = v->source_id;
    out->vol = v->vol;
    out->pan = v->pan;
    out->pos = v->pos;
    out->serial = v->serial;
    out->sound = v->sound;
}
