/* Map format v1 (.vmap). docs/design/tech.md, "Map". */
#include "bast_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_FIXED 48 /* bytes before the texture name table */

/* ---- names ------------------------------------------------------------ */

static const struct {
    int type;
    const char *name;
} entity_names[] = {
    {BAST_ENT_PLAYER_START, "PLAYER_START"},
    {BAST_ENT_WAYPOINT, "WAYPOINT"},
    {BAST_ENT_KILL_TRIGGER, "KILL_TRIGGER"},
    {BAST_ENT_LIGHTS, "LIGHTS"},
    {BAST_ENT_BOSS_POD, "BOSS_POD"},
    {BAST_ENT_RESPAWN_ITEM, "RESPAWN_ITEM"},
    {BAST_ENT_TEST_SPRITE, "TEST_SPRITE"},
    {BAST_ENT_MUTT_TROOPER, "MUTT_TROOPER"},
    {BAST_ENT_CHIHUAHUA_CHOMPER, "CHIHUAHUA_CHOMPER"},
    {BAST_ENT_POMERANIAN_PLASMAGUNNER, "POMERANIAN_PLASMAGUNNER"},
    {BAST_ENT_DACHSHUND_HOWITZER, "DACHSHUND_HOWITZER"},
    {BAST_ENT_MASTIFF_JUGGERNAUT, "MASTIFF_JUGGERNAUT"},
    {BAST_ENT_SHEPHERD_SHIELDBEARER, "SHEPHERD_SHIELDBEARER"},
    {BAST_ENT_WOOFU, "WOOFU"},
    {BAST_ENT_ANKH_PISTOL, "ANKH_PISTOL"},
    {BAST_ENT_HAIRBALL_SCATTERGUN, "HAIRBALL_SCATTERGUN"},
    {BAST_ENT_SISTRUM_REPEATER, "SISTRUM_REPEATER"},
    {BAST_ENT_SCARAB_LAUNCHER, "SCARAB_LAUNCHER"},
    {BAST_ENT_EYE_OF_RA_LANCE, "EYE_OF_RA_LANCE"},
    {BAST_ENT_SPHINX_HOWLER, "SPHINX_HOWLER"},
    {BAST_ENT_FISH_SNACK, "FISH_SNACK"},
    {BAST_ENT_TUNA_TIN, "TUNA_TIN"},
    {BAST_ENT_GOLDEN_SARDINE, "GOLDEN_SARDINE"},
    {BAST_ENT_LINEN_WRAP, "LINEN_WRAP"},
    {BAST_ENT_SCARAB_PLATE, "SCARAB_PLATE"},
    {BAST_ENT_BANDAGE_SCRAP, "BANDAGE_SCRAP"},
    {BAST_ENT_CHARGE_VIAL, "CHARGE_VIAL"},
    {BAST_ENT_CHARGE_JAR, "CHARGE_JAR"},
    {BAST_ENT_YARN_BUNDLE, "YARN_BUNDLE"},
    {BAST_ENT_YARN_BASKET, "YARN_BASKET"},
    {BAST_ENT_SCARAB, "SCARAB"},
    {BAST_ENT_SCARAB_CRATE, "SCARAB_CRATE"},
    {BAST_ENT_SUN_SHARD, "SUN_SHARD"},
    {BAST_ENT_SUN_ORB, "SUN_ORB"},
    {BAST_ENT_LITTER_SATCHEL, "LITTER_SATCHEL"},
    {BAST_ENT_GREEN_ANKH, "GREEN_ANKH"},
    {BAST_ENT_BLUE_ANKH, "BLUE_ANKH"},
    {BAST_ENT_VIOLET_ANKH, "VIOLET_ANKH"},
    {BAST_ENT_CATNIP_FRENZY, "CATNIP_FRENZY"},
    {BAST_ENT_CATS_EYE_AMULET, "CATS_EYE_AMULET"},
    {BAST_ENT_NINTH_LIFE, "NINTH_LIFE"},
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const char *const cell_flag_names[16] = {
    "SOLID", "DOOR", "PUSHWALL", "SWITCH", "EXIT", "SECRET", "DAMAGE", "ONEWAY",
    "START_OPEN", "BLOCK_MONSTER", "BLOCK_ALL", "TRIGGER",
};

static const char *const entity_flag_names[16] = {
    "SKILL1", "SKILL2", "SKILL3", "AMBUSH", "SECRET_ONLY", "PICKUP_TRIGGER",
};

const char *bast_entity_type_name(int type)
{
    for (int i = 0; i < COUNT(entity_names); i++)
        if (entity_names[i].type == type)
            return entity_names[i].name;
    return NULL;
}

int bast_entity_type_from_name(const char *name)
{
    if (!name)
        return 0;
    for (int i = 0; i < COUNT(entity_names); i++)
        if (strcmp(entity_names[i].name, name) == 0)
            return entity_names[i].type;
    return 0;
}

const char *bast_cell_flag_name(int bit)
{
    return bit >= 0 && bit < 16 ? cell_flag_names[bit] : NULL;
}

const char *bast_entity_flag_name(int bit)
{
    return bit >= 0 && bit < 16 ? entity_flag_names[bit] : NULL;
}

/* ---- memory ----------------------------------------------------------- */

bast_map *bast_map_new(int width, int height)
{
    if (width < 1 || width > BAST_MAP_MAX_DIM || height < 1 || height > BAST_MAP_MAX_DIM) {
        bast_set_error("bad map size %dx%d (1..%d)", width, height, BAST_MAP_MAX_DIM);
        return NULL;
    }
    bast_map *m = calloc(1, sizeof *m);
    bast_map_cell *c = calloc((size_t)width * (size_t)height, sizeof *c);
    if (!m || !c) {
        free(m);
        free(c);
        bast_set_error("out of memory");
        return NULL;
    }
    m->width = (uint8_t)width;
    m->height = (uint8_t)height;
    m->texture_count = 1;
    m->cells = c;
    return m;
}

void bast_map_free(bast_map *m)
{
    if (m) {
        free(m->cells);
        free(m->entities);
        free(m);
    }
}

bast_map_cell *bast_map_cell_at(bast_map *m, int x, int y)
{
    if (!m || x < 0 || y < 0 || x >= m->width || y >= m->height)
        return NULL;
    return &m->cells[(size_t)y * m->width + x];
}

int bast_map_add_entity(bast_map *m)
{
    if (m->entity_count >= BAST_MAP_MAX_ENTITIES) {
        bast_set_error("too many entities (max %d)", BAST_MAP_MAX_ENTITIES);
        return -1;
    }
    if (m->entity_count == m->entity_capacity) {
        unsigned cap = m->entity_capacity ? m->entity_capacity * 2u : 16u;
        if (cap > BAST_MAP_MAX_ENTITIES)
            cap = BAST_MAP_MAX_ENTITIES;
        bast_map_entity *e = realloc(m->entities, cap * sizeof *e);
        if (!e) {
            bast_set_error("out of memory");
            return -1;
        }
        m->entities = e;
        m->entity_capacity = (uint16_t)cap;
    }
    int i = m->entity_count++;
    memset(&m->entities[i], 0, sizeof m->entities[i]);
    return i;
}

int bast_map_remove_entity(bast_map *m, int index)
{
    if (index < 0 || index >= m->entity_count) {
        bast_set_error("no entity %d", index);
        return -1;
    }
    memmove(&m->entities[index], &m->entities[index + 1],
            (size_t)(m->entity_count - index - 1) * sizeof m->entities[0]);
    m->entity_count--;
    return 0;
}

int bast_map_texture_index(bast_map *m, const char *name)
{
    if (!name || !name[0])
        return 0;
    if (strlen(name) > BAST_MAP_TEXNAME_LEN) {
        bast_set_error("texture name '%s' longer than %d", name, BAST_MAP_TEXNAME_LEN);
        return -1;
    }
    for (int i = 1; i < m->texture_count; i++)
        if (strcmp(m->textures[i], name) == 0)
            return i;
    if (m->texture_count >= BAST_MAP_MAX_TEXTURES) {
        bast_set_error("texture table full (%d)", BAST_MAP_MAX_TEXTURES);
        return -1;
    }
    int i = m->texture_count++;
    memset(m->textures[i], 0, sizeof m->textures[i]);
    memcpy(m->textures[i], name, strlen(name));
    return i;
}

/* ---- file I/O --------------------------------------------------------- */

static unsigned rd16(const uint8_t *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)(v >> 8 & 0xff);
}

