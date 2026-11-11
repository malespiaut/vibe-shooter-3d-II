#include "engine/render.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/colormap.h"
#include "engine/files.h"
#include "engine/sprite.h"
#include "engine/video.h"

/* Floats here are projection maths only (tech.md Simulation: Gameplay
 * floats). Nothing in this file writes game state. */

#define TEX_SIZE 64
#define MAX_RAY_STEPS (2 * BAST_MAP_MAX_DIM + 2)

static bast_tex *textures[RENDER_MAX_TEXTURES];
static bast_tex *sky;
/* Sprite frames per entity type, direction and idle frame; d1..d7 may
 * alias d0 of the same frame. */
static bast_tex *sprite_frames[BAST_ENT_TYPE_LIMIT][SPRITE_MAX_FRAMES][SPRITE_DIRS];
static render_column columns[VIEW_W];
static float depth[VIEW_W];
/* Camera of the last render_view(), for render_draw_sprites(). */
static double cam_x, cam_y, cam_dx, cam_dy;
static double cam_eye = 0.5; /* eye height above the floor, units */
static char err_buf[2048];
/* render_set_doors(): live door open amounts per cell, or NULL. */
static const fix_t *door_open;
/* render_set_pushwalls(): live pushwall blocks, or NULL (map state). */
static const render_pushwall *pw_list;
static int pw_count;
/* Cells a live pushwall block overlaps, rebuilt per render_view(). */
static uint8_t pw_touch[BAST_MAP_MAX_DIM * BAST_MAP_MAX_DIM];
/* render_set_dark(): live darkness per cell, or NULL (map `dark`). */
static const uint8_t *dark_live;

const char *render_error(void)
{
    return err_buf;
}

void render_shutdown(void)
{
    for (int i = 0; i < RENDER_MAX_TEXTURES; i++) {
        bast_tex_free(textures[i]);
        textures[i] = NULL;
    }
    bast_tex_free(sky);
    sky = NULL;
    for (int t = 0; t < BAST_ENT_TYPE_LIMIT; t++) {
        for (int f = 0; f < SPRITE_MAX_FRAMES; f++) {
            bast_tex **fr = sprite_frames[t][f];
            for (int d = SPRITE_DIRS - 1; d >= 0; d--) {
                if (d == 0 || fr[d] != fr[0])
                    bast_tex_free(fr[d]);
                fr[d] = NULL;
            }
        }
    }
}

/* Sprite frame `path` from files.c ("sprites/..." or "test/..."): the TEX, or
 * NULL with *found = 0 if absent, or NULL with *found = 1 and err_buf set if
 * it is broken. */
static bast_tex *load_sprite_frame(const char *dir, const char *name, int *found)
{
    static const char *const sub[] = { "sprites", "test" };
    char path[256];
    *found = 0;
    for (size_t i = 0; i < sizeof sub / sizeof sub[0]; i++) {
        snprintf(path, sizeof path, "%s/%s.tex", sub[i], name);
        int64_t n = 0;
        uint8_t *data = files_read(path, &n);
        if (!data)
            continue;
        *found = 1;
        bast_tex *t = bast_tex_from_memory(data, n);
        free(data);
        if (!t)
            snprintf(err_buf, sizeof err_buf, "%s/%s: %s", dir, path, bast_error());
        return t;
    }
    return NULL;
}

static int load_sprites(const bast_map *map, const char *dir)
{
    for (int i = 0; i < map->entity_count; i++) {
        const bast_map_entity *e = &map->entities[i];
        /* A RESPAWN_ITEM shows the pickup its param names (game/items.c). */
        const int type = e->type == BAST_ENT_RESPAWN_ITEM ? e->param : e->type;
        if (type >= BAST_ENT_TYPE_LIMIT)
            continue;
        const sprite_def *def = sprite_def_for_entity(type);
        if (!def || sprite_frames[type][0][0])
            continue;
        for (int f = 0; f < def->frames; f++) {
            bast_tex **fr = sprite_frames[type][f];
            for (int d = 0; d < SPRITE_DIRS; d++) {
                char name[128];
                int found;
                snprintf(name, sizeof name, "%s_idle_d%d_f%d", def->name, d, f);
                bast_tex *t = load_sprite_frame(dir, name, &found);
                if (!t && found)
                    return 0;
                if (!t && d == 0) {
                    snprintf(err_buf, sizeof err_buf,
                             "sprite '%s' not found in %s/sprites or %s/test", name, dir, dir);
                    return 0;
                }
                fr[d] = t ? t : fr[0];
            }
        }
    }
    return 1;
}

