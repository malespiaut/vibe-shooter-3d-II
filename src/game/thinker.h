#ifndef GAME_THINKER_H
#define GAME_THINKER_H

/* Thinkers (tech.md State hash): the generic actor list. A fixed-capacity
 * array in creation order; a thinker's id is its index + 1 and never
 * changes (thinkers are never removed, dead ones stay as corpses), so ids,
 * not pointers, are what other thinkers and the hash refer to. Id 0 means
 * "none". Enemy behaviour lives in game/ai.c, stats in game/enemy_info.c. */

#include <stdint.h>

#include "bast.h"
#include "engine/angle.h"
#include "engine/fixed.h"

#define MAX_THINKERS 256

enum {
    TH_STATE_STAND = 0,  /* alive, idle (asleep); weapon pickups: on the floor */
    TH_STATE_DEAD = 1,   /* corpse: no longer hit, never removed */
    TH_STATE_TAKEN = 2,  /* picked-up item: inert, never removed */
    TH_STATE_CHASE = 3,  /* awake, moving toward the player (game/ai.c) */
    TH_STATE_WINDUP = 4, /* telegraph: stopped, facing the player */
    TH_STATE_ATTACK = 5, /* attack resolved, recovering */
    TH_STATE_PAIN = 6,   /* flinch */
    TH_STATE_DYING = 7,  /* killed barrel: explodes when `tics` runs out */
    TH_STATE_FREE = 8,   /* spent projectile or effect: slot reusable */
};

/* thinker_t.movedir: 8 compass directions, counter-clockwise from east. */
enum { DI_EAST, DI_NE, DI_NORTH, DI_NW, DI_WEST, DI_SW, DI_SOUTH, DI_SE, DI_NONE };

/* Thinker types that are not map entities (projectiles and effects,
 * game/projectile.h). Above every bast entity type number. */
enum {
    TH_TYPE_SCARAB_PROJ = 256,
    TH_TYPE_EXPLOSION_SMALL = 257,
    TH_TYPE_EXPLOSION_BIG = 258,
    TH_TYPE_PLASMA_BOLT = 259, /* Pomeranian bolt (fx plasma_bolt) */
    TH_TYPE_PLASMA_HIT = 260,  /* its impact effect (fx plasma_hit) */
    TH_TYPE_RETICLE = 261,     /* Dachshund mortar mark (fx reticle): the
                                * shell lands when `tics` runs out */
    TH_TYPE_BONE_PROJ = 262,   /* Mastiff Juggernaut bone missile */
};

/* Owner id of projectiles the player fired (thinker ids are 1..MAX_THINKERS). */
#define TH_ID_PLAYER 0xFFFFu

/* Kibble barrel (items.md Decorations): solid, 0.25 u radius, 20 HP. It
 * blocks the player like an enemy while thinker_alive, so it stops
 * blocking as soon as it is DYING, BARREL_FUSE_TICS before it explodes. */
#define BARREL_HP 20
#define BARREL_RADIUS ((fix_t)16384)
#define BARREL_FUSE_TICS 4 /* from the killing hit to the explosion */

/* thinker_t.flags */
enum {
    TH_FLAG_COUNTKILL = 1 << 0, /* counts toward kills total */
    TH_FLAG_GIB = 1 << 1,       /* died at HP <= -(max HP / 2); never the boss */
    TH_FLAG_SHOOTABLE = 1 << 2, /* hitscan and melee can hit it (enemies) */
    TH_FLAG_PICKUP = 1 << 3,    /* touched by the player (weapon pickups) */
    TH_FLAG_TRIGGER = 1 << 4,   /* pickup with PICKUP_TRIGGER: fires `tag` */
    TH_FLAG_AMBUSH = 1 << 5,    /* entity AMBUSH (deaf): wakes on sight only */
    TH_FLAG_RETREAT = 1 << 6,   /* Dachshund backing off (game/ai.c) */
};

