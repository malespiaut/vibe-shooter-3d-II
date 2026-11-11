#include "engine/render.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/colormap.h"
#include "engine/files.h"
#include "engine/video.h"

/* Floats here are projection maths only (tech.md Simulation: Gameplay
 * floats). Nothing in this file writes game state. */

#define TEX_SIZE 64
#define MAX_RAY_STEPS (2 * BAST_MAP_MAX_DIM + 2)

static bast_tex *textures[RENDER_MAX_TEXTURES];
static bast_tex *sky;
static render_column columns[VIEW_W];
static float depth[VIEW_W];
static char err_buf[2048];

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
}

int render_cell_is_wall(const bast_map_cell *c)
{
    if (c->wall == 0)
        return 0;
    if (c->flags & (BAST_CELL_SOLID | BAST_CELL_PUSHWALL))
        return 1;
    /* No door motion exists yet: a door is drawn in its initial state. */
    return (c->flags & BAST_CELL_DOOR) && !(c->flags & BAST_CELL_START_OPEN);
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
        if (!render_cell_is_wall(c)) {
            front = c;
            continue;
        }
        /* Exact perpendicular distance to the face that was crossed. */
        double dist = side == 0 ? (mx - px + (1 - stepx) / 2) / rdx
                                : (my - py + (1 - stepy) / 2) / rdy;
        if (dist < 1e-4)
            dist = 1e-4;
        /* Texture u, oriented so every face reads left to right as seen
         * from the open side (screen right = view right). */
        double u;
        if (side == 0) {
            double wy = py + dist * rdy;
            u = wy - floor(wy);
            if (stepx > 0) /* looking east: screen right is south */
                u = 1.0 - u;
        } else {
            double wx = px + dist * rdx;
            u = wx - floor(wx);
            if (stepy < 0) /* looking south: screen right is west */
                u = 1.0 - u;
        }
        int tx = (int)(u * TEX_SIZE);
        if (tx < 0)
            tx = 0;
        if (tx > TEX_SIZE - 1)
            tx = TEX_SIZE - 1;
        int dark = front ? front->dark : 0;
        int row = (int)(32.0 * dist / VIEW_DIST) + dark;
        col->hit = 1;
        col->dist = (float)dist;
        col->tex = c->wall;
        col->tex_x = tx;
        col->light_row = row < 0 ? 0 : row > COLORMAP_ROWS - 1 ? COLORMAP_ROWS - 1 : row;
        col->map_x = mx;
        col->map_y = my;
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
 * (eye 0.5 units above the floor and below the ceiling), the world point
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
                        double rx, double ry, angle_t angle, uint8_t *fb)
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
        const double dist = 0.5 * VIEW_PROJ / rows;
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
            int light = drow + c->dark;
            if (light > COLORMAP_ROWS - 1)
                light = COLORMAP_ROWS - 1;
            if (t->flags & BAST_TEX_FULLBRIGHT)
                light = 0;
            row[sx] = colormap_row(light)[t->pixels[u * TEX_SIZE + v]];
        }
    }
}

void render_view(const bast_map *map, fix_t x, fix_t y, angle_t angle, uint8_t *fb)
{
    const double px = x / 65536.0, py = y / 65536.0;
    const double dx = fine_cos(angle) / 65536.0, dy = fine_sin(angle) / 65536.0;
    /* Camera plane: view right, length 1 (tan 45 deg) = VIEW_W/2 px at
     * VIEW_PROJ px, which is the 90 degree FOV. */
    const double rx = dy, ry = -dx;
    const double half = VIEW_H / 2.0;

    for (int sx = 0; sx < VIEW_W; sx++) {
        const double cam = (double)(sx - VIEW_W / 2) / (VIEW_W / 2);
        render_column *col = &columns[sx];
        cast(map, px, py, dx + rx * cam, dy + ry * cam, col);

        int ys = VIEW_H / 2, ye = VIEW_H / 2;
        double h = 0, top = half;
        if (col->hit) {
            h = VIEW_PROJ / (double)col->dist;
            top = half - h / 2;
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
    draw_planes(map, px, py, dx, dy, rx, ry, angle, fb);
    memset(fb + VIEW_H * SCREEN_W, 0, (size_t)(SCREEN_H - VIEW_H) * SCREEN_W);
}
