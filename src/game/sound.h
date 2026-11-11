#ifndef GAME_SOUND_H
#define GAME_SOUND_H

/* Sound events (docs/design/audio.md). The simulation never talks to the
 * mixer: it appends events to gs->sounds, a per-tic queue that game_tic
 * empties at the start of every tic, and the main loop drains it after the
 * tic into the mixer (window) or a --sound-log file (headless). The queue
 * is not part of game_hash: sound never changes the simulation. */

#include <stdint.h>

#include "engine/fixed.h"

typedef struct game_state game_state;

enum {
#define SFX(name) SFX_##name,
#include "game/sfx_names.h"
#undef SFX
    SFX_COUNT
};

#define SOUND_QUEUE_MAX 64

typedef struct {
    uint16_t id;   /* SFX_* */
    uint8_t local; /* 1: player/UI sound, no attenuation or pan */
    fix_t x, y;    /* world position (positional sounds) */
} sound_event;

typedef struct {
    int count;
    sound_event ev[SOUND_QUEUE_MAX];
} sound_queue;

/* Name of sound `id` ("door_open"), NULL when out of range. */
const char *sfx_name(int id);

/* Queues a positional sound at (x, y). Events past SOUND_QUEUE_MAX in one
 * tic are dropped. */
void game_sound(game_state *gs, int sfx_id, fix_t x, fix_t y);

/* Queues a player/UI sound: no distance volume, centred. */
void game_sound_local(game_state *gs, int sfx_id);

#endif