int render_cell_is_wall(const bast_map_cell *c)
{
    if (c->wall == 0)
        return 0;
    if (c->flags & (BAST_CELL_SOLID | BAST_CELL_PUSHWALL))
        return 1;
    return (c->flags & BAST_CELL_DOOR) && !(c->flags & BAST_CELL_START_OPEN);
}

void render_set_doors(const fix_t *open)
{
    door_open = open;
}

void render_set_pushwalls(const render_pushwall *pw, int n)
{
    pw_list = pw;
    pw_count = pw ? n : 0;
}

void render_set_dark(const uint8_t *dark)
{
    dark_live = dark;
}

/* Darkness of map cell `c` (a pointer into m->cells): live or map. */
static int cell_dark(const bast_map *m, const bast_map_cell *c)
{
    return dark_live ? dark_live[c - m->cells] : c->dark;
}

/* Marks the cells each live pushwall block overlaps (at most 2x2). */
static void touch_pushwalls(const bast_map *m)
{
    memset(pw_touch, 0, (size_t)m->width * m->height);
    for (int i = 0; i < pw_count; i++) {
        const int x0 = fix_to_int(pw_list[i].x), y0 = fix_to_int(pw_list[i].y);
        const int x1 = fix_to_int(pw_list[i].x + FIX_ONE - 1);
        const int y1 = fix_to_int(pw_list[i].y + FIX_ONE - 1);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++)
                if (x >= 0 && y >= 0 && x < m->width && y < m->height)
                    pw_touch[y * m->width + x] = 1;
    }
}

/* Nearest entry of the ray (px, py) + t (rdx, rdy) into a live pushwall
 * block, with t in [t0, t1] (the span the ray spends in the current cell).
 * Returns the pushwall index or -1; *t_hit, *side (0: an x face, 1: a y
 * face) and *f (0..1 along the face toward +y or +x) describe the hit. */
static int hit_pushwall(double px, double py, double rdx, double rdy, double t0, double t1,
                        double *t_hit, int *side, double *f)
{
    int best = -1;
    for (int i = 0; i < pw_count; i++) {
        const double bx = pw_list[i].x / 65536.0, by = pw_list[i].y / 65536.0;
        double xa = -1e30, xb = 1e30, ya = -1e30, yb = 1e30;
        if (rdx != 0) {
            const double a = (bx - px) / rdx, b = (bx + 1.0 - px) / rdx;
            xa = a < b ? a : b;
            xb = a < b ? b : a;
        } else if (px < bx || px > bx + 1.0) {
            continue;
        }
        if (rdy != 0) {
            const double a = (by - py) / rdy, b = (by + 1.0 - py) / rdy;
            ya = a < b ? a : b;
            yb = a < b ? b : a;
        } else if (py < by || py > by + 1.0) {
            continue;
        }
        const int s = xa > ya ? 0 : 1;
        const double te = s == 0 ? xa : ya, tx = xb < yb ? xb : yb;
        if (te > tx || te <= 0 || te < t0 - 1e-9 || te > t1 + 1e-9)
            continue;
        if (best >= 0 && te >= *t_hit)
            continue;
        best = i;
        *t_hit = te;
        *side = s;
        double ff = s == 0 ? py + te * rdy - by : px + te * rdx - bx;
        *f = ff < 0 ? 0 : ff > 1 ? 1 : ff;
    }
    return best;
}

