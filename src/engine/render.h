#ifndef ENGINE_RENDER_H
#define ENGINE_RENDER_H

/* Software 3D view (GAME.md 2.3 and 3, docs/design/tech.md World: Camera,
 * Lighting). Draws into the SCREEN_W x SCREEN_H index framebuffer: the view
 * is rows 0..VIEW_H-1, the status-bar area below is left at index 0 until
 * the HUD exists. Rendering only reads game data, never writes it.
 *
 * Walls: one DDA ray per screen column, perpendicular distance (no fisheye),
 * textured from TEX files, shaded once per column with the colormap row
 *   row = clamp(32 * dist / 16 + cell_dark, 0, 31).
 * Floors and ceilings: horizontal scanlines over the pixels walls left
 * open, distance shading once per row plus the sampled cell's dark per
 * pixel. Ceiling texture 0 is sky (map sky_tex, 320x104, unshaded).
 *
 * Sprites: billboards drawn after walls, floors and ceilings, sorted far to
 * near, projected
 * at 64 px per unit (VIEW_PROJ / dist px per unit) with the TEX origin at
 * the bottom-centre anchor on the floor, clipped per column against the
 * wall depth buffer, index 255 skipped, shaded with one colormap row per
 * sprite: clamp(32 * dist / 16 + cell_dark, 0, 31), row 0 if the TEX is
 * fullbright. dist is the perpendicular (view-axis) distance, like walls. */

#include <stdint.h>

#include "bast.h"
#include "engine/angle.h"
#include "engine/fixed.h"

enum {
    VIEW_W = 320,
    VIEW_H = 208,
    VIEW_PROJ = 160, /* projection plane distance in px: 90 degree FOV */
    VIEW_DIST = 16,  /* units: distance part reaches row 31 here */
    RENDER_MAX_TEXTURES = BAST_MAP_MAX_TEXTURES,
    RENDER_MAX_SPRITES = 256 /* drawn per frame; the rest are dropped */
};

/* Sky texture (art-bible.md "Sky"): 320x104, 90 degrees per 320 px. */
enum {
    RENDER_SKY_W = 320,
    RENDER_SKY_H = 104,
    RENDER_SKY_REPEAT = 4 /* times per full turn */
};

/* Fills for floor/ceiling pixels with no texture (index 0 without a sky,
 * or outside the map). */
enum {
    RENDER_CEIL_FILL = 5,  /* gray_03 */
    RENDER_FLOOR_FILL = 8  /* gray_06 */
};

/* What the last render_view() drew in screen column x. */
typedef struct {
    int hit;        /* 1 if the ray hit a wall */
    float dist;     /* perpendicular distance in units (depth for sprites) */
    int height;     /* wall height in px, before clipping: VIEW_PROJ / dist */
    int top, bottom; /* drawn wall rows [top, bottom), clipped to the view */
    int tex;        /* map texture index of the wall, 0 if none */
    int tex_x;      /* texture column 0..63 */
    int light_row;  /* colormap row used */
    int map_x, map_y; /* wall cell */
} render_column;

/* One billboard to draw. */
typedef struct {
    fix_t x, y;          /* world position of the anchor (TEX origin) */
    fix_t height;        /* anchor height above the floor in units, 0 = floor */
    const bast_tex *tex; /* frame to draw, origin = anchor */
    int dark;            /* cell_dark of the sprite's cell, 0..31 */
} render_sprite;

/* A live thing to draw as a world sprite (game state, not the map): its
 * entity type picks the sprite (sprite_def_for_entity()). */
typedef struct {
    uint16_t type;
    fix_t x, y;    /* floor position: the TEX origin (bottom centre) sits here */
    angle_t angle; /* facing, for 8-direction sprites */
    uint8_t anim;  /* enemy sprites: SPR_* animation (engine/sprite.h) */
    uint8_t frame; /* enemy sprites: frame within `anim`; items ignore both */
} render_thing;

/* Loads the TEX of every wall texture `map` uses, from
 * `assets_dir`/textures/<name>.tex, else `assets_dir`/test/<name>.tex.
 * Needs colormap_load() first for drawing. Replaces any earlier set.
 * Returns 1, or 0 with render_error() set (a missing texture is an error). */
