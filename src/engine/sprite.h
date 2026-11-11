#ifndef ENGINE_SPRITE_H
#define ENGINE_SPRITE_H

/* Sprite frame selection (GAME.md 3, docs/design/tech.md World: Sprites,
 * Sprite naming). Integer only: gameplay code may call it. */

#include <stdint.h>

#include "engine/angle.h"

enum {
    SPRITE_DIRS = 8,
    SPRITE_MAX_FRAMES = 4, /* idle animation frames per direction */
    SPRITE_SHIMMER_TICS = 18, /* items.md: 2-frame 18-tic shimmer */
    SPRITE_GIB_FRAMES = 8 /* tools/gibs.py frames per gib animation */
};

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
 * one disappears), never from the static map entity list. */
typedef struct {
    const char *name; /* may hold a subdirectory: "items/fish" */
    int frames;       /* 1..SPRITE_MAX_FRAMES */
    int live;
    int tics;         /* tics per idle frame, 0 = SPRITE_SHIMMER_TICS */
    const char *gib;  /* gib animation gibs/<gib>_gib_d0_f<0..7>.tex, or NULL */
} sprite_def;

/* Sprite of map entity `type`, or NULL if that type has no sprite yet.
 * Every pickup has one (items.md), except the Ankh Pistol (32): the player
 * starts with it and it has no world pickup art. */
const sprite_def *sprite_def_for_entity(int type);

/* Base name of sprite_def_for_entity(type), or NULL. */
const char *sprite_name_for_entity(int type);

/* Idle frame shown at render tic `tic` by a sprite with `frames` frames:
 * SPRITE_SHIMMER_TICS tics per frame, looping. */
int sprite_anim_frame(uint32_t tic, int frames);

/* Idle frame of `def` at render tic `tic`: def->tics (or
 * SPRITE_SHIMMER_TICS) tics per frame, looping. */
int sprite_def_frame(const sprite_def *def, uint32_t tic);

#endif