/* Open amount of door cell (x, y) as a fraction 0..1. */
static double door_amount(const bast_map *m, const bast_map_cell *c, int x, int y)
{
    if (!door_open)
        return (c->flags & BAST_CELL_START_OPEN) ? 1.0 : 0.0;
    fix_t p = door_open[y * m->width + x];
    if (p <= 0)
        return 0.0;
    if (p >= FIX_ONE)
        return 1.0;
    return p / 65536.0;
}

static bast_tex *load_one(const char *dir, const char *name, int w, int h)
{
    /* Through files.c (tech.md "Archives"): game.pak first, loose fallback. */
    static const char *const sub[] = { "textures", "test" };
    char path[256];
    for (size_t i = 0; i < sizeof sub / sizeof sub[0]; i++) {
        snprintf(path, sizeof path, "%s/%s.tex", sub[i], name);
        int64_t n = 0;
        uint8_t *data = files_read(path, &n);
        if (!data)
            continue;
        bast_tex *t = bast_tex_from_memory(data, n);
        free(data);
        if (!t)
            snprintf(err_buf, sizeof err_buf, "%s/%s: %s", dir, path, bast_error());
        else if (t->width != w || t->height != h) {
            snprintf(err_buf, sizeof err_buf, "%s/%s: expected %dx%d, got %dx%d", dir, path, w,
                     h, t->width, t->height);
            bast_tex_free(t);
            t = NULL;
        }
        return t;
    }
    snprintf(err_buf, sizeof err_buf, "texture '%s' not found in %s/textures or %s/test",
             name, dir, dir);
    return NULL;
}

int render_init(const bast_map *map, const char *assets_dir)
{
    render_shutdown();
    if (!files_init(assets_dir)) {
        snprintf(err_buf, sizeof err_buf, "%s", files_error());
        return 0;
    }
    /* Walls of every cell that can block a ray (doors and pushwalls move
     * later), floors and ceilings of every cell, the sky if any ceiling is
     * open. Texture 0 is none. */
    uint8_t used[RENDER_MAX_TEXTURES] = { 0 };
    int any_sky = 0;
    for (int i = 0; i < map->width * map->height; i++) {
        const bast_map_cell *c = &map->cells[i];
        if (c->flags & (BAST_CELL_SOLID | BAST_CELL_PUSHWALL | BAST_CELL_DOOR))
            used[c->wall] = 1;
        used[c->floor] = 1;
        used[c->ceil] = 1;
        any_sky |= c->ceil == 0;
    }
    used[0] = 0;
    for (int t = 1; t < map->texture_count; t++) {
        if (!used[t])
            continue;
        textures[t] = load_one(assets_dir, map->textures[t], TEX_SIZE, TEX_SIZE);
        if (!textures[t]) {
            render_shutdown();
            return 0;
        }
    }
    if (any_sky && map->sky_tex != 0) {
        sky = load_one(assets_dir, map->textures[map->sky_tex], RENDER_SKY_W, RENDER_SKY_H);
        if (!sky) {
            render_shutdown();
            return 0;
        }
    }
    if (!load_sprites(map, assets_dir)) {
        render_shutdown();
        return 0;
    }
    err_buf[0] = '\0';
    return 1;
}

const render_column *render_get_column(int x)
{
    return &columns[x < 0 ? 0 : x >= VIEW_W ? VIEW_W - 1 : x];
}

const float *render_depth(void)
{
    return depth;
}

static const bast_map_cell *cell(const bast_map *m, int x, int y)
{
    if (x < 0 || y < 0 || x >= m->width || y >= m->height)
        return NULL;
    return &m->cells[y * m->width + x];
}

/* Fills `col` for a wall face hit at perpendicular distance `dist`, `f`
 * along the face (0..1 toward +y for side 0, +x for side 1), lit by the
 * open cell `front` the face looks into. */