static void wr32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i) & 0xff);
}

/* Fixed-size NUL-padded string field -> C string. Rejects non-zero bytes
 * after the first NUL so that load + save is byte-identical. */
static int rdstr(char *dst, const uint8_t *src, int len, const char *what)
{
    int n = 0;
    while (n < len && src[n])
        n++;
    for (int i = n; i < len; i++) {
        if (src[i]) {
            bast_set_error("%s: garbage after string terminator", what);
            return -1;
        }
    }
    memcpy(dst, src, (size_t)n);
    dst[n] = 0;
    return 0;
}

static void wrstr(uint8_t *dst, const char *src, int len)
{
    size_t n = 0;
    while (n < (size_t)len && src[n])
        n++;
    memset(dst, 0, (size_t)len);
    memcpy(dst, src, n);
}

static int32_t s32(uint32_t v)
{
    return v & 0x80000000u ? (int32_t)(v - 0x80000000u) - INT32_MAX - 1 : (int32_t)v;
}

/* Check the in-memory map is writable as a valid file. */
static int validate(const bast_map *m)
{
    if (m->width < 1 || m->width > BAST_MAP_MAX_DIM || m->height < 1 ||
        m->height > BAST_MAP_MAX_DIM) {
        bast_set_error("bad map size %dx%d", m->width, m->height);
        return -1;
    }
    if (strlen(m->name) > BAST_MAP_NAME_LEN) {
        bast_set_error("map name longer than %d", BAST_MAP_NAME_LEN);
        return -1;
    }
    if (m->texture_count < 1 || m->texture_count > BAST_MAP_MAX_TEXTURES) {
        bast_set_error("bad texture count %d", m->texture_count);
        return -1;
    }
    if (m->textures[0][0]) {
        bast_set_error("texture 0 must be empty (none/sky)");
        return -1;
    }
    for (int i = 0; i < m->texture_count; i++) {
        if (strlen(m->textures[i]) > BAST_MAP_TEXNAME_LEN) {
            bast_set_error("texture %d name longer than %d", i, BAST_MAP_TEXNAME_LEN);
            return -1;
        }
        if (i > 0 && !m->textures[i][0]) {
            bast_set_error("texture %d has an empty name", i);
            return -1;
        }
    }
    if (m->sky_tex >= m->texture_count) {
        bast_set_error("sky_tex %d >= texture count %d", m->sky_tex, m->texture_count);
        return -1;
    }
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            const bast_map_cell *c = &m->cells[y * m->width + x];
            if (c->wall >= m->texture_count || c->floor >= m->texture_count ||
                c->ceil >= m->texture_count) {
                bast_set_error("cell (%d,%d): texture index out of range", x, y);
                return -1;
            }
            if (c->dark > BAST_MAP_MAX_DARK || c->dark_alt > BAST_MAP_MAX_DARK) {
                bast_set_error("cell (%d,%d): dark out of range 0..%d", x, y, BAST_MAP_MAX_DARK);
                return -1;
            }
        }
    }
    for (int i = 0; i < m->entity_count; i++) {
        if (strlen(m->entities[i].name) > BAST_MAP_ENTNAME_LEN) {
            bast_set_error("entity %d name longer than %d", i, BAST_MAP_ENTNAME_LEN);
            return -1;
        }
        if (m->entities[i].type == BAST_ENT_NONE) {
            bast_set_error("entity %d has type 0", i);
            return -1;
        }
    }
    return 0;
}

