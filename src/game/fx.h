#ifndef GAME_FX_H
#define GAME_FX_H

/* Short-lived impact effects (GAME.md 6.3, weapons.md): hitscan puffs and
 * blood hits. A fixed-size ring in game state, spawned only from game_tic
 * (weapon fire), counted down by fx_tic, hashed (game_hash), and read by
 * the renderer through fx_render_things. Explosions are not in this list:
 * they are the explosion thinkers projectiles turn into (game/projectile.h),
 * and fx_render_things draws those too. Fixed point only. */

#include <stdint.h>

#include "engine/fixed.h"
#include "engine/render.h"

struct game_state;

#define MAX_FX 64
/* Puffs sit this far in front of the wall the trace hit, so the sprite is
 * not clipped by the wall's own depth. */
#define FX_WALL_PULLBACK ((fix_t)4096) /* 1/16 u */
#define FX_HIT_Z ((fix_t)32768)            /* puffs: eye height, 0.5 u */
#define FX_EXPLOSION_MIN_Z ((fix_t)32768)  /* explosions centred no lower */

/* Frame counts of the fx sprites the game animates (assets/src/fx/). */
#define FX_PUFF_FRAMES 4
#define FX_BLOOD_FRAMES 4
#define FX_SCARAB_FRAMES 4
#define FX_PLASMA_BOLT_FRAMES 4

enum {
    FX_NONE = 0,
    FX_PUFF = 1,  /* hitscan on a wall or a barrel: fx/puff_hit */
    FX_BLOOD = 2, /* hitscan or claws on an enemy: fx/blood_hit */
    FX_KINDS
};

typedef struct {
    uint8_t kind; /* FX_*, FX_NONE = free slot */
    int32_t tics; /* left; the effect is freed when it reaches 0 */
    fix_t x, y;   /* centre, at eye height (hitscan is level) */
} fx_t;

typedef struct {
    fx_t list[MAX_FX];
    uint32_t spawned; /* effects spawned since game_init; next slot = spawned % MAX_FX */
} fx_list_t;

/* Lifetime of `kind` in tics: its sprite's frames x FX_TICS_PER_FRAME
 * (game/projectile.h); 0 for an unknown kind. */
int32_t fx_lifetime(int kind);

/* Spawns `kind` centred at (x, y): the slot spawned % MAX_FX, which is free
 * or holds the oldest effect (that one is replaced). */
void fx_spawn(struct game_state *gs, int kind, fix_t x, fix_t y);

/* Counts every effect down; one at 0 is freed. game_tic calls it first, so
 * an effect spawned during tic T is drawn after tics T .. T + lifetime - 1:
 * exactly its lifetime. */
void fx_tic(struct game_state *gs);

/* Number of live effects. */
int fx_count(const struct game_state *gs);

/* Appends to `out` (from index n, up to `max`) every projectile and
 * explosion thinker, then every live effect, as render_things of the fx
 * sprites (sprite.h FXS_*) with their frame and height. Read only. Returns
 * the new count. */
int fx_render_things(const struct game_state *gs, render_thing *out, int n, int max);

#endif