static void shade_hit(const bast_map *m, const bast_map_cell *front, double dist, int side,
                      int stepx, int stepy, double f, int tex, int mx, int my,
                      render_column *col)
{
    /* Texture u, oriented so every face reads left to right as seen
     * from the open side (screen right = view right). */
    double u = f;
    if (side == 0 ? stepx > 0  /* looking east: screen right is south */
                  : stepy < 0) /* looking south: screen right is west */
        u = 1.0 - u;
    int tx = (int)(u * TEX_SIZE);
    if (tx < 0)
        tx = 0;
    if (tx > TEX_SIZE - 1)
        tx = TEX_SIZE - 1;
    int dark = front ? cell_dark(m, front) : 0;
    int row = (int)(32.0 * dist / VIEW_DIST) + dark;
    col->hit = 1;
    col->dist = (float)dist;
    col->tex = tex;
    col->tex_x = tx;
    col->light_row = row < 0 ? 0 : row > COLORMAP_ROWS - 1 ? COLORMAP_ROWS - 1 : row;
    col->map_x = mx;
    col->map_y = my;
}

/* One DDA ray (Amanatides-Woo). The perpendicular
 * distance is measured along the view direction, so a flat wall projects
 * flat (no fisheye). */
static void cast(const bast_map *m, double px, double py, double rdx, double rdy,
                 render_column *col)
{
    memset(col, 0, sizeof *col);
    int mx = (int)floor(px), my = (int)floor(py);
    const double ddx = rdx == 0 ? 1e30 : fabs(1.0 / rdx);
    const double ddy = rdy == 0 ? 1e30 : fabs(1.0 / rdy);
    int stepx, stepy;
    double sdx, sdy;
    if (rdx < 0) {
        stepx = -1;
        sdx = (px - mx) * ddx;
    } else {
        stepx = 1;
        sdx = (mx + 1.0 - px) * ddx;
    }
    if (rdy < 0) {
        stepy = -1;
        sdy = (py - my) * ddy;
    } else {
        stepy = 1;
        sdy = (my + 1.0 - py) * ddy;
    }
    /* Light comes from the open cell the face looks into: solid cells sit
     * between rooms of different darkness and carry none of their own. */
    const bast_map_cell *front = cell(m, mx, my);
    for (int i = 0; i < MAX_RAY_STEPS; i++) {
        int side;
        if (sdx < sdy) {
            sdx += ddx;
            mx += stepx;
            side = 0;
        } else {
            sdy += ddy;
            my += stepy;
            side = 1;
        }
        const bast_map_cell *c = cell(m, mx, my);
        if (!c)
            return; /* left the map: nothing to draw */
        if (pw_list && pw_touch[my * m->width + mx]) {
            /* The ray spends t in [t0, t1] in this cell. */
            const double t0 = side == 0 ? sdx - ddx : sdy - ddy;
            const double t1 = sdx < sdy ? sdx : sdy;
            double th = 0, f = 0;
            int ps = 0;
            const int pi = hit_pushwall(px, py, rdx, rdy, t0, t1, &th, &ps, &f);
            if (pi >= 0) {
                if (th > t0 + 1e-6 && !(c->flags & BAST_CELL_SOLID))
                    front = c; /* the hit is inside this (open) cell */
                if (th < 1e-4)
                    th = 1e-4;
                shade_hit(m, front, th, ps, stepx, stepy, f, pw_list[pi].tex, mx, my, col);
                return;
            }
        }
        /* Live pushwalls replace the map's PUSHWALL cells: the tile is
         * open floor unless a block (tested above) is in it. */
        if (pw_list && (c->flags & BAST_CELL_PUSHWALL) && !(c->flags & BAST_CELL_SOLID)) {
            front = c;
            continue;
        }
        const int is_door = c->wall != 0 && (c->flags & BAST_CELL_DOOR) &&
                            !(c->flags & (BAST_CELL_SOLID | BAST_CELL_PUSHWALL));
        if (!is_door && !render_cell_is_wall(c)) {
            front = c;
            continue;
        }
        /* Exact perpendicular distance to the face that was crossed. */
        double dist = side == 0 ? (mx - px + (1 - stepx) / 2) / rdx
                                : (my - py + (1 - stepy) / 2) / rdy;
        if (dist < 1e-4)
            dist = 1e-4;
        /* f: position along the face, 0..1 toward +y (side 0) or +x. */
        double f = side == 0 ? py + dist * rdy : px + dist * rdx;
        f -= floor(f);
        if (is_door) {
            /* The panel covers [p, 1) of the face; the rest is a gap. */
            const double p = door_amount(m, c, mx, my);
            if (f < p || p >= 1.0) {
                front = c;
                continue;
            }
            f -= p;
        }
        shade_hit(m, front, dist, side, stepx, stepy, f, c->wall, mx, my, col);
        return;
    }
}

