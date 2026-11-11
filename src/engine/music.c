/* 4-channel pattern music sequencer. See music.h and docs/design/audio.md.
 * Integer arithmetic only: oscillators are 32-bit phase accumulators. */
#include "engine/music.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/mixer.h"

/* ---- pitch ------------------------------------------------------------ *
 * Pitch is in fine units: 64 per semitone, 768 per octave, 0 = MIDI note 0
 * (C-1, 8.1758 Hz). Phase increment for a 32-bit accumulator at 22050 Hz:
 *   inc(p) = f(p) * 2^32 / 22050,  f(p) = 440 * 2^((p - 69*64) / 768)
 * Computed as inc(p) = INC0_Q8 * 2^(k/768) >> (8 - oct), with oct = p / 768,
 * k = p % 768 and
 *   INC0_Q8   = round(8.175798915643707 * 2^32 / 22050 * 2^8) = 407681904
 *   RATIO_Q32 = round(2^32 * 2^(1/768))                       = 4298845406
 * The 768-entry table g_octave[k] = 2^(k/768) in Q30 is built at attach time
 * by repeated rounded multiplication by RATIO_Q32 (error < 1e-6 relative). */
#define INC0_Q8 407681904ULL
#define RATIO_Q32 4298845406ULL
#define FINE_PER_OCT 768
#define PITCH_MAX (127 * 64)

static uint32_t g_octave[FINE_PER_OCT];

static void build_octave_table(void)
{
    uint64_t t = 1ULL << 30;
    for (int k = 0; k < FINE_PER_OCT; k++) {
        g_octave[k] = (uint32_t)t;
        t = (t * RATIO_Q32 + (1ULL << 31)) >> 32;
    }
}

static uint32_t pitch_inc(int p)
{
    if (p < 0)
        p = 0;
    if (p > PITCH_MAX)
        p = PITCH_MAX;
    int oct = p / FINE_PER_OCT, k = p % FINE_PER_OCT;
    return (uint32_t)((INC0_Q8 * g_octave[k]) >> (30 + 8 - oct));
}

/* ---- parser ----------------------------------------------------------- */

static int perr(char *err, size_t errlen, int line, const char *msg, const char *tok)
{
    if (err && errlen) {
        if (tok)
            snprintf(err, errlen, "line %d: %s '%s'", line, msg, tok);
        else
            snprintf(err, errlen, "line %d: %s", line, msg);
    }
    return 0;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* Two hex digits exactly; -1 if malformed. */
static int hex2(const char *s)
{
    if (strlen(s) != 2)
        return -1;
    int a = hexval(s[0]), b = hexval(s[1]);
    return a < 0 || b < 0 ? -1 : a * 16 + b;
}

/* Non-negative decimal integer; -1 if malformed. */
static int decval(const char *s)
{
    if (!*s || strlen(s) > 6)
        return -1;
    int v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return -1;
        v = v * 10 + (*s - '0');
    }
    return v;
}

static int parse_note(const char *t)
{
    if (strcmp(t, "---") == 0)
        return MUSIC_NOTE_NONE;
    if (strcmp(t, "===") == 0)
        return MUSIC_NOTE_OFF;
    static const int semi[7] = {9, 11, 0, 2, 4, 5, 7}; /* A..G */
    if (strlen(t) != 3 || t[0] < 'A' || t[0] > 'G' || (t[1] != '-' && t[1] != '#') ||
        t[2] < '0' || t[2] > '8')
        return -1;
    int s = semi[t[0] - 'A'];
    if (t[1] == '#') {
        if (t[0] == 'E' || t[0] == 'B')
            return -1;
        s++;
    }
    return (t[2] - '0' + 1) * 12 + s;
}

#define MAX_TOK (MUSIC_MAX_ORDER + 1)

/* Splits on spaces, tabs and '|'. Returns MAX_TOK + 1 on too many tokens. */
static int split(char *s, char **tok)
{
    int n = 0;
    char *p = s;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '|')
            *p++ = 0;
        if (!*p)
            return n;
        if (n == MAX_TOK)
            return MAX_TOK + 1;
        tok[n++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '|')
            p++;
    }
}

