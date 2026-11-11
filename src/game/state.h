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
#include "game/thinker.h"
#include "game/weapon.h"
#include "game/door.h"
#include "game/tags.h"

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
 * a digit in the buttons field. */
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

/* Armour absorption class (items.md Armour). */
enum { ARMOUR_NONE, ARMOUR_LINEN, ARMOUR_PLATE };

typedef struct {
    fix_t x, y;       /* world units, 16.16, y grows north */
    angle_t angle;
    fix_t momx, momy; /* velocity per tic */
    int32_t health;
    int32_t armour;
    weapons_t weapons;        /* owned slots, ammo (game/weapon.h) */
    uint32_t keys;            /* KEY_GREEN | KEY_BLUE | KEY_VIOLET (door.h) */
    int32_t armour_class;     /* ARMOUR_* */
    int32_t satchel;          /* Litter Satchel carried: ammo caps x2 */
    int32_t catnip_tics;      /* Catnip Frenzy left, 0 = off */
    int32_t catseye_tics;     /* Cat's Eye Amulet left, 0 = off */
    int32_t ninth_life;       /* 0 or 1 */
    int32_t invuln_tics;      /* after Ninth Life saved the player */
    int32_t dead;
} player_t;

/* A non-weapon pickup spawned from a map entity (game/items.c), or dropped
 * by an enemy (game/ai.c). Weapon pickups are thinkers (TH_FLAG_PICKUP). */
typedef struct {
    uint16_t type;  /* enum bast_entity_type, 48..95 */
    uint16_t flags; /* entity flags (PICKUP_TRIGGER) */
    uint8_t tag;
    uint8_t taken;
    uint8_t dropped; /* spawned by a dying enemy: not counted in items */
    int32_t amount;  /* ammo given, 0 = the pickup's normal amount */
    fix_t x, y;
} pickup_t;

#define MAX_PICKUPS 1024

/* Event hook for the HUD (hud.md: pickup messages, border flash, face).
 * The simulation appends; the renderer reads what is new since its last
 * look (event_seq). Never hashed, never read by gameplay. */
enum { GEV_PICKUP = 1, GEV_DAMAGE = 2 };
typedef struct {
    uint8_t kind;    /* GEV_* */
    uint8_t has_src; /* GEV_DAMAGE: 1 if src_x/src_y is the damage source */
    uint16_t type;   /* GEV_PICKUP: entity type taken */
    int32_t amount;  /* GEV_DAMAGE: damage after skill, before armour */
    fix_t src_x, src_y;
} game_event;
#define GAME_EVENT_RING 16

typedef struct game_state {
    uint32_t tic;
    rng_t rng;               /* the gameplay RNG (tech.md Simulation) */
    const bast_map *map;     /* not owned, never hashed (static data) */
    player_t player;
    thinkers_t things; /* every thinker, creation order */
    uint8_t prev_buttons;    /* last tic's buttons, for rising edges */
    int door_count;
    door_t doors[MAX_DOORS]; /* row-major order of the DOOR cells */
    int16_t door_index[64 * 64]; /* cell y*width+x -> door, -1 = none */
    int32_t skill;     /* 1 Kitten, 2 Tomcat, 3 Sabretooth */

    int32_t pickup_count;    /* non-weapon pickups spawned, entity order */
    pickup_t pickups[MAX_PICKUPS];
    int32_t items_got;       /* level counter: pickups taken, weapons too */
    int32_t items_total;     /* pickups + weapon pickups spawned */

    int pushwall_count;
    pushwall_t pushwalls[MAX_PUSHWALLS]; /* row-major order of PUSHWALL cells */
    uint8_t cell_fired[64 * 64]; /* cell y*width+x -> FIRED_* bits (tags.h) */
    int32_t secrets, secret_total; /* SECRET cells entered / in the map */
    uint32_t tags_fired; /* tags fired so far (every non-zero fire) */
    uint8_t lights_alt;  /* a LIGHTS entity fired: cells use dark_alt */
    uint8_t exit;        /* an EXIT cell was used: the level run ends */
    int32_t last_tag;    /* last non-zero tag fired (tests); not hashed */

    /* HUD events (not hashed): events[(seq-1) % GAME_EVENT_RING] is the
     * newest, event_seq counts every event since game_init. */
    uint32_t event_seq;
    game_event events[GAME_EVENT_RING];
} game_state;

/* Resets the state for `map` (borrowed, must outlive the state): tic 0, RNG
 * seeded for (level, demo_seed), player at the map's PLAYER_START (centre of
 * the map, angle 0, if none), health 100, armour 0, at rest, Claws and
 * pistol with 50 charges; every enemy and weapon-pickup entity flagged for
 * `skill` (1..3, clamped) spawned as a thinker, every other pickup as a
 * pickup_t (game/items.c). Kitten (1): half damage, 1.5x ammo. */
void game_init(game_state *gs, const bast_map *map, int level, int skill, uint32_t demo_seed);

/* Advances the simulation by one tic. */
void game_tic(game_state *gs, const ticcmd *cmd);

/* 64-bit FNV-1a over the canonical serialisation (tech.md State hash):
 * tic, RNG state, player (x, y, angle, momx, momy, health, armour, weapon
 * state, ammo), every thinker in creation order (type, x, y, angle, state,
 * flags, tics, health, target id), kills and kill total, then player keys,
 * previous buttons, door count and every door (state, pos, timer), then
 * armour class, satchel, powerup timers, Ninth Life, invulnerability, dead, skill,
 * pickups (count, then each taken flag in spawn order), items taken, then
 * the map logic (tags_hash: exit, lights, secrets, tags fired, pushwalls,
 * one-shot cell states), then
 * per thinker its AI fields (movedir, goal tile, movecount). New
 * fields are appended as the systems that own them land. */
uint64_t game_hash(const game_state *gs);

/* True when tile (tx, ty) blocks the player: outside the map, SOLID,
 * BLOCK_ALL, a DOOR that is not fully open (live door state), or a tile a
 * pushwall occupies (live pushwall state; a PUSHWALL cell it left is free). */
int game_tile_blocks(const game_state *gs, int tx, int ty);

/* True when a circle of radius r at (x, y) overlaps a blocking tile
 * (touching is not overlapping). */
int game_circle_blocked(const game_state *gs, fix_t x, fix_t y, fix_t r);

/* True when the circle overlaps tile (tx, ty) (touching is not overlapping). */
int game_circle_overlaps_tile(fix_t x, fix_t y, fix_t r, int tx, int ty);

/* Grants key bits (KEY_*, door.h). Key pickups set the same bits
 * (items.c); tests use this directly. */
void game_give_keys(game_state *gs, uint32_t keys);

/* Appends a HUD event (overwrites the oldest of the ring). */
void game_post_event(game_state *gs, const game_event *ev);

#endif