int render_sky_column(angle_t angle, int sx)
{
    /* RENDER_SKY_REPEAT * RENDER_SKY_W columns per 65536 angle units;
     * turning left (angle up) moves the sky right on screen. */
    const int a = (int)(((uint32_t)angle * (RENDER_SKY_REPEAT * RENDER_SKY_W)) >> 16);
    return ((sx - a) % RENDER_SKY_W + RENDER_SKY_W) % RENDER_SKY_W;
}

/* Floors, ceilings and sky (GAME.md 3, Floors and Ceilings; tech.md
 * Lighting): one horizontal scanline per view row, drawing only the pixels
 * the wall pass left open. Per row: the distance of the plane at that row
 * (eye `eye` units above the floor, 1 - eye below the ceiling), the world point
 * under column 0 and the per-column step, all in 32.32 fixed point; per
 * pixel: two adds, a cell lookup, one texel, one add and clamp for the
 * cell's darkness. The column rays are the wall pass's rays, so floor
 * edges meet the wall bases.
 *
 * Texel (u, v) of world point (wx, wy): u = frac(wx) * 64 (east is +u),
 * v = 63 - frac(wy) * 64 (north is up on the texture when seen from above).
 * Sky: texture column render_sky_column(), row = screen row (the 104 px sky
 * fills the 104 rows above the horizon exactly), never shaded. */
static void draw_planes(const bast_map *map, double px, double py, double dx, double dy,
                        double rx, double ry, angle_t angle, double eye, uint8_t *fb)
{
    const int horizon = VIEW_H / 2;
    int skycol[VIEW_W];
    for (int sx = 0; sx < VIEW_W; sx++)
        skycol[sx] = render_sky_column(angle, sx);
    const double one = 4294967296.0; /* 2^32 */

    for (int y = 0; y < VIEW_H; y++) {
        const int floor_row = y >= horizon;
        /* Rows from the horizon to this pixel row's centre. */
        const double rows = floor_row ? y + 0.5 - horizon : horizon - (y + 0.5);
        const double dist = (floor_row ? eye : 1.0 - eye) * VIEW_PROJ / rows;
        int drow = (int)(32.0 * dist / VIEW_DIST);
        if (drow > COLORMAP_ROWS - 1)
            drow = COLORMAP_ROWS - 1;
        /* Column sx sees direction (d + r * cam), cam = (sx - 160) / 160. */
        int64_t wx = (int64_t)((px + dist * (dx - rx)) * one);
        int64_t wy = (int64_t)((py + dist * (dy - ry)) * one);
        const int64_t stx = (int64_t)(dist * rx / (VIEW_W / 2) * one);
        const int64_t sty = (int64_t)(dist * ry / (VIEW_W / 2) * one);
        uint8_t *row = fb + y * SCREEN_W;

        for (int sx = 0; sx < VIEW_W; sx++, wx += stx, wy += sty) {
            const render_column *col = &columns[sx];
            if (floor_row ? y < col->bottom : y >= col->top)
                continue; /* wall pixel */
            const int64_t cx = wx >> 32, cy = wy >> 32;
            const bast_map_cell *c =
                (cx < 0 || cy < 0 || cx >= map->width || cy >= map->height)
                    ? NULL
                    : &map->cells[cy * map->width + cx];
            const int ti = c ? (floor_row ? c->floor : c->ceil) : 0;
            if (!floor_row && c && ti == 0 && sky) {
                row[sx] = sky->pixels[skycol[sx] * RENDER_SKY_H + y];
                continue;
            }
            const bast_tex *t = textures[ti];
            if (!c || !t) {
                row[sx] = floor_row ? RENDER_FLOOR_FILL : RENDER_CEIL_FILL;
                continue;
            }
            const int u = (int)((wx >> 26) & (TEX_SIZE - 1));
            const int v = TEX_SIZE - 1 - (int)((wy >> 26) & (TEX_SIZE - 1));
            int light = drow + cell_dark(map, c);
            if (light > COLORMAP_ROWS - 1)
                light = COLORMAP_ROWS - 1;
            if (t->flags & BAST_TEX_FULLBRIGHT)
                light = 0;
            row[sx] = colormap_row(light)[t->pixels[u * TEX_SIZE + v]];
        }
    }
}

