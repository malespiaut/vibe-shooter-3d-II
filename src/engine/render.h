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
 * Floors and ceilings are flat fills until their issue lands. */

#include <stdint.h>

#include "bast.h"
#include "engine/angle.h"
#include "engine/fixed.h"

enum {
    VIEW_W = 320,
    VIEW_H = 208,
    VIEW_PROJ = 160, /* projection plane distance in px: 90 degree FOV */
    VIEW_DIST = 16,  /* units: distance part reaches row 31 here */
    RENDER_MAX_TEXTURES = BAST_MAP_MAX_TEXTURES
};

/* Flat fills for the not-yet-textured floor and ceiling (gray ramp). */
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

/* Loads the TEX of every wall texture `map` uses, from
 * `assets_dir`/textures/<name>.tex, else `assets_dir`/test/<name>.tex.
 * Needs colormap_load() first for drawing. Replaces any earlier set.
 * Returns 1, or 0 with render_error() set (a missing texture is an error). */
int render_init(const bast_map *map, const char *assets_dir);
void render_shutdown(void);
const char *render_error(void);

/* Draws the view from (x, y) facing `angle` (eye height 0.5 units). */
void render_view(const bast_map *map, fix_t x, fix_t y, angle_t angle, uint8_t *fb);

/* Column x (0..VIEW_W-1) of the last render_view(). */
const render_column *render_get_column(int x);
/* VIEW_W perpendicular distances of the last render_view() (sprites). */
const float *render_depth(void);

/* 1 if the cell blocks rays: a wall texture and SOLID, PUSHWALL or a closed
 * DOOR. */
int render_cell_is_wall(const bast_map_cell *c);

#endif
