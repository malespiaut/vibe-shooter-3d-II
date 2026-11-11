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

/* ---- map (.vmap) ------------------------------------------------------ */
/* docs/design/tech.md, "Map". Little-endian:
 *   header  "VMAP", u16 version=1, u8 width, u8 height, char name[32],
 *           u8 sky_tex, u8 music, u16 par_seconds, u16 entity_count,
 *           u16 texture_count, texture_count x char[16] (entry 0 = "" = none)
 *   cells   width*height x 9 bytes, row-major from (0,0) south-west
 *   entities entity_count x 16 bytes
 *   names   optional: u16 count, count x (u16 entity index, char[16] name)
 * Strings are NUL-padded (a full-length string has no terminator). */

#define BAST_MAP_VERSION      1
#define BAST_MAP_MAX_DIM      64
#define BAST_MAP_MAX_TEXTURES 256
#define BAST_MAP_MAX_ENTITIES 65535
#define BAST_MAP_NAME_LEN     32
#define BAST_MAP_TEXNAME_LEN  16
#define BAST_MAP_ENTNAME_LEN  16
#define BAST_MAP_MAX_DARK     31
#define BAST_MAP_CELL_SIZE    9
#define BAST_MAP_ENTITY_SIZE  16

/* Cell flags (uint16). Behaviour is defined in tech.md. */
#define BAST_CELL_SOLID         0x0001u
#define BAST_CELL_DOOR          0x0002u
#define BAST_CELL_PUSHWALL      0x0004u
#define BAST_CELL_SWITCH        0x0008u
#define BAST_CELL_EXIT          0x0010u
#define BAST_CELL_SECRET        0x0020u
#define BAST_CELL_DAMAGE        0x0040u
#define BAST_CELL_ONEWAY        0x0080u
#define BAST_CELL_START_OPEN    0x0100u
#define BAST_CELL_BLOCK_MONSTER 0x0200u
#define BAST_CELL_BLOCK_ALL     0x0400u
#define BAST_CELL_TRIGGER       0x0800u
#define BAST_CELL_FLAG_COUNT    12

/* Entity flags (uint16). */
#define BAST_ENT_SKILL1         0x0001u
#define BAST_ENT_SKILL2         0x0002u
#define BAST_ENT_SKILL3         0x0004u
#define BAST_ENT_SKILL_ALL      0x0007u
#define BAST_ENT_AMBUSH         0x0008u /* ambush/deaf */
#define BAST_ENT_SECRET_ONLY    0x0010u
#define BAST_ENT_PICKUP_TRIGGER 0x0020u /* pickup fires its tag when taken */
#define BAST_ENT_FLAG_COUNT     6

/* Entity types (uint16 in the file). Numbers are part of the file format:
 * never renumber, only append. 0 is invalid. Ranges: 1-15 specials,
 * 16-31 enemies, 32-47 weapon pickups, 48-95 items, 96+ decorations. */
enum bast_entity_type {
    BAST_ENT_NONE = 0,
    BAST_ENT_PLAYER_START = 1,
    BAST_ENT_WAYPOINT = 2,
    BAST_ENT_KILL_TRIGGER = 3,
    BAST_ENT_LIGHTS = 4,
    BAST_ENT_BOSS_POD = 5,
    BAST_ENT_RESPAWN_ITEM = 6,

    BAST_ENT_MUTT_TROOPER = 16,
    BAST_ENT_CHIHUAHUA_CHOMPER = 17,
    BAST_ENT_POMERANIAN_PLASMAGUNNER = 18,
    BAST_ENT_DACHSHUND_HOWITZER = 19,
    BAST_ENT_MASTIFF_JUGGERNAUT = 20,
    BAST_ENT_SHEPHERD_SHIELDBEARER = 21,
    BAST_ENT_WOOFU = 22,

    BAST_ENT_ANKH_PISTOL = 32,
    BAST_ENT_HAIRBALL_SCATTERGUN = 33,
    BAST_ENT_SISTRUM_REPEATER = 34,
    BAST_ENT_SCARAB_LAUNCHER = 35,
    BAST_ENT_EYE_OF_RA_LANCE = 36,
    BAST_ENT_SPHINX_HOWLER = 37,

