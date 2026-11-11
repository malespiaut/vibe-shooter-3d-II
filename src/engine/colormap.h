#ifndef ENGINE_COLORMAP_H
#define ENGINE_COLORMAP_H

/* Palette + 32-row light colormap (GAME.md 2.3, docs/design/tech.md).
 * colormap.bin is built by tools/colormap.py; row 0 is full light (identity),
 * row 31 the darkest. */

#include <stddef.h>
#include <stdint.h>

enum {
    COLORMAP_ROWS = 32,
    COLORMAP_BYTES = COLORMAP_ROWS * 256
};

/* Load `dir`/palette.bin (768 B, via libbast) and `dir`/colormap.bin
 * (8192 B). Returns 1 on success, 0 on failure (see colormap_error()).
 * On failure the previously loaded tables are kept. */
int colormap_load(const char *dir);
/* Same from memory (rgb: 768 bytes). size must be COLORMAP_BYTES and row 0
 * must be the identity. */
int colormap_set(const uint8_t *rgb, const uint8_t *cmap, size_t size);
const char *colormap_error(void);

/* 768-byte RGB palette, for platform_present(). */
const uint8_t *colormap_palette(void);
/* 256-byte row r; r is clamped to 0..COLORMAP_ROWS-1. */
const uint8_t *colormap_row(int r);

#endif