typedef struct {
    double z, lat; /* perpendicular distance, lateral offset (view right) */
    int index;     /* position in the caller's list: tie-break */
} sprite_key;

static int cmp_far_first(const void *a, const void *b)
{
    const sprite_key *p = a, *q = b;
    if (p->z != q->z)
        return p->z > q->z ? -1 : 1;
    return p->index - q->index;
}

/* Nearer than this a sprite is skipped (its projection would explode). */
#define SPRITE_NEAR 0.0625

static void draw_one(const render_sprite *s, double z, double lat, uint8_t *fb)
{
    const bast_tex *t = s->tex;
    /* px per texel: VIEW_PROJ px per unit at dist 1, 64 texels per unit. */
    const double scale = VIEW_PROJ / (z * TEX_SIZE);
    const double cx = VIEW_W / 2.0 + lat * VIEW_PROJ / z;
    /* The floor under the anchor is cam_eye units below the eye. */
    const double anchor_y = VIEW_H / 2.0 + (cam_eye - s->height / 65536.0) * VIEW_PROJ / z;
    const double left = cx - t->origin_x * scale;
    const double top = anchor_y - t->origin_y * scale;
    const double w = t->width * scale, h = t->height * scale;
    /* Pixel (x, y) is covered when its centre is in [left, left + w) etc. */
    double fx0 = ceil(left - 0.5), fx1 = ceil(left + w - 0.5);
    double fy0 = ceil(top - 0.5), fy1 = ceil(top + h - 0.5);
    int x0 = fx0 < 0 ? 0 : fx0 > VIEW_W ? VIEW_W : (int)fx0;
    int x1 = fx1 < 0 ? 0 : fx1 > VIEW_W ? VIEW_W : (int)fx1;
    int y0 = fy0 < 0 ? 0 : fy0 > VIEW_H ? VIEW_H : (int)fy0;
    int y1 = fy1 < 0 ? 0 : fy1 > VIEW_H ? VIEW_H : (int)fy1;
    if (x0 >= x1 || y0 >= y1)
        return;

    int row = (int)(32.0 * z / VIEW_DIST) + s->dark;
    row = row < 0 ? 0 : row > COLORMAP_ROWS - 1 ? COLORMAP_ROWS - 1 : row;
    const uint8_t *cm = colormap_row((t->flags & BAST_TEX_FULLBRIGHT) ? 0 : row);

    /* Texel steps in 16.16, like the wall columns. */
    const double inv = 1.0 / scale;
    const int64_t step = (int64_t)(inv * 65536.0);
    const int64_t v0 = (int64_t)((y0 + 0.5 - top) * inv * 65536.0);
    for (int sx = x0; sx < x1; sx++) {
        if (depth[sx] <= z)
            continue; /* a wall is in front in this column */
        int u = (int)((sx + 0.5 - left) * inv);
        if (u < 0)
            u = 0;
        if (u > t->width - 1)
            u = t->width - 1;
        const uint8_t *src = t->pixels + (size_t)u * t->height;
        uint8_t *p = fb + sx;
        int64_t v = v0;
        for (int yy = y0; yy < y1; yy++, v += step) {
            int ty = (int)(v >> 16);
            if (ty > t->height - 1)
                ty = t->height - 1;
            if (ty < 0)
                ty = 0;
            const uint8_t c = src[ty];
            if (c != BAST_INDEX_TRANSPARENT)
                p[yy * SCREEN_W] = cm[c];
        }
    }
}