int render_init(const bast_map *map, const char *assets_dir);
/* render_init() also loads the sprite frames of every map entity type that
 * has a sprite (sprite_def_for_entity()): <name>_idle_d<0..7>_f<0..frames-1>
 * .tex from `assets_dir`/sprites, else `assets_dir`/test (or /gibs). d0 of
 * every frame is required; a missing d1..d7 falls back to d0 (things without
 * facing). Enemy sprites load every animation (<name>_<anim>_d<d>_f<f>,
 * sprite_enemy_frames()); an enemy set missing any d0 frame is skipped with
 * one warning per name on stderr and not drawn, which is not an error. */
void render_shutdown(void);
const char *render_error(void);

/* Draws the view from (x, y) facing `angle` (eye height 0.5 units): walls,
 * then the sprites of the map's static (non-live) entities (sprite_dir()). */
void render_view(const bast_map *map, fix_t x, fix_t y, angle_t angle, uint8_t *fb);
/* render_view() plus `n_things` live things (pickups from game state,
 * game/items.h items_render_things()), sorted with the static map sprites.
 * Map entities whose sprite is `live` are drawn only from this list, so a
 * taken pickup disappears. `tic` picks the idle animation frame
 * (sprite_anim_frame()); it is read, never written. Types without a loaded
 * sprite are skipped. render_view() is this with no things and tic 0. */
void render_view_things(const bast_map *map, fix_t x, fix_t y, angle_t angle,
                        const render_thing *things, int n_things, uint32_t tic, uint8_t *fb);
/* render_view_things() with the eye `eye_height` units (16.16) above the
 * floor, clamped to [1/64, 63/64]: walls, floor, ceiling and every sprite
 * (map and live things) move with it (the death view drops it to 0.125).
 * render_view_things() is eye 0.5. */
void render_view_things_eye(const bast_map *map, fix_t x, fix_t y, angle_t angle,
                            fix_t eye_height, const render_thing *things, int n_things,
                            uint32_t tic, uint8_t *fb);

/* Sky texture column drawn in screen column sx (0..VIEW_W-1) when facing
 * `angle`: (sx - angle * 1280 / 65536) mod 320. */
int render_sky_column(angle_t angle, int sx);
/* Draws `n` sprites (any order: they are sorted far to near here) with the
 * camera and wall depth buffer of the last render_view(). Returns how many
 * were in front of the camera and drawn (fully clipped ones included). */
int render_draw_sprites(const render_sprite *s, int n, uint8_t *fb);

/* Column x (0..VIEW_W-1) of the last render_view(). */
const render_column *render_get_column(int x);
/* VIEW_W perpendicular distances of the last render_view() (sprites). */
const float *render_depth(void);

/* Live door state for the wall pass (doors are game state, game/door.h; the
 * engine does not read game_state). `open` has one entry per map cell,
 * index y * map width + x, read only for DOOR cells: how far the door is
 * open, 0 closed .. FIX_ONE fully open (door_t.pos). The table is borrowed
 * and read by every later render_view() until replaced; NULL (the default)
 * draws each door in its initial map state (START_OPEN open, else closed).
 *
 * A door at open amount p is drawn on the face of its cell the ray enters,
 * with the panel slid by p along that face toward +x (faces running east-
 * west) or +y (faces running north-south): the panel covers [p, 1) of the
 * face, its texture shifted with it, the uncovered part is a gap the ray
 * passes through. Fully open draws nothing. */
void render_set_doors(const fix_t *open);

/* One live pushwall block (game/tags.h pushwall_t): a 1x1 unit wall whose
 * south-west (minimum) corner is at (x, y) in world units, textured with
 * map texture `tex` on all four faces. */
typedef struct {
    fix_t x, y;
    int tex;
} render_pushwall;

/* Live pushwall state for the wall pass. With a list (n >= 0, the array
 * borrowed until replaced), the map's PUSHWALL cells are open floor and
 * the wall pass draws the `n` blocks where they are now, sliding blocks
 * between two tiles included: each face is hit exactly, lit by the open
 * cell the face looks into. NULL (the default) draws every PUSHWALL cell
 * as a wall in its map position. */
void render_set_pushwalls(const render_pushwall *pw, int n);

/* Live darkness for walls, floors, ceilings and map sprites: one entry
 * per map cell (index y * map width + x), 0..31, borrowed until replaced.
 * NULL (the default) uses each cell's map `dark`. */
void render_set_dark(const uint8_t *dark);

/* 1 if the cell blocks rays: a wall texture and SOLID, PUSHWALL or a closed
 * DOOR in its initial map state (START_OPEN doors are open). Doors with
 * live state go through render_set_doors(). */
int render_cell_is_wall(const bast_map_cell *c);

#endif
