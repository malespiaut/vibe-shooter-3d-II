#ifndef ENGINE_MUSIC_H
#define ENGINE_MUSIC_H

/* 4-channel pattern music sequencer (docs/design/audio.md, "Music").
 *
 * Songs are `.vmus` text files (format in audio.md). The sequencer renders
 * into the mixer's 4 reserved music channels through mixer_set_music():
 * call music_attach() once after mixer_init(). Rows advance on the 35 Hz tic
 * clock, MUSIC_TIC_SAMPLES (= 22050 / 35) samples per tic. Integer only.
 *
 * Threads: music_parse/music_load/music_free run on the game thread;
 * music_play/music_stop post a request that the audio thread picks up at the
 * start of its next render call. A song must stay alive while it plays. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MUSIC_CHANNELS 4
#define MUSIC_ROWS 64
#define MUSIC_MAX_PATTERNS 64
#define MUSIC_MAX_INSTS 32  /* ids 01..1f */
#define MUSIC_MAX_ORDER 128
#define MUSIC_TIC_SAMPLES 630 /* 22050 Hz / 35 Hz */

enum { MUSIC_WAVE_SQUARE, MUSIC_WAVE_TRIANGLE, MUSIC_WAVE_NOISE, MUSIC_WAVE_PLUCK };

#define MUSIC_NOTE_NONE 0
#define MUSIC_NOTE_OFF 255
#define MUSIC_VOL_NONE 255

typedef struct {
    bool defined;
    uint8_t wave;
    uint8_t vol;      /* 0..64 */
    uint8_t a, d, r;  /* tics */
    uint8_t s;        /* sustain level 0..64 */
    uint8_t duty;     /* square: high fraction duty/256 */
    uint8_t mode;     /* noise: 0 long LFSR, 1 short (metallic) */
    uint16_t damp;    /* pluck: feedback 0..256, 256 = plain averaging */
} music_inst;

typedef struct {
    uint8_t note;  /* MIDI number 12..119 (C-0..B-8), NONE or OFF */
    uint8_t inst;  /* 1..31, 0 = none */
    uint8_t vol;   /* 0..64 or MUSIC_VOL_NONE */
    char fx;       /* 0, 'A', 'U', 'D', 'V' */
    uint8_t fxp;   /* effect parameter byte */
} music_cell;

typedef struct {
    int tempo; /* tics per row */
    music_inst inst[MUSIC_MAX_INSTS];
    int npatterns;
    music_cell pat[MUSIC_MAX_PATTERNS][MUSIC_ROWS][MUSIC_CHANNELS];
    bool pat_defined[MUSIC_MAX_PATTERNS];
    int norder;
    uint8_t order[MUSIC_MAX_ORDER];
    int loop; /* order index to jump to after the last entry */
} music_song;

/* Parses .vmus text. NULL on error, with "line N: message" in err. */
music_song *music_parse(const char *text, char *err, size_t errlen);
/* Reads and parses a file. NULL on error (message in err). */
music_song *music_load(const char *path, char *err, size_t errlen);
void music_free(music_song *song);

/* Samples from the start of the song to the start of order entry `idx`. */
uint32_t music_order_start(const music_song *song, int idx);

/* Installs the sequencer as the mixer's music hook. */
void music_attach(void);
/* Starts `song` from order 0 (replaces any playing song). */
void music_play(const music_song *song);
void music_stop(void);
/* Mixer hook body: adds `frames` stereo frames into acc. */
void music_render(int32_t *acc, int frames);

#endif