typedef struct {
    uint16_t type;  /* enum bast_entity_type */
    uint8_t state;  /* TH_STATE_* */
    uint8_t flags;  /* TH_FLAG_* */
    fix_t x, y;
    angle_t angle;
    int32_t tics;   /* tics left in the current state */
    int32_t health;
    int32_t max_health;
    fix_t radius;   /* half the width in enemies.md */
    uint16_t target; /* thinker id, 0 = none */
    uint8_t tag;     /* entity tag: pickup trigger, or enemy KILL_TRIGGER group (static, not hashed) */
    uint8_t movedir; /* DI_*: compass direction of the step being walked */
    uint8_t goal_tx, goal_ty; /* tile whose centre the step walks to */
    int32_t movecount; /* tics to the next chase decision (every 8) */
    fix_t momx, momy; /* velocity per tic (projectiles) */
    fix_t z;          /* height above the floor; render-only, not hashed */
    uint16_t owner;   /* projectile: thinker id that fired it, or TH_ID_PLAYER */
    uint8_t howler_hit; /* set to 1 by every Sphinx Howler hit (game/weapon.c);
                         * the Shieldbearer's 70-tic shield drop reads and
                         * clears it. */
    uint32_t died_tic; /* game tic of the killing hit (DEAD enemies; hashed then) */
    int32_t shield_tics; /* Shieldbearer: tics left of a Howler shield drop */
    uint8_t shoulder;    /* Mastiff: shoulder of the next missile, 0 left, 1 right */
} thinker_t;

typedef struct {
    thinker_t list[MAX_THINKERS];
    int count;
    int32_t kills, kill_total;
} thinkers_t;

/* 1 for a live enemy or barrel: any state but DEAD, TAKEN, DYING and FREE;
 * never a pickup, projectile or effect (type >= TH_TYPE_SCARAB_PROJ). Doors,
 * pushwalls, player collision, hitscan, splash and AI use it. */
int thinker_alive(const thinker_t *t);

/* 1 when entity `e` is placed at `skill` (1..3): its SKILLn flag is set.
 * The one skill-placement test, for thinkers and pickups (game/items.c). */
int entity_on_skill(const bast_map_entity *e, int skill);

/* Empties the list and spawns every enemy, weapon-pickup and kibble-barrel
 * entity of `map` flagged for `skill` (1..3), in entity order. Enemies keep
 * AMBUSH (TH_FLAG_AMBUSH). Weapon pickups keep the
 * entity's tag and PICKUP_TRIGGER (TH_FLAG_TRIGGER). Other item types live
 * in game_state.pickups (game/items.c). */
void thinkers_spawn_map(thinkers_t *th, const bast_map *map, int skill);

/* Appends an enemy; returns its id, or 0 when the list is full or `type`
 * is not an enemy. */
uint16_t thinker_spawn(thinkers_t *th, uint16_t type, fix_t x, fix_t y, angle_t angle);

/* Appends a weapon pickup (BAST_ENT_ANKH_PISTOL..SPHINX_HOWLER); returns
 * its id, or 0 when full or `type` is not a weapon. */
uint16_t thinker_spawn_pickup(thinkers_t *th, uint16_t type, fix_t x, fix_t y);

/* Appends a kibble barrel (shootable, solid, not a kill); returns its id,
 * or 0 when full. */
uint16_t thinker_spawn_barrel(thinkers_t *th, fix_t x, fix_t y);

/* A zeroed thinker for a projectile or effect: the first TH_STATE_FREE
 * slot, else a new one at the end. Returns its id, 0 when full. Only
 * transient thinkers ever become FREE, so ids that other thinkers keep
 * (targets, owners of live projectiles) stay valid. */
uint16_t thinker_alloc(thinkers_t *th);

/* NULL for id 0 or an id that does not exist. */
thinker_t *thinker_get(thinkers_t *th, uint16_t id);

/* Applies `damage` to a live thinker: at HP <= 0 it becomes dead (counted
 * as a kill when it counts) and gibs at HP <= -(max HP / 2). A barrel
 * instead becomes TH_STATE_DYING with BARREL_FUSE_TICS on its fuse
 * (projectiles_tic explodes it). Returns 1 when this hit killed it. */
int thinker_damage(thinkers_t *th, thinker_t *t, int32_t damage);

/* Render-side animation timing (enemies.md, Animation list). */
#define TH_ANIM_IDLE_TICS 18  /* idle: 2 frames */
#define TH_ANIM_WALK_TICS 6   /* walk: 4 frames, advancing only while moving */
#define TH_ANIM_DEATH_TICS 5  /* death: 5 frames, frame 4 is the corpse */
#define TH_ANIM_GIB_TICS 4    /* gib: 8 frames, the last one stays */

/* Sprite animation (SPR_*, engine/sprite.h) and frame an enemy shows at
 * game tic `tic`. Reads the thinker only: drawing never changes the state
 * hash. STAND -> idle, CHASE -> walk (frame 0 while not stepping), WINDUP ->
 * windup (glow frame 1 in the last half of the enemy's wind-up), ATTACK ->
 * attack (frame 0 for the first half of the recovery), PAIN -> pain, DEAD
 * -> death from died_tic then frame 4 forever, or gib with TH_FLAG_GIB. */
void thinker_anim(const thinker_t *t, uint32_t tic, int *anim, int *frame);

#endif