bast_map *bast_map_from_memory(const uint8_t *d, int64_t size)
{
    if (size < HEADER_FIXED || memcmp(d, "VMAP", 4) != 0) {
        bast_set_error("not a VMAP file");
        return NULL;
    }
    if (rd16(d + 4) != BAST_MAP_VERSION) {
        bast_set_error("unsupported VMAP version %u", rd16(d + 4));
        return NULL;
    }
    bast_map *m = bast_map_new(d[6], d[7]);
    if (!m)
        return NULL;
    if (rdstr(m->name, d + 8, BAST_MAP_NAME_LEN, "map name"))
        goto fail;
    m->sky_tex = d[40];
    m->music = d[41];
    m->par_seconds = (uint16_t)rd16(d + 42);
    unsigned ents = rd16(d + 44), texs = rd16(d + 46);
    if (texs < 1 || texs > BAST_MAP_MAX_TEXTURES) {
        bast_set_error("bad texture count %u", texs);
        goto fail;
    }
    int64_t ncells = (int64_t)m->width * m->height;
    int64_t need = HEADER_FIXED + (int64_t)texs * BAST_MAP_TEXNAME_LEN +
                   ncells * BAST_MAP_CELL_SIZE + (int64_t)ents * BAST_MAP_ENTITY_SIZE;
    if (size < need) {
        bast_set_error("VMAP truncated: %lld bytes, need %lld", (long long)size, (long long)need);
        goto fail;
    }
    const uint8_t *p = d + HEADER_FIXED;
    m->texture_count = (uint16_t)texs;
    for (unsigned i = 0; i < texs; i++, p += BAST_MAP_TEXNAME_LEN)
        if (rdstr(m->textures[i], p, BAST_MAP_TEXNAME_LEN, "texture name"))
            goto fail;
    for (int64_t i = 0; i < ncells; i++, p += BAST_MAP_CELL_SIZE) {
        bast_map_cell *c = &m->cells[i];
        c->wall = p[0];
        c->floor = p[1];
        c->ceil = p[2];
        c->dark = p[3];
        c->dark_alt = p[4];
        c->flags = (uint16_t)rd16(p + 5);
        c->tag = p[7];
        c->param = p[8];
    }
    for (unsigned i = 0; i < ents; i++, p += BAST_MAP_ENTITY_SIZE) {
        int k = bast_map_add_entity(m);
        if (k < 0)
            goto fail;
        bast_map_entity *e = &m->entities[k];
        e->type = (uint16_t)rd16(p);
        e->x = s32(rd32(p + 2));
        e->y = s32(rd32(p + 6));
        e->angle = (uint16_t)rd16(p + 10);
        e->flags = (uint16_t)rd16(p + 12);
        e->tag = p[14];
        e->param = p[15];
    }
    int64_t left = size - (p - d);
    if (left > 0) {
        if (left < 2) {
            bast_set_error("VMAP name table truncated");
            goto fail;
        }
        unsigned n = rd16(p);
        p += 2;
        if (left != 2 + (int64_t)n * (2 + BAST_MAP_ENTNAME_LEN)) {
            bast_set_error("VMAP name table: %lld bytes left for %u names", (long long)left, n);
            goto fail;
        }
        int prev = -1;
        for (unsigned i = 0; i < n; i++, p += 2 + BAST_MAP_ENTNAME_LEN) {
            int idx = (int)rd16(p);
            if (idx >= m->entity_count || idx <= prev) {
                bast_set_error("VMAP name table: bad or unsorted entity index %d", idx);
                goto fail;
            }
            prev = idx;
            if (rdstr(m->entities[idx].name, p + 2, BAST_MAP_ENTNAME_LEN, "entity name"))
                goto fail;
            if (!m->entities[idx].name[0]) {
                bast_set_error("VMAP name table: empty name for entity %d", idx);
                goto fail;
            }
        }
    }
    if (validate(m))
        goto fail;
    return m;
fail:
    bast_map_free(m);
    return NULL;
}