    BAST_ENT_FISH_SNACK = 48,
    BAST_ENT_TUNA_TIN = 49,
    BAST_ENT_GOLDEN_SARDINE = 50,
    BAST_ENT_LINEN_WRAP = 51,
    BAST_ENT_SCARAB_PLATE = 52,
    BAST_ENT_BANDAGE_SCRAP = 53,
    BAST_ENT_CHARGE_VIAL = 54,
    BAST_ENT_CHARGE_JAR = 55,
    BAST_ENT_YARN_BUNDLE = 56,
    BAST_ENT_YARN_BASKET = 57,
    BAST_ENT_SCARAB = 58,
    BAST_ENT_SCARAB_CRATE = 59,
    BAST_ENT_SUN_SHARD = 60,
    BAST_ENT_SUN_ORB = 61,
    BAST_ENT_LITTER_SATCHEL = 62,
    BAST_ENT_GREEN_ANKH = 63,
    BAST_ENT_BLUE_ANKH = 64,
    BAST_ENT_VIOLET_ANKH = 65,
    BAST_ENT_CATNIP_FRENZY = 66,
    BAST_ENT_CATS_EYE_AMULET = 67,
    BAST_ENT_NINTH_LIFE = 68,

    BAST_ENT_TYPE_LIMIT = 96 /* one past the highest type number in use */
};

/* "PLAYER_START" etc. (the enum name without BAST_ENT_), NULL if unknown. */
const char *bast_entity_type_name(int type);
/* Inverse of bast_entity_type_name(); 0 if unknown. */
int bast_entity_type_from_name(const char *name);
/* Name of cell flag bit `bit` (0..15), "SOLID" etc.; NULL if unused. */
const char *bast_cell_flag_name(int bit);
/* Name of entity flag bit `bit` (0..15), "SKILL1" etc.; NULL if unused. */
const char *bast_entity_flag_name(int bit);

typedef struct bast_map_cell {
    uint8_t wall;     /* texture index, 0 = none */
    uint8_t floor;    /* texture index */
    uint8_t ceil;     /* texture index, 0 = sky */
    uint8_t dark;     /* 0..31 */
    uint8_t dark_alt; /* 0..31, after a LIGHTS entity's tag fires */
    uint16_t flags;   /* BAST_CELL_* */
    uint8_t tag;
    uint8_t param;
} bast_map_cell;

typedef struct bast_map_entity {
    uint16_t type;  /* enum bast_entity_type */
    int32_t x, y;   /* 16.16 world units */
    uint16_t angle; /* angle_t */
    uint16_t flags; /* BAST_ENT_* flags */
    uint8_t tag;
    uint8_t param;
    char name[BAST_MAP_ENTNAME_LEN + 1]; /* "" = unnamed */
} bast_map_entity;

typedef struct bast_map {
    uint8_t width, height; /* 1..64 */
    char name[BAST_MAP_NAME_LEN + 1];
    uint8_t sky_tex;
    uint8_t music;
    uint16_t par_seconds;
    uint16_t texture_count; /* 1..256, entry 0 is "" */
    char textures[BAST_MAP_MAX_TEXTURES][BAST_MAP_TEXNAME_LEN + 1];
    bast_map_cell *cells; /* width*height, cell (x, y) is cells[y * width + x] */
    uint16_t entity_count;
    uint16_t entity_capacity;
    bast_map_entity *entities;
} bast_map;

/* New map: every cell zero (no wall, texture 0, sky, dark 0), one texture
 * entry (""), no entities. NULL on bad size or OOM. */
bast_map *bast_map_new(int width, int height);
bast_map *bast_map_load(const char *path);
bast_map *bast_map_from_memory(const uint8_t *data, int64_t size);
/* Validates (texture indices, dark ranges, string lengths) then writes. The
 * name table is written only when at least one entity is named.
 * 0 on success, -1 on error. */
int bast_map_save(const bast_map *map, const char *path);
void bast_map_free(bast_map *map);

/* Cell (x, y), y grows north; NULL when out of bounds. */
bast_map_cell *bast_map_cell_at(bast_map *map, int x, int y);
/* Append a zeroed entity, return its index or -1 (full / OOM). */
int bast_map_add_entity(bast_map *map);
/* Remove entity `index`, shifting the later ones down. 0 or -1. */
int bast_map_remove_entity(bast_map *map, int index);
/* Index of texture `name` ("" -> 0), appending it if absent; -1 if the
 * table is full or the name is too long. */
int bast_map_texture_index(bast_map *map, const char *name);

#endif