int render_draw_sprites(const render_sprite *s, int n, uint8_t *fb)
{
    static sprite_key keys[RENDER_MAX_SPRITES];
    int k = 0;
    for (int i = 0; i < n && k < RENDER_MAX_SPRITES; i++) {
        if (!s[i].tex)
            continue;
        const double rx = s[i].x / 65536.0 - cam_x, ry = s[i].y / 65536.0 - cam_y;
        const double z = rx * cam_dx + ry * cam_dy;
        if (z < SPRITE_NEAR)
            continue;
        /* View right is (dy, -dx). */
        keys[k].z = z;
        keys[k].lat = rx * cam_dy - ry * cam_dx;
        keys[k].index = i;
        k++;
    }
    qsort(keys, (size_t)k, sizeof keys[0], cmp_far_first);
    for (int i = 0; i < k; i++)
        draw_one(&s[keys[i].index], keys[i].z, keys[i].lat, fb);
    return k;
}

/* Frame of `type` seen from (vx, vy) for a thing at (x, y) facing `angle`,
 * at render tic `tic`; NULL if the type has no loaded sprite. */
static const bast_tex *thing_frame(int type, fix_t x, fix_t y, angle_t angle, fix_t vx,
                                   fix_t vy, uint32_t tic)
{
    const sprite_def *def = sprite_def_for_entity(type);
    if (!def || !sprite_frames[type][0][0])
        return NULL;
    /* Angle of the vector thing -> viewer picks the facing frame. */
    const int dir = sprite_dir(angle_of(vx - x, vy - y), angle);
    return sprite_frames[type][sprite_anim_frame(tic, def->frames)][dir];
}

/* Static sprites of the map's entities (non-live types, e.g. TEST_SPRITE)
 * plus the live things, sorted and drawn together. */
static void draw_world_sprites(const bast_map *map, fix_t vx, fix_t vy,
                               const render_thing *things, int n_things, uint32_t tic,
                               uint8_t *fb)
{
    static render_sprite list[RENDER_MAX_SPRITES];
    int n = 0;
    for (int i = 0; i < map->entity_count && n < RENDER_MAX_SPRITES; i++) {
        const bast_map_entity *e = &map->entities[i];
        const sprite_def *def = sprite_def_for_entity(e->type);
        if (!def || def->live)
            continue;
        const bast_tex *t = thing_frame(e->type, e->x, e->y, e->angle, vx, vy, tic);
        if (!t)
            continue;
        const bast_map_cell *c = cell(map, e->x >> 16, e->y >> 16);
        list[n] = (render_sprite){ e->x, e->y, 0, t, c ? cell_dark(map, c) : 0 };
        n++;
    }
    for (int i = 0; i < n_things && n < RENDER_MAX_SPRITES; i++) {
        const render_thing *th = &things[i];
        const bast_tex *t = thing_frame(th->type, th->x, th->y, th->angle, vx, vy, tic);
        if (!t)
            continue;
        const bast_map_cell *c = cell(map, th->x >> 16, th->y >> 16);
        list[n] = (render_sprite){ th->x, th->y, 0, t, c ? cell_dark(map, c) : 0 };
        n++;
    }
    render_draw_sprites(list, n, fb);
}

void render_view(const bast_map *map, fix_t x, fix_t y, angle_t angle, uint8_t *fb)
{
    render_view_things(map, x, y, angle, NULL, 0, 0, fb);
}

