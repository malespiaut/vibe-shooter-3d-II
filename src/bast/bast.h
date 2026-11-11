/* libbast: the single implementation of the shared asset formats.
 *
 * Used by the engine (linked statically) and by the Python tools through
 * libbast.so + tools/bast.py (ctypes). Spec: docs/design/tech.md, "Files and
 * formats". All binary formats are little-endian.
 *
 * Floating point is used only for Oklab colour matching (asset pipeline and
 * load time), never in gameplay logic.
 */
#ifndef BAST_H
#define BAST_H

#include <stdint.h>

/* ---- errors ---------------------------------------------------------- */

/* Message for the last failed call (static buffer, overwritten by the next
 * failure, not thread-safe). Never NULL. */
const char *bast_error(void);

/* ---- palette --------------------------------------------------------- */

#define BAST_PAL_RESERVED    0x01u /* palette_flags.bin bit0 */
#define BAST_PAL_FULLBRIGHT  0x02u /* bit1 */
#define BAST_PAL_TRANSPARENT 0x04u /* bit2 */

#define BAST_INDEX_BLACK       0
#define BAST_INDEX_WHITE       1
#define BAST_INDEX_TRANSPARENT 255

typedef struct bast_palette {
    uint8_t rgb[256][3];
    uint8_t flags[256];
    double lab[256][3]; /* Oklab of each entry, computed at load */
} bast_palette;

/* Load `path` (palette.bin, 768 bytes RGB) and the palette_flags.bin that
 * sits next to it (256 bytes). Returns NULL on error, see bast_error(). */
bast_palette *bast_palette_load(const char *path);
/* Build from memory (768 RGB bytes + 256 flag bytes). Never fails except OOM. */
bast_palette *bast_palette_from_memory(const uint8_t rgb[768], const uint8_t flags[256]);
void bast_palette_free(bast_palette *pal);

/* Which flagged entries nearest-colour matching may return. Reserved entries
 * are never returned. */
#define BAST_ALLOW_FULLBRIGHT  0x01u
#define BAST_ALLOW_TRANSPARENT 0x02u

/* Nearest palette index in Oklab (squared Euclidean distance). Skips
 * reserved, fullbright and transparent entries; ties go to the lowest index. */
int bast_palette_nearest(const bast_palette *pal, uint8_t r, uint8_t g, uint8_t b);
/* Same, with `allow` = BAST_ALLOW_* bits opting fullbright/transparent in. */
int bast_palette_nearest_ex(const bast_palette *pal, uint8_t r, uint8_t g, uint8_t b,
                            unsigned allow);

/* sRGB (0..255) -> Oklab (L, a, b). Reference matrices by Bjorn Ottosson. */
void bast_srgb_to_oklab(uint8_t r, uint8_t g, uint8_t b, double lab[3]);
/* Oklab -> sRGB 0..255. L is clamped to 0..1; out-of-gamut colours are
 * mapped by reducing chroma at constant L and hue (40-step bisection), then
 * rounded half to even. Used by the palette generator. */
void bast_oklab_to_srgb(double L, double a, double b, uint8_t rgb[3]);

/* ---- TEX ------------------------------------------------------------- */

#define BAST_TEX_HEADER_SIZE 16
#define BAST_TEX_MAX_DIM     1024

#define BAST_TEX_HAS_TRANSPARENCY 0x01u
#define BAST_TEX_FULLBRIGHT       0x02u

typedef struct bast_tex {
    uint16_t width, height;    /* 1..1024 */
    int16_t origin_x, origin_y; /* sprite anchor, px from left / top */
    uint8_t flags;              /* BAST_TEX_* */
    uint8_t *pixels;            /* width*height indices, COLUMN-MAJOR:
                                   pixel (x, y) is pixels[x * height + y] */
} bast_tex;

/* New zero-filled texture (all index 0). NULL on bad size or OOM. */
bast_tex *bast_tex_new(int width, int height);
bast_tex *bast_tex_load(const char *path);
/* Parse a TEX file held in memory. */
bast_tex *bast_tex_from_memory(const uint8_t *data, int64_t size);
/* Returns 0 on success, -1 on error (see bast_error()). */
int bast_tex_save(const bast_tex *tex, const char *path);
void bast_tex_free(bast_tex *tex);

/* Convert row-major RGBA8 (rgba[(y * w + x) * 4]) to a TEX: alpha < 128
 * becomes index 255 (and sets HAS_TRANSPARENCY), everything else goes through
 * bast_palette_nearest(). No dithering. Origin is (0, 0). */
bast_tex *bast_tex_from_rgba(const bast_palette *pal, const uint8_t *rgba, int w, int h);

/* ---- SND ------------------------------------------------------------- */

/* Sound effect: magic "SND1", uint32 rate, uint32 sample count, then
 * `count` signed 8-bit mono PCM samples. docs/design/tech.md, "Audio files". */
#define BAST_SND_HEADER_SIZE 12
#define BAST_SND_RATE        22050u
#define BAST_SND_MAX_SAMPLES (BAST_SND_RATE * 30u) /* 30 s: sanity bound */

typedef struct bast_snd {
    uint32_t rate;    /* always BAST_SND_RATE */
    uint32_t count;   /* number of samples, 0..BAST_SND_MAX_SAMPLES */
    int8_t *samples;  /* `count` samples (non-NULL even when count == 0) */
} bast_snd;

/* New silent sound of `count` samples at 22050 Hz. NULL on bad count/OOM. */
bast_snd *bast_snd_new(int64_t count);
bast_snd *bast_snd_load(const char *path);
bast_snd *bast_snd_from_memory(const uint8_t *data, int64_t size);
/* Returns 0 on success, -1 on error (see bast_error()). */
int bast_snd_save(const bast_snd *snd, const char *path);
void bast_snd_free(bast_snd *snd);

#endif