static int parse_row(music_song *s, int pat, int row, char **tok, int n, int line,
                     char *err, size_t errlen)
{
    if (n != 4 * MUSIC_CHANNELS)
        return perr(err, errlen, line, "row needs 4 cells of 4 fields (note inst vol fx)", NULL);
    for (int c = 0; c < MUSIC_CHANNELS; c++) {
        char **f = tok + 4 * c;
        music_cell *cell = &s->pat[pat][row][c];
        int note = parse_note(f[0]);
        if (note < 0)
            return perr(err, errlen, line, "bad note", f[0]);
        cell->note = (uint8_t)note;
        if (strcmp(f[1], "..") == 0) {
            cell->inst = 0;
        } else {
            int i = hex2(f[1]);
            if (i < 1 || i >= MUSIC_MAX_INSTS)
                return perr(err, errlen, line, "bad instrument", f[1]);
            if (!s->inst[i].defined)
                return perr(err, errlen, line, "undefined instrument", f[1]);
            cell->inst = (uint8_t)i;
        }
        if (strcmp(f[2], "..") == 0) {
            cell->vol = MUSIC_VOL_NONE;
        } else {
            int v = hex2(f[2]);
            if (v < 0 || v > 64)
                return perr(err, errlen, line, "bad volume (00..40 hex)", f[2]);
            cell->vol = (uint8_t)v;
        }
        if (strcmp(f[3], "...") == 0) {
            cell->fx = 0;
            cell->fxp = 0;
        } else {
            char e = f[3][0];
            int p = strlen(f[3]) == 3 ? hex2(f[3] + 1) : -1;
            if ((e != 'A' && e != 'U' && e != 'D' && e != 'V') || p < 0)
                return perr(err, errlen, line, "bad effect", f[3]);
            cell->fx = e;
            cell->fxp = (uint8_t)p;
        }
    }
    return 1;
}

static int parse_inst(music_song *s, char **tok, int n, int line, char *err, size_t errlen)
{
    if (n < 3)
        return perr(err, errlen, line, "inst needs: inst <id> <wave> [key=value...]", NULL);
    int id = hex2(tok[1]);
    if (id < 1 || id >= MUSIC_MAX_INSTS)
        return perr(err, errlen, line, "bad instrument id (01..1f)", tok[1]);
    music_inst in = {.defined = true, .vol = 64, .s = 64, .duty = 128, .damp = 256};
    static const char *waves[] = {"square", "triangle", "noise", "pluck"};
    int w = -1;
    for (int i = 0; i < 4; i++)
        if (strcmp(tok[2], waves[i]) == 0)
            w = i;
    if (w < 0)
        return perr(err, errlen, line, "unknown wave", tok[2]);
    in.wave = (uint8_t)w;
    for (int i = 3; i < n; i++) {
        char *eq = strchr(tok[i], '=');
        if (!eq)
            return perr(err, errlen, line, "expected key=value", tok[i]);
        *eq = 0;
        int v = decval(eq + 1);
        const char *k = tok[i];
        int lim = !strcmp(k, "vol") || !strcmp(k, "s")             ? 64
                  : !strcmp(k, "a") || !strcmp(k, "d") || !strcmp(k, "r") ? 255
                  : !strcmp(k, "duty")                              ? 255
                  : !strcmp(k, "mode")                              ? 1
                  : !strcmp(k, "damp")                              ? 256
                                                                    : -1;
        if (lim < 0)
            return perr(err, errlen, line, "unknown instrument key", k);
        if (v < 0 || v > lim)
            return perr(err, errlen, line, "value out of range for", k);
        if (!strcmp(k, "vol"))
            in.vol = (uint8_t)v;
        else if (!strcmp(k, "s"))
            in.s = (uint8_t)v;
        else if (!strcmp(k, "a"))
            in.a = (uint8_t)v;
        else if (!strcmp(k, "d"))
            in.d = (uint8_t)v;
        else if (!strcmp(k, "r"))
            in.r = (uint8_t)v;
        else if (!strcmp(k, "duty"))
            in.duty = (uint8_t)v;
        else if (!strcmp(k, "mode"))
            in.mode = (uint8_t)v;
        else
            in.damp = (uint16_t)v;
    }
    if (in.wave == MUSIC_WAVE_SQUARE && in.duty == 0)
        return perr(err, errlen, line, "duty must be 1..255", NULL);
    s->inst[id] = in;
    return 1;
}

