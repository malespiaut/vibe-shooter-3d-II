#ifndef ENGINE_SPRITE_H
#define ENGINE_SPRITE_H

/* Sprite frame selection (GAME.md 3, docs/design/tech.md World: Sprites,
 * Sprite naming). Integer only: gameplay code may call it. */

#include <stdint.h>

#include "engine/angle.h"

enum {
    SPRITE_DIRS = 8,
    SPRITE_MAX_FRAMES = 8, /* frames per animation and direction (gib: 8) */
    SPRITE_SHIMMER_TICS = 18 /* items.md: 2-frame 18-tic shimmer */
};

/* Enemy animations (docs/design/enemies.md, Animation list), in the order
 * of sprite_anim_name() and sprite_enemy_frames(). Items only have
 * SPR_IDLE. */
enum {
    SPR_IDLE,
    SPR_WALK,
    SPR_WINDUP,
    SPR_ATTACK,
    SPR_PAIN,
    SPR_DEATH,
    SPR_GIB,
    SPR_ANIMS
};

/* Name of animation `anim` in sprite files ("idle", "walk", ...). */
const char *sprite_anim_name(int anim);

/* Frames per direction of enemy animation `anim` (enemies.md): idle 2,
 * walk 4, windup 2, attack 2, pain 1, death 5, gib 8. */
int sprite_enemy_frames(int anim);

/* Direction frame 0..7 for a thing facing `thing_angle`, seen by a viewer
 * that lies in direction `viewer_angle` from the thing (the angle of the
 * vector thing -> viewer). Naming contract: d0 = the thing faces the camera,
 * then the camera moves counter-clockwise around it in 45 degree steps
 * (d2 = camera on the thing's left, d4 = its back, d6 = its right).
 * Each direction covers [k*45 - 22.5, k*45 + 22.5) degrees of
 * viewer_angle - thing_angle. */
int sprite_dir(angle_t viewer_angle, angle_t thing_angle);

/* World sprite of a map entity type: frames are
 * `<name>_idle_d<dir>_f<frame>.tex` under assets/gen/sprites/ (or test/),
 * frame 0..frames-1, dir 0..7 (d1..d7 may be missing: things without
 * facing). `live` sprites are drawn from live game state (pickups: a taken
 * one disappears), never from the static map entity list. `enemy` sprites
 * have every animation of sprite_enemy_frames(): `<name>_<anim>_d<d>_f<f>`
 * (gib frames under assets/gen/gibs/, d0 only), and the caller picks the
 * animation and frame (game/thinker.h thinker_anim()). `no_gib` enemies
 * (the boss, enemies.md) have every animation but `gib`. */
typedef struct {
    const char *name; /* may hold a subdirectory: "items/fish" */
    int frames;       /* idle frames, 1..SPRITE_MAX_FRAMES */
    int live;
    int enemy;
    int no_gib;
} sprite_def;

/* Sprite of map entity `type`, or NULL if that type has no sprite yet.
 * Every pickup has one (items.md), except the Ankh Pistol (32): the player
 * starts with it and it has no world pickup art. */
const sprite_def *sprite_def_for_entity(int type);

/* Base name of sprite_def_for_entity(type), or NULL. */
const char *sprite_name_for_entity(int type);

/* Effect sprites (GAME.md 6.3, tools/fx.py): projectiles, explosions and
 * impacts, frames `<name>_d0_f<frame>.tex` under assets/gen/fx/ (one
 * direction). A render_thing whose type is SPRITE_FX_BASE + FXS_* draws
 * one, with the frame the game picked (render_thing.frame). `fullbright`
 * ones are drawn with colormap row 0 whatever the light (energy: projectiles
 * and explosions); the others are shaded like any sprite, their fb_* pixels
 * staying bright through the colormap. */
enum { SPRITE_FX_BASE = 0x4000 };
enum {
    FXS_SCARAB_PROJ,
    FXS_EXPLOSION_SMALL,
    FXS_EXPLOSION_BIG,
    FXS_PUFF,
    FXS_BLOOD,
    FXS_COUNT
};
#define FX_SPRITE_MAX_FRAMES 16

typedef struct {
    const char *name; /* "puff_hit": fx/puff_hit_d0_f<n>.tex */
    int frames;       /* 1..FX_SPRITE_MAX_FRAMES */
    int fullbright;
} fx_sprite_def;

/* Effect sprite FXS_*, or NULL. */
const fx_sprite_def *fx_sprite_def_for(int id);

/* Idle frame shown at render tic `tic` by a sprite with `frames` frames:
 * SPRITE_SHIMMER_TICS tics per frame, looping. */
int sprite_anim_frame(uint32_t tic, int frames);

#endif