void render_view_things(const bast_map *map, fix_t x, fix_t y, angle_t angle,
                        const render_thing *things, int n_things, uint32_t tic, uint8_t *fb)
{
    render_view_things_eye(map, x, y, angle, FIX_ONE / 2, things, n_things, tic, fb);
}

void render_view_things_eye(const bast_map *map, fix_t x, fix_t y, angle_t angle,
                            fix_t eye_height, const render_thing *things, int n_things,
                            uint32_t tic, uint8_t *fb)
{
    /* Inside the 1-unit-high room: the planes need a gap on both sides. */
    const fix_t lo = FIX_ONE / 64, hi = FIX_ONE - FIX_ONE / 64;
    const double eye = (eye_height < lo ? lo : eye_height > hi ? hi : eye_height) / 65536.0;
    const double px = x / 65536.0, py = y / 65536.0;
    const double dx = fine_cos(angle) / 65536.0, dy = fine_sin(angle) / 65536.0;
    /* Camera plane: view right, length 1 (tan 45 deg) = VIEW_W/2 px at
     * VIEW_PROJ px, which is the 90 degree FOV. */
    const double rx = dy, ry = -dx;
    const double half = VIEW_H / 2.0;
    cam_x = px;
    cam_y = py;
    cam_dx = dx;
    cam_dy = dy;
    cam_eye = eye;
    if (pw_list)
        touch_pushwalls(map);

    for (int sx = 0; sx < VIEW_W; sx++) {
        const double cam = (double)(sx - VIEW_W / 2) / (VIEW_W / 2);
        render_column *col = &columns[sx];
        cast(map, px, py, dx + rx * cam, dy + ry * cam, col);

        int ys = VIEW_H / 2, ye = VIEW_H / 2;
        double h = 0, top = half;
        if (col->hit) {
            h = VIEW_PROJ / (double)col->dist;
            top = half - h * (1.0 - eye); /* the wall spans 1 unit, eye at `eye` */
            /* Pixel row y is wall when its centre y + 0.5 is in [top, top + h). */
            double fs = ceil(top - 0.5), fe = ceil(top + h - 0.5);
            if (fs < -1e6)
                fs = -1e6;
            if (fe > 1e6)
                fe = 1e6;
            ys = (int)fs;
            ye = (int)fe;
            col->height = ye - ys;
            depth[sx] = col->dist;
        } else {
            depth[sx] = 1e30f;
        }
        if (ys < 0)
            ys = 0;
        if (ye > VIEW_H)
            ye = VIEW_H;
        if (ye < ys)
            ye = ys;
        col->top = ys;
        col->bottom = ye;

        uint8_t *p = fb + sx;
        if (col->hit && ye > ys) {
            const bast_tex *t = textures[col->tex];
            const uint8_t *cm = colormap_row(
                (t && (t->flags & BAST_TEX_FULLBRIGHT)) ? 0 : col->light_row);
            if (t) {
                const uint8_t *src = t->pixels + col->tex_x * TEX_SIZE;
                /* v in 16.16 texels, stepped per row. */
                const double vstep = TEX_SIZE / h;
                int64_t v = (int64_t)((ys + 0.5 - top) * vstep * 65536.0);
                const int64_t step = (int64_t)(vstep * 65536.0);
                for (int yy = ys; yy < ye; yy++, v += step) {
                    int ty = (int)(v >> 16);
                    if (ty > TEX_SIZE - 1)
                        ty = TEX_SIZE - 1;
                    if (ty < 0)
                        ty = 0;
                    p[yy * SCREEN_W] = cm[src[ty]];
                }
            } else {
                for (int yy = ys; yy < ye; yy++)
                    p[yy * SCREEN_W] = cm[BAST_INDEX_WHITE];
            }
        }
    }
    draw_planes(map, px, py, dx, dy, rx, ry, angle, eye, fb);
    memset(fb + VIEW_H * SCREEN_W, 0, (size_t)(SCREEN_H - VIEW_H) * SCREEN_W);

    draw_world_sprites(map, x, y, things, n_things, tic, fb);
}