music_song *music_parse(const char *text, char *err, size_t errlen)
{
    if (err && errlen)
        err[0] = 0;
    music_song *s = calloc(1, sizeof *s);
    if (!s) {
        perr(err, errlen, 0, "out of memory", NULL);
        return NULL;
    }
    s->norder = 0;
    s->loop = 0;
    int line = 0, cur_pat = -1, rows = 0, pat_line = 0, order_line = 0, loop_line = 0;
    bool header = false, have_loop = false;
    const char *p = text;
    char buf[512];
    char *tok[MAX_TOK];
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        line++;
        if (len >= sizeof buf) {
            perr(err, errlen, line, "line too long", NULL);
            goto fail;
        }
        memcpy(buf, p, len);
        buf[len] = 0;
        p = e ? e + 1 : p + len;
        char *hash = strchr(buf, '#');
        /* '#' starts a comment unless it is a sharp inside a note (C#4). */
        while (hash && hash > buf && hash[-1] >= 'A' && hash[-1] <= 'G')
            hash = strchr(hash + 1, '#');
        if (hash)
            *hash = 0;
        int n = split(buf, tok);
        if (n > MAX_TOK) {
            perr(err, errlen, line, "too many tokens", NULL);
            goto fail;
        }
        if (n == 0)
            continue;

        if (cur_pat >= 0 && rows < MUSIC_ROWS) {
            if (!parse_row(s, cur_pat, rows, tok, n, line, err, errlen))
                goto fail;
            rows++;
            continue;
        }
        if (!header) {
            if (n != 2 || strcmp(tok[0], "vmus") != 0 || strcmp(tok[1], "1") != 0) {
                perr(err, errlen, line, "expected header 'vmus 1'", NULL);
                goto fail;
            }
            header = true;
        } else if (!strcmp(tok[0], "tempo")) {
            int t = n == 2 ? decval(tok[1]) : -1;
            if (t < 1 || t > 32) {
                perr(err, errlen, line, "tempo must be 1..32 tics per row", NULL);
                goto fail;
            }
            s->tempo = t;
        } else if (!strcmp(tok[0], "inst")) {
            if (!parse_inst(s, tok, n, line, err, errlen))
                goto fail;
        } else if (!strcmp(tok[0], "pattern")) {
            int k = n == 2 ? decval(tok[1]) : -1;
            if (k < 0 || k >= MUSIC_MAX_PATTERNS) {
                perr(err, errlen, line, "bad pattern number", n == 2 ? tok[1] : NULL);
                goto fail;
            }
            if (s->pat_defined[k]) {
                perr(err, errlen, line, "pattern defined twice", tok[1]);
                goto fail;
            }
            s->pat_defined[k] = true;
            if (k + 1 > s->npatterns)
                s->npatterns = k + 1;
            cur_pat = k;
            rows = 0;
            pat_line = line;
        } else if (!strcmp(tok[0], "order")) {
            order_line = line;
            for (int i = 1; i < n; i++) {
                int k = decval(tok[i]);
                if (k < 0 || k >= MUSIC_MAX_PATTERNS || s->norder >= MUSIC_MAX_ORDER) {
                    perr(err, errlen, line, "bad order entry", tok[i]);
                    goto fail;
                }
                s->order[s->norder++] = (uint8_t)k;
            }
        } else if (!strcmp(tok[0], "loop")) {
            int k = n == 2 ? decval(tok[1]) : -1;
            if (k < 0) {
                perr(err, errlen, line, "bad loop index", NULL);
                goto fail;
            }
            s->loop = k;
            have_loop = true;
            loop_line = line;
        } else {
            perr(err, errlen, line, "unknown directive", tok[0]);
            goto fail;
        }
    }
    if (cur_pat >= 0 && rows < MUSIC_ROWS) {
        snprintf(buf, sizeof buf, "pattern %d has %d rows, expected 64", cur_pat, rows);
        perr(err, errlen, pat_line, buf, NULL);
        goto fail;
    }
    if (!header) {
        perr(err, errlen, line, "expected header 'vmus 1'", NULL);
        goto fail;
    }
    if (s->tempo == 0) {
        perr(err, errlen, line, "missing tempo", NULL);
        goto fail;
    }
    if (s->norder == 0) {
        perr(err, errlen, line, "missing order list", NULL);
        goto fail;
    }
    for (int i = 0; i < s->norder; i++)
        if (!s->pat_defined[s->order[i]]) {
            snprintf(buf, sizeof buf, "order entry %d names undefined pattern %d", i, s->order[i]);
            perr(err, errlen, order_line, buf, NULL);
            goto fail;
        }
    if (have_loop && s->loop >= s->norder) {
        perr(err, errlen, loop_line, "loop index past the order list", NULL);
        goto fail;
    }
    return s;