bast_map *bast_map_load(const char *path)
{
    int64_t size = 0;
    uint8_t *d = bast_read_file(path, &size);
    if (!d)
        return NULL;
    bast_map *m = bast_map_from_memory(d, size);
    if (!m) {
        char msg[200];
        snprintf(msg, sizeof msg, "%s", bast_error());
        bast_set_error("%s: %s", path, msg);
    }
    free(d);
    return m;
}

int bast_map_save(const bast_map *m, const char *path)
{
    if (!m || validate(m)) {
        if (!m)
            bast_set_error("no map");
        char msg[200];
        snprintf(msg, sizeof msg, "%s", bast_error());
        bast_set_error("%s: %s", path, msg);
        return -1;
    }
    int named = 0;
    for (int i = 0; i < m->entity_count; i++)
        named += m->entities[i].name[0] != 0;
    size_t ncells = (size_t)m->width * m->height;
    size_t size = HEADER_FIXED + (size_t)m->texture_count * BAST_MAP_TEXNAME_LEN +
                  ncells * BAST_MAP_CELL_SIZE + (size_t)m->entity_count * BAST_MAP_ENTITY_SIZE +
                  (named ? 2 + (size_t)named * (2 + BAST_MAP_ENTNAME_LEN) : 0);
    uint8_t *d = calloc(size, 1);
    if (!d) {
        bast_set_error("out of memory");
        return -1;
    }
    memcpy(d, "VMAP", 4);
    wr16(d + 4, BAST_MAP_VERSION);
    d[6] = m->width;
    d[7] = m->height;
    wrstr(d + 8, m->name, BAST_MAP_NAME_LEN);
    d[40] = m->sky_tex;
    d[41] = m->music;
    wr16(d + 42, m->par_seconds);
    wr16(d + 44, m->entity_count);
    wr16(d + 46, m->texture_count);
    uint8_t *p = d + HEADER_FIXED;
    for (int i = 0; i < m->texture_count; i++, p += BAST_MAP_TEXNAME_LEN)
        wrstr(p, m->textures[i], BAST_MAP_TEXNAME_LEN);
    for (size_t i = 0; i < ncells; i++, p += BAST_MAP_CELL_SIZE) {
        const bast_map_cell *c = &m->cells[i];
        p[0] = c->wall;
        p[1] = c->floor;
        p[2] = c->ceil;
        p[3] = c->dark;
        p[4] = c->dark_alt;
        wr16(p + 5, c->flags);
        p[7] = c->tag;
        p[8] = c->param;
    }
    for (int i = 0; i < m->entity_count; i++, p += BAST_MAP_ENTITY_SIZE) {
        const bast_map_entity *e = &m->entities[i];
        wr16(p, e->type);
        wr32(p + 2, (uint32_t)e->x);
        wr32(p + 6, (uint32_t)e->y);
        wr16(p + 10, e->angle);
        wr16(p + 12, e->flags);
        p[14] = e->tag;
        p[15] = e->param;
    }
    if (named) {
        wr16(p, (unsigned)named);
        p += 2;
        for (int i = 0; i < m->entity_count; i++) {
            if (!m->entities[i].name[0])
                continue;
            wr16(p, (unsigned)i);
            wrstr(p + 2, m->entities[i].name, BAST_MAP_ENTNAME_LEN);
            p += 2 + BAST_MAP_ENTNAME_LEN;
        }
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        bast_set_error("%s: %s", path, strerror(errno));
        free(d);
        return -1;
    }
    int ok = fwrite(d, 1, size, f) == size;
    if (fclose(f) != 0)
        ok = 0;
    free(d);
    if (!ok) {
        bast_set_error("%s: write error", path);
        return -1;
    }
    return 0;
}
