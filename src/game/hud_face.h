#ifndef GAME_HUD_FACE_H
#define GAME_HUD_FACE_H

/* Status-bar cat face selection (hud.md "Cat face"). Render side: reads a
 * summary of game state each tic, never writes game state, and its idle
 * glances come from its own RNG (never the gameplay one). */

#include <stdint.h>

#include "engine/angle.h"
#include "engine/fixed.h"
#include "engine/rng.h"

enum {
    FACE_IDLE0, /* eyes centre */
    FACE_IDLE1, /* eyes left */
    FACE_IDLE2, /* eyes right */
    FACE_HURTL,
    FACE_HURTR,
    FACE_HURTF,
    FACE_BIGHIT,
    FACE_FURY,
    FACE_SMUG,
    FACE_FRENZY,
    FACE_DEAD,
    FACE_GOD,
    FACE_EXPR_COUNT
};

#define FACE_HURT_TICS 18   /* "damage this tic" held long enough to see */
#define FACE_BIGHIT_TICS 35
#define FACE_BIGHIT_DAMAGE 20
#define FACE_SMUG_TICS 35
#define FACE_FURY_TICS 70   /* fire held this many tics in a row */
#define FACE_GLANCE_MIN 70  /* idle glance every 70..140 tics */
#define FACE_GLANCE_MAX 140
#define FACE_GLANCE_TICS 17 /* how long a glance lasts */

/* What the face reacts to this tic. */
typedef struct {
    int32_t health;
    int dead;
    int god;        /* dev flag */
    int frenzy;     /* Catnip Frenzy active */
    int fire;       /* fire held this tic */
    int hurt;       /* FACE_HURTL/R/F if damaged this tic, else 0 */
    int32_t hurt_amount; /* biggest single hit this tic */
    int smug;       /* weapon picked up this tic */
} hud_face_input;

typedef struct {
    int tier;       /* 1..5, 0 = dead */
    int expr;       /* FACE_* shown */
    int timed;      /* FACE_HURT*, FACE_BIGHIT, FACE_SMUG or -1 */
    int timed_tics; /* left for `timed` */
    int fire_held;  /* consecutive tics */
    int glance;     /* FACE_IDLE0..2 */
    int glance_wait, glance_tics;
    rng_t rng;      /* render RNG for glances */
} hud_face;

/* Health tier: 80+ -> 1, 60-79 -> 2, 40-59 -> 3, 20-39 -> 4, 1-19 -> 5,
 * 0 or less -> 0 (dead). */
int hud_face_tier(int32_t health);

/* Hurt expression from the damage source as seen from the player at
 * (px, py) facing `view`: within 45 degrees of straight ahead -> FACE_HURTF,
 * else FACE_HURTL (source to the left, counter-clockwise) or FACE_HURTR.
 * No source (has_src 0) or a source on the player -> FACE_HURTF. */
int hud_face_hurt_dir(angle_t view, fix_t px, fix_t py, int has_src, fix_t sx, fix_t sy);

void hud_face_init(hud_face *f, uint32_t seed);
/* One tic: updates timers and picks f->tier and f->expr. Priority: dead,
 * god, big hit, hurt, smug, fury, frenzy, idle (with glances). */
void hud_face_tic(hud_face *f, const hud_face_input *in);

/* TEX name of the current face ("face_t1_idle0", "face_dead", ...). */
void hud_face_name(const hud_face *f, char *buf, int size);

#endif