fail:
    free(s);
    return NULL;
}

music_song *music_load(const char *path, char *err, size_t errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err && errlen)
            snprintf(err, errlen, "%s: cannot open", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = size >= 0 ? malloc((size_t)size + 1) : NULL;
    if (!text || fread(text, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(text);
        if (err && errlen)
            snprintf(err, errlen, "%s: read error", path);
        return NULL;
    }
    fclose(f);
    text[size] = 0;
    char e[200];
    music_song *s = music_parse(text, e, sizeof e);
    free(text);
    if (!s && err && errlen)
        snprintf(err, errlen, "%s: %s", path, e);
    return s;
}

void music_free(music_song *song)
{
    free(song);
}

uint32_t music_order_start(const music_song *song, int idx)
{
    return (uint32_t)idx * MUSIC_ROWS * (uint32_t)song->tempo * MUSIC_TIC_SAMPLES;
}

/* ---- playback (audio thread) ----------------------------------------- */

enum { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

#define ENV_FULL (64 << 8) /* envelope level, Q8 */
#define KS_MAX 1024        /* pluck delay line: down to 22050/1024 = 21.5 Hz */
#define NOISE_SEED 0x4a35u /* every note restarts the noise from here */

typedef struct {
    const music_inst *inst;
    int note;     /* MIDI base note */
    int vol;      /* channel volume 0..64 from the volume column */
    int env_stage, env_t, env, env_from;
    int slide;    /* fine units, persistent until the next note */
    char fx;
    int fxp;
    int vib_phase;
    uint32_t phase, inc;
    uint32_t lfsr;
    int32_t gain;      /* Q16 of 0..16384 */
    int32_t gain_step; /* per sample, toward this tic's target */
    int ks_len, ks_pos;
    int16_t ks[KS_MAX];
} chan;

static struct {
    const music_song *song;
    int order, row, row_tic;
    int tic_left; /* samples left in the current tic */
    chan ch[MUSIC_CHANNELS];
} g_seq;

static _Atomic(const music_song *) g_req_song;
static atomic_uint g_req_serial;
static unsigned g_seen_serial;

static void music_mixer_hook(int32_t *acc, int frames, void *user)
{
    (void)user;
    music_render(acc, frames);
}

static void seq_reset(const music_song *song)
{
    memset(&g_seq, 0, sizeof g_seq);
    g_seq.song = song;
}

void music_attach(void)
{
    build_octave_table();
    seq_reset(NULL);
    atomic_store(&g_req_song, NULL);
    atomic_store(&g_req_serial, 0);
    g_seen_serial = 0;
    mixer_set_music(music_mixer_hook, NULL);
}

void music_play(const music_song *song)
{
    atomic_store_explicit(&g_req_song, song, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_req_serial, 1, memory_order_release);
}

void music_stop(void)
{
    music_play(NULL);
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static uint32_t lfsr_step(uint32_t l, int mode)
{
    uint32_t fb = (l ^ (l >> (mode ? 6 : 1))) & 1u;
    return (l >> 1) | (fb << 14);
}

static int ks_length(uint32_t inc)
{
    if (inc == 0)
        return KS_MAX;
    uint64_t len = ((1ULL << 32) + inc / 2) / inc;
    return len < 2 ? 2 : len > KS_MAX ? KS_MAX : (int)len;
}

static void note_on(chan *c, int note, int vol)
{
    c->note = note;
    c->vol = vol;
    c->env_stage = ENV_ATTACK;
    c->env_t = 0;
    c->env = 0;
    c->slide = 0;
    c->vib_phase = 0;
    c->phase = 0;
    c->lfsr = NOISE_SEED;
    c->gain = 0;
    c->gain_step = 0;
    c->inc = pitch_inc(note * 64);
    if (c->inst && c->inst->wave == MUSIC_WAVE_PLUCK) {
        /* Excitation: a burst of noise, softened by one 2-tap average. */
        c->ks_len = ks_length(c->inc);
        c->ks_pos = 0;
        uint32_t l = NOISE_SEED;
        for (int i = 0; i < c->ks_len; i++) {
            l = lfsr_step(l, 0);
            c->ks[i] = (l & 1u) ? 30000 : -30000;
        }
        int16_t first = c->ks[0];
        for (int i = 0; i < c->ks_len; i++) {
            int16_t next = i + 1 < c->ks_len ? c->ks[i + 1] : first;
            c->ks[i] = (int16_t)((c->ks[i] + next) / 2);
        }
    }
}

static void row_cell(chan *c, const music_cell *cell, const music_song *s)
{
    if (cell->inst)
        c->inst = &s->inst[cell->inst];
    c->fx = cell->fx;
    c->fxp = cell->fxp;
    if (cell->note == MUSIC_NOTE_OFF) {
        if (c->env_stage != ENV_OFF) {
            c->env_stage = ENV_RELEASE;
            c->env_t = 0;
            c->env_from = c->env;
        }
    } else if (cell->note != MUSIC_NOTE_NONE) {
        note_on(c, cell->note, cell->vol == MUSIC_VOL_NONE ? 64 : cell->vol);
    } else if (cell->vol != MUSIC_VOL_NONE) {
        c->vol = cell->vol;
    }
}

static void env_tic(chan *c)
{
    const music_inst *in = c->inst;
    int sus = in->s << 8;
    switch (c->env_stage) {
    case ENV_ATTACK:
        if (in->a == 0 || ++c->env_t >= in->a) {
            c->env = ENV_FULL;
            c->env_stage = ENV_DECAY;
            c->env_t = 0;
        } else {
            c->env = ENV_FULL * c->env_t / in->a;
        }
        break;
    case ENV_DECAY:
        if (in->d == 0 || ++c->env_t >= in->d) {
            c->env = sus;
            c->env_stage = ENV_SUSTAIN;
        } else {
            c->env = ENV_FULL - (ENV_FULL - sus) * c->env_t / in->d;
        }
        break;
    case ENV_SUSTAIN:
        c->env = sus;
        break;
    case ENV_RELEASE:
        if (in->r == 0 || ++c->env_t >= in->r) {
            c->env = 0;
            c->env_stage = ENV_OFF;
        } else {
            c->env = c->env_from * (in->r - c->env_t) / in->r;
        }
        break;
    default:
        c->env = 0;
        break;
    }
}

/* Per-tic update of one channel: effects -> pitch, envelope -> gain ramp. */
static void chan_tic(chan *c, int row_tic)
{
    if (!c->inst) {
        c->gain = c->gain_step = 0;
        return;
    }
    int semis = 0, vib = 0;
    switch (c->fx) {
    case 'A': {
        int step = row_tic % 3;
        semis = step == 1 ? c->fxp >> 4 : step == 2 ? c->fxp & 15 : 0;
        break;
    }
    case 'U':
        c->slide += c->fxp;
        break;
    case 'D':
        c->slide -= c->fxp;
        break;
    case 'V': {
        /* Triangle LFO, 64 steps per cycle: speed x steps per tic, depth y
         * sixteenths of a semitone (= 4 fine units each). */
        c->vib_phase = (c->vib_phase + (c->fxp >> 4)) & 63;
        int ph = c->vib_phase;
        int tri = ph < 16 ? ph : ph < 48 ? 32 - ph : ph - 64; /* -16..16 */
        vib = tri * (c->fxp & 15) / 4;
        break;
    }
    default:
        break;
    }
    c->slide = clampi(c->slide, -PITCH_MAX, PITCH_MAX);
    c->inc = pitch_inc(c->note * 64 + semis * 64 + c->slide + vib);
    if (c->inst->wave == MUSIC_WAVE_PLUCK) {
        c->ks_len = ks_length(c->inc);
        if (c->ks_pos >= c->ks_len)
            c->ks_pos = 0;
    }

    env_tic(c);
    /* Gain 0..16384 = env (Q8, 0..16384) * inst vol * channel vol / 4096. */
    int32_t target = (int32_t)(((int64_t)c->env * c->inst->vol * c->vol) >> 12);
    c->gain_step = ((target << 16) - c->gain) / MUSIC_TIC_SAMPLES;
}

static void seq_tic(void)
{
    const music_song *s = g_seq.song;
    if (g_seq.row_tic == 0) {
        int pat = s->order[g_seq.order];
        for (int i = 0; i < MUSIC_CHANNELS; i++)
            row_cell(&g_seq.ch[i], &s->pat[pat][g_seq.row][i], s);
    }
    for (int i = 0; i < MUSIC_CHANNELS; i++)
        chan_tic(&g_seq.ch[i], g_seq.row_tic);
    if (++g_seq.row_tic >= s->tempo) {
        g_seq.row_tic = 0;
        if (++g_seq.row >= MUSIC_ROWS) {
            g_seq.row = 0;
            if (++g_seq.order >= s->norder)
                g_seq.order = s->loop;
        }
    }
    g_seq.tic_left = MUSIC_TIC_SAMPLES;
}

/* Oscillator output, about +-32767. */
static int32_t osc(chan *c)
{
    const music_inst *in = c->inst;
    int32_t v;
    switch (in->wave) {
    case MUSIC_WAVE_SQUARE:
        v = (c->phase >> 24) < in->duty ? 32767 : -32767;
        c->phase += c->inc;
        break;
    case MUSIC_WAVE_TRIANGLE: {
        /* 32-step triangle, 16 levels. */
        int u = (int)(c->phase >> 27);
        int t = u < 16 ? u : 31 - u;
        v = (t * 2 - 15) * 2184;
        c->phase += c->inc;
        break;
    }
    case MUSIC_WAVE_NOISE: {
        /* LFSR clocked 16 times per pitch period, at most once per sample. */
        uint32_t old = c->phase;
        c->phase += c->inc;
        if ((c->phase >> 28) != (old >> 28) || c->inc >= (1u << 28))
            c->lfsr = lfsr_step(c->lfsr, in->mode);
        v = (c->lfsr & 1u) ? 32767 : -32767;
        break;
    }
    default: { /* pluck: Karplus-Strong on the noise burst */
        int len = c->ks_len, i = c->ks_pos;
        int j = i + 1 < len ? i + 1 : 0;
        v = c->ks[i];
        c->ks[i] = (int16_t)(((c->ks[i] + c->ks[j]) * (int32_t)in->damp) >> 9);
        c->ks_pos = j;
        break;
    }
    }
    return v;
}

static void render_span(int32_t *acc, int n)
{
    for (int i = 0; i < MUSIC_CHANNELS; i++) {
        chan *c = &g_seq.ch[i];
        if (!c->inst || ((c->gain >> 16) == 0 && c->gain_step == 0))
            continue;
        for (int k = 0; k < n; k++) {
            c->gain += c->gain_step;
            int32_t g = c->gain >> 16;
            /* 4 channels at full scale sum to about +-32767. */
            int32_t s = (osc(c) * g) >> 16;
            acc[2 * k] += s;
            acc[2 * k + 1] += s;
        }
    }
}

void music_render(int32_t *acc, int frames)
{
    unsigned serial = atomic_load_explicit(&g_req_serial, memory_order_acquire);
    if (serial != g_seen_serial) {
        g_seen_serial = serial;
        seq_reset(atomic_load_explicit(&g_req_song, memory_order_relaxed));
    }
    if (!g_seq.song)
        return;
    while (frames > 0) {
        if (g_seq.tic_left == 0)
            seq_tic();
        int n = frames < g_seq.tic_left ? frames : g_seq.tic_left;
        render_span(acc, n);
        acc += 2 * n;
        frames -= n;
        g_seq.tic_left -= n;
    }
}

