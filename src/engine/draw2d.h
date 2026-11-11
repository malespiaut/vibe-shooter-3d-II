#ifndef ENGINE_DRAW2D_H
#define ENGINE_DRAW2D_H

/* 2D drawing into the 320x240 indexed framebuffer (docs/design/hud.md):
 * TEX patches with transparency, the three fonts, solid fills and 256-byte
 * colour remaps. `fb` is always SCREEN_W x SCREEN_H indices, row-major
 * (video.h). Everything clips on all four sides; nothing is ever written
 * outside the framebuffer. Coordinates are screen pixels, (0,0) top-left. */

#include <stdint.h>

#include "bast.h"

/* ---- palette ramps (docs/design/art-bible.md, catlantis.gpl order) ---- */

/* First index and entry count of ramp `name` ("gray", "gold", "red", ...).
 * Returns 1 if found, 0 otherwise. */
int ramp_find(const char *name, int *first, int *count);
/* Number of ramps in the table and the i-th one (for tests / tools). */
int ramp_table_size(void);
const char *ramp_table_entry(int i, int *first, int *count);

/* remap[i] = i. */
void remap_identity(uint8_t remap[256]);
/* Identity, except every entry of ramp `from` maps to the entry of ramp `to`
 * at the same relative position (from_00 -> to_00, last -> last, linear in
 * between, rounded to nearest). Built once at load time; returns 1 on
 * success, 0 if a ramp name is unknown (remap is then identity). */
int remap_ramp(uint8_t remap[256], const char *from, const char *to);

/* ---- patches and fills ---- */

/* Blit `tex` with its top-left corner at (x, y). Index 255 is transparent.
 * The TEX origin is ignored: HUD art is placed by its top-left corner, as
 * every box in hud.md is. Drawn column by column (TEX is column-major). */
void draw_patch(uint8_t *fb, const bast_tex *tex, int x, int y);
/* Same, every opaque pixel written as remap[index]. */
void draw_patch_remap(uint8_t *fb, const bast_tex *tex, int x, int y, const uint8_t remap[256]);
/* Sub-rectangle (sx, sy, w, h) of `tex` drawn at (x, y); remap may be NULL. */
void draw_patch_region(uint8_t *fb, const bast_tex *tex, int sx, int sy, int w, int h, int x,
                       int y, const uint8_t *remap);
/* Fill the inclusive rectangle (x0,y0)-(x1,y1) with `idx` (corners in any
 * order). */
void draw_fill(uint8_t *fb, int x0, int y0, int x1, int y1, uint8_t idx);

/* ---- fonts (hud.md Fonts) ---- */

typedef struct font {
    bast_tex *tex;          /* 16-column glyph sheet, owned */
    int cell_w, cell_h;     /* cell = advance x line height */
    int16_t glyph[128];     /* ASCII -> cell index, -1 = none */
} font;

/* Load `dir`/fonts/`name`.tex, name one of "small", "menu", "bigdigits".
 * NULL on failure (unknown name, missing file, sheet size mismatch). */
font *font_load(const char *dir, const char *name);
/* Wrap an already loaded sheet (takes ownership of tex, also on failure). */
font *font_from_tex(const char *name, bast_tex *tex);
void font_free(font *f);

/* Characters a font lacks: lower case falls back to upper case if the font
 * has it, anything else draws as a blank cell (space). '\n' starts a new
 * line cell_h lower at the starting x. */

/* Width in px of the widest line: characters x cell_w (shadow included). */
int text_width(const font *f, const char *str);
/* Text with its first cell's top-left at (x, y); remap may be NULL. */
void draw_text(uint8_t *fb, const font *f, int x, int y, const char *str, const uint8_t *remap);
/* Each line right-aligned so its last pixel column (the shadow column of
 * the last cell) is exactly x_right, as hud.md's "right-aligned to x=103". */
void draw_text_right(uint8_t *fb, const font *f, int x_right, int y, const char *str,
                     const uint8_t *remap);

#endif
