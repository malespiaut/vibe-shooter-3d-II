#ifndef GAME_DEMO_H
#define GAME_DEMO_H

/* Demos (tech.md Demos): text files, `#` comments, header lines
 * (`vsdemo 1`, `level`, `skill`, `seed`, `map`, `on_death`) then body commands, each
 * consuming tics. This module parses them, plays them back one ticcmd per
 * tic, and records live input back into raw `in` lines.
 *
 * Body commands are parsed through one dispatch table (demo.c `body_cmds`);
 * high-level commands (goto, face, expect, ...) are added there. */

#include <stddef.h>
#include <stdint.h>

#include "game/state.h"

enum {
    DEMO_CMD_IN,        /* raw ticcmd held for n tics */
    DEMO_CMD_WAIT,      /* n tics of no input */
    DEMO_CMD_GOTO,      /* to tile (tx, ty), or waypoint `name`; fails after n tics */
    DEMO_CMD_FACE,      /* to angle `angle`, or to tile (tx, ty) when has_tile */
    DEMO_CMD_EXPECT,    /* assert stat op value (no tic) */
    DEMO_CMD_FACE_NEAREST, /* turn to the nearest visible live enemy */
    DEMO_CMD_FIRE_NEAREST, /* face and shoot it until it dies; fails after n tics */
    DEMO_CMD_CLEAR,        /* fight visible enemies within `value` units; fails after n */
    /* use, weapon and fire are DEMO_CMD_IN with a fixed ticcmd. */
};

/* expect ops */
enum { DEMO_OP_EQ, DEMO_OP_NE, DEMO_OP_LT, DEMO_OP_LE, DEMO_OP_GT, DEMO_OP_GE };

#define DEMO_NAME_MAX 17 /* waypoint names: 16 chars + NUL (map entity names) */
#define DEMO_GOTO_DEFAULT_MAX 1050

typedef struct {
    int kind;
    int line; /* source line, for messages */
    int32_t n;
    ticcmd cmd;
    /* goto / face / expect tile */
    int32_t tx, ty;
    int has_tile;
    char name[DEMO_NAME_MAX]; /* goto @name; expect: the stat name */
    uint16_t angle;           /* face <deg> */
    int op;                   /* expect */
    int32_t value;            /* expect */
} demo_cmd;

#define DEMO_MAP_NAME_MAX 64

typedef struct {
    int level;    /* default 1 */
    int skill;    /* default 2 */
    uint32_t seed;
    char map[DEMO_MAP_NAME_MAX]; /* "" or a map name: maps/<map>.vmap */
    int on_death_restart; /* `on_death restart`: a death does not end the run */

    demo_cmd *cmds;
    int count, cap;

    /* Playback cursor. */
    int pc;
    int32_t done; /* tics consumed by cmds[pc] */
    uint16_t target; /* face/fire nearest, clear: thinker id being fought, 0 = none yet */
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
 * state (high-level commands read it). Zero-tic commands (expect, a goto
 * or face already satisfied) run before the tic they precede; at the end of
 * the demo pending ones still run against the final state. */
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
