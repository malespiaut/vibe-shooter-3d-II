#ifndef GAME_STATE_H
#define GAME_STATE_H

/* Game state and the 35 Hz simulation step (tech.md Simulation, World:
 * Player). Gameplay maths is fixed point only (engine/fixed.h, angle.h,
 * rng.h); nothing here reads the clock or the platform. */

#include <stdint.h>

#include "bast.h"
#include "engine/angle.h"
#include "engine/fixed.h"
#include "engine/rng.h"

#define TICRATE 35

/* World: Player (tech.md). Speeds are per tic, 16.16. */
#define PLAYER_RADIUS ((fix_t)16384)    /* 0.25 units */
#define PLAYER_WALK_SPEED ((fix_t)7490) /* 4 u/s */
#define PLAYER_RUN_SPEED ((fix_t)13107) /* 7 u/s */
#define PLAYER_TURN_SPEED 1872          /* angle units per tic, 180 deg/s */
#define PLAYER_ACCEL_TICS 4             /* to max speed, and to stop */
#define PLAYER_START_HEALTH 100

/* ticcmd buttons. */
enum {
    BT_FIRE = 1 << 0,
    BT_USE = 1 << 1,
    BT_RUN = 1 << 2,
};
/* Weapon slot select 1..7 in bits 3..5 (0 = no change); demos write it as
 * a digit in the buttons field. No weapons exist yet, game_tic ignores it. */
#define BT_WEAPON_SHIFT 3
#define BT_WEAPON_MASK (7 << BT_WEAPON_SHIFT)

#define TICCMD_MAX 127

/* One tic of player input. fwd/side are -127..127: a fraction of the walk
 * speed, or of the run speed when BT_RUN is held. side > 0 strafes right.
 * turn is in angle units per tic, positive = counter-clockwise (left). */
typedef struct {
    int8_t fwd;
    int8_t side;
    int16_t turn;
    uint8_t buttons;
} ticcmd;

typedef struct {
    fix_t x, y;       /* world units, 16.16, y grows north */
    angle_t angle;
    fix_t momx, momy; /* velocity per tic */
    int32_t health;
    int32_t armour;
} player_t;

typedef struct {
    uint32_t tic;
    rng_t rng;               /* the gameplay RNG (tech.md Simulation) */
    const bast_map *map;     /* not owned, never hashed (static data) */
    player_t player;
} game_state;

/* Resets the state for `map` (borrowed, must outlive the state): tic 0, RNG
 * seeded for (level, demo_seed), player at the map's PLAYER_START (centre of
 * the map, angle 0, if none), health 100, armour 0, at rest. */
void game_init(game_state *gs, const bast_map *map, int level, uint32_t demo_seed);

/* Advances the simulation by one tic. */
void game_tic(game_state *gs, const ticcmd *cmd);

/* 64-bit FNV-1a over the canonical serialisation (tech.md State hash):
 * tic, RNG state, player (x, y, angle, momx, momy, health, armour). New
 * fields are appended as the systems that own them land. */
uint64_t game_hash(const game_state *gs);

/* True when tile (tx, ty) blocks the player: outside the map, SOLID,
 * BLOCK_ALL, PUSHWALL, or a DOOR that is not START_OPEN (no door motion
 * exists yet, so a door is in its initial state). */
int game_tile_blocks(const bast_map *map, int tx, int ty);

/* True when a circle of radius r at (x, y) overlaps a blocking tile
 * (touching is not overlapping). */
int game_circle_blocked(const bast_map *map, fix_t x, fix_t y, fix_t r);

#endif
