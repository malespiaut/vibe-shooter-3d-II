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

/* Loads the TEX of every wall texture `map` uses, from
 * `assets_dir`/textures/<name>.tex, else `assets_dir`/test/<name>.tex.
 * Needs colormap_load() first for drawing. Replaces any earlier set.
 * Returns 1, or 0 with render_error() set (a missing texture is an error). */
int render_init(const bast_map *map, const char *assets_dir);
/* render_init() also loads the sprite frames of every map entity type that
 * has a sprite (sprite_name_for_entity()): <name>_idle_d<0..7>_f0.tex from
 * `assets_dir`/sprites, else `assets_dir`/test. d0 is required; a missing
 * d1..d7 falls back to d0 (things without facing). */
void render_shutdown(void);
const char *render_error(void);

/* Draws the view from (x, y) facing `angle` (eye height 0.5 units): walls,
 * then the sprites of the map's entities (frame chosen by sprite_dir()). */
void render_view(const bast_map *map, fix_t x, fix_t y, angle_t angle, uint8_t *fb);

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

/* 1 if the cell blocks rays: a wall texture and SOLID, PUSHWALL or a closed
 * DOOR. */
int render_cell_is_wall(const bast_map_cell *c);

#endif
