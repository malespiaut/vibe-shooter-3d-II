#ifndef GAME_DEMO_H
#define GAME_DEMO_H

/* Demos (tech.md Demos): text files, `#` comments, header lines
 * (`vsdemo 1`, `level`, `skill`, `seed`, `map`) then body commands, each
 * consuming tics. This module parses them, plays them back one ticcmd per
 * tic, and records live input back into raw `in` lines.
 *
 * Body commands are parsed through one dispatch table (demo.c `body_cmds`);
 * high-level commands (goto, face, expect, ...) are added there. */

#include <stddef.h>
#include <stdint.h>

#include "game/state.h"

enum {
    DEMO_CMD_IN,   /* raw ticcmd held for n tics */
    DEMO_CMD_WAIT, /* n tics of no input */
};

typedef struct {
    int kind;
    int line; /* source line, for messages */
    int32_t n;
    ticcmd cmd;
} demo_cmd;

#define DEMO_MAP_NAME_MAX 64

typedef struct {
    int level;    /* default 1 */
    int skill;    /* default 2 */
    uint32_t seed;
    char map[DEMO_MAP_NAME_MAX]; /* "" or a map name: maps/<map>.vmap */

    demo_cmd *cmds;
    int count, cap;

    /* Playback cursor. */
    int pc;
    int32_t done; /* tics consumed by cmds[pc] */
} demo_t;

/* Parses demo text (NUL-terminated). On error returns NULL and writes
 * "line N: message" into err. */
demo_t *demo_parse(const char *text, char *err, size_t errlen);
void demo_free(demo_t *d);

enum {
    DEMO_TIC = 0,  /* *out holds the input for this tic */
    DEMO_END = 1,  /* no more commands */
    DEMO_FAIL = 2, /* a command failed (expect, timeout); message in err */
};

/* Produces the input for the next tic from the demo and the current game
 * state (high-level commands read it). */
int demo_next(demo_t *d, const game_state *gs, ticcmd *out, char *err, size_t errlen);

/* Formats the buttons field of an `in` line ("-" when none). buf >= 8. */
void demo_format_buttons(uint8_t buttons, char *buf);

/* Recorder: collects one ticcmd per tic, merging equal consecutive tics
 * into one `in` line. */
typedef struct {
    int32_t *counts;
    ticcmd *cmds;
    int len, cap;
} demo_rec;

void demo_rec_init(demo_rec *r);
int demo_rec_add(demo_rec *r, const ticcmd *cmd); /* 0 on allocation failure */
/* Writes the header (map line omitted when map is NULL or "") and the
 * `in` lines. Returns 1 on success. */
int demo_rec_write(const demo_rec *r, const char *path, int level, int skill, uint32_t seed,
                   const char *map);
void demo_rec_free(demo_rec *r);

#endif
