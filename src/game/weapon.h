#ifndef GAME_WEAPON_H
#define GAME_WEAPON_H

/* Player weapons (weapons.md Stats, Behaviour notes). Slots 1..7; this
 * module fires 1-4 (Claws, Ankh Pistol, Hairball Scattergun, Sistrum
 * Repeater). Hitscan: DDA ray against blocking tiles (game_tile_blocks with
 * live door state: a door that is not fully open blocks, an open one lets
 * shots through), then the nearest live thinker whose radius the ray
 * crosses. All maths is fixed point; spread and damage come from the
 * gameplay RNG. */

#include <stdint.h>

#include "engine/angle.h"
#include "engine/fixed.h"

enum {
    WP_NONE = 0,
    WP_CLAWS = 1,
    WP_PISTOL = 2,
    WP_SCATTERGUN = 3,
    WP_REPEATER = 4,
    WP_LAUNCHER = 5,
    WP_LANCE = 6,
    WP_HOWLER = 7,
    WP_SLOTS = 8, /* index 0 unused */
};

enum {
    AM_NONE = -1,
    AM_CHARGES = 0, /* Ankh charges */
    AM_SHELLS = 1,  /* Yarn shells */
    AM_SCARABS = 2,
    AM_CELLS = 3, /* Sun cells */
    AM_TYPES = 4,
};

#define WEAPON_SWITCH_TICS 10  /* lower 5, raise 5; fire blocked */
#define WEAPON_FLASH_TICS 3    /* muzzle flash, weapons 2-4 and 6 */
#define HITSCAN_RANGE ((fix_t)(32 * 65536))
#define CLAW_REACH ((fix_t)49152) /* 0.75 units */
#define PLAYER_START_CHARGES 50
#define PICKUP_RADIUS ((fix_t)32768) /* 0.5 u from the player centre */

typedef struct {
    int8_t ammo_type;   /* AM_* */
    uint8_t ammo_use;   /* per shot */
    uint8_t interval;   /* tics between shots */
    uint8_t pellets;    /* hitscan rays per shot */
    int32_t dmg_base, dmg_spread;
    angle_t spread;     /* max horizontal offset per ray, angle units */
    uint8_t flash;      /* lights the view when fired */
} weapon_info_t;

extern const weapon_info_t weapon_info[WP_SLOTS];

/* Player weapon state, hashed in this order (state.c game_hash). */
typedef struct {
    uint8_t owned;        /* bit n = slot n owned */
    uint8_t ready;        /* slot in hand */
    uint8_t pending;      /* slot being raised, WP_NONE when not switching */
    uint8_t switch_tics;  /* > 0 while switching (fire blocked) */
    uint8_t refire_tics;  /* > 0 until the ready weapon may fire again */
    uint8_t flash_tics;   /* muzzle flash left, for the renderer */
    angle_t bob_phase;    /* view-bob phase, for the renderer */
    int32_t ammo[AM_TYPES];
    uint32_t shots;       /* shots fired (each pellet volley is one shot) */
} weapons_t;

struct game_state;

/* Claws and pistol owned, pistol ready, 50 charges. */
void weapons_init(weapons_t *w);

/* 1 when slot `wp` has the ammo for one shot (Claws always do). */
int weapon_has_ammo(const weapons_t *w, int wp);

/* Highest owned slot with ammo, never 5 or 7 (weapons.md). */
int weapon_auto_pick(const weapons_t *w);

/* Ammo cap without the Litter Satchel (weapons.md Ammo types). */
extern const int32_t ammo_max[AM_TYPES];

/* Ammo cap of type `a`: ammo_max[a], doubled with the Litter Satchel
 * (`satchel` != 0). */
int32_t ammo_cap(int a, int satchel);

/* The one ammo-giving rule, for ammo, weapon and Satchel pickups: adds `n`
 * of type `a` (x1.5 rounded down when `kitten`, tech.md Skill), capped by
 * ammo_cap(a, satchel). Returns 1 when the player was below the cap (the
 * pickup is used), 0 when full (it stays). */
int weapon_add_ammo(weapons_t *w, int a, int32_t n, int kitten, int satchel);

/* Weapon pickup (weapons.md Behaviour notes): owns slot `wp` and adds its
 * pickup ammo through weapon_add_ammo; a weapon already owned gives only
 * the ammo. The weapon in hand does not change. Returns 1 when anything
 * was taken (a weapon owned with full ammo stays on the floor). */
int weapon_give_ex(weapons_t *w, int wp, int kitten, int satchel);

/* weapon_give_ex at skill 2 without the Satchel. */
int weapon_give(weapons_t *w, int wp);

/* Starts switching to `wp` (no-op if not owned or already in hand). */
void weapon_select(weapons_t *w, int wp);

/* One tic of the weapon: switch progress, timers, bob, and firing when
 * `fire` is held. Called by game_tic after the player moved and after
 * items_touch took the pickups (weapon pickups included). */
void weapon_tic(struct game_state *gs, int select_slot, int fire);

/* Distance along the unit vector (dx, dy) from (x, y) to the first tile
 * that blocks (game_tile_blocks: live doors), or `range` if none is reached
 * within it; 0 when (x, y) is itself in a blocking tile. Grid DDA. Shots
 * and enemy sight share it. */
fix_t game_trace_wall(const struct game_state *gs, fix_t x, fix_t y, fix_t dx, fix_t dy,
                      fix_t range);

/* Traces one ray from (x, y) at angle `a` up to `range`: returns the id of
 * the nearest live thinker hit before any blocking tile, or 0. *wall_dist
 * (if not NULL) gets the distance to the first blocking tile, capped at
 * `range`. For melee, a thinker counts as hit when its near edge is within
 * `range` (`edge` = 1); for hitscan its centre's projection must be
 * (`edge` = 0). */
uint16_t weapon_trace(struct game_state *gs, fix_t x, fix_t y, angle_t a, fix_t range, int edge,
                      fix_t *wall_dist);

#endif
