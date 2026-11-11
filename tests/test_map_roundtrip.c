/* libbast .vmap: build a map in memory, save, load, compare every field;
 * check the byte layout of header, cell, entity and name table; reject
 * malformed files. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bast.h"

static const char *tmp_path(const char *name)
{
    static char buf[512];
    const char *dir = getenv("TMPDIR");
    snprintf(buf, sizeof buf, "%s/%s", dir && *dir ? dir : "/tmp", name);
    return buf;
}

static uint8_t *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*n);
    assert(b && fread(b, 1, (size_t)*n, f) == (size_t)*n);
    fclose(f);
    return b;
}

static bast_map *build(void)
{
    bast_map *m = bast_map_new(5, 3);
    assert(m);
    assert(m->texture_count == 1 && m->entity_count == 0);
    strcpy(m->name, "Roundtrip Test");
    assert(bast_map_texture_index(m, "brick") == 1);
    assert(bast_map_texture_index(m, "floor_a") == 2);
    assert(bast_map_texture_index(m, "brick") == 1); /* reused */
    assert(bast_map_texture_index(m, "") == 0);
    assert(bast_map_texture_index(m, "sky_test") == 3);
    assert(bast_map_texture_index(m, "abcdefghijklmnop") == 4); /* 16 chars, no NUL in file */
    assert(bast_map_texture_index(m, "abcdefghijklmnopq") == -1);
    m->sky_tex = 3;
    m->music = 7;
    m->par_seconds = 0xBEEF;
    for (int y = 0; y < 3; y++) {
        for (int x = 0; x < 5; x++) {
            bast_map_cell *c = bast_map_cell_at(m, x, y);
            assert(c == &m->cells[y * 5 + x]);
            c->wall = (uint8_t)((x + y) % 5);
            c->floor = (uint8_t)(x % 5);
            c->ceil = (uint8_t)(y % 5);
            c->dark = (uint8_t)(x * 7 + y) % 32;
            c->dark_alt = (uint8_t)(31 - c->dark);
            c->flags = (uint16_t)(0x0F01u * (unsigned)(x + 1) + (unsigned)y);
            c->tag = (uint8_t)(10 * y + x);
            c->param = (uint8_t)(200 + x + y);
        }
    }
    assert(bast_map_cell_at(m, 5, 0) == NULL && bast_map_cell_at(m, 0, 3) == NULL);
    assert(bast_map_cell_at(m, -1, 0) == NULL);
    for (int i = 0; i < 40; i++) { /* forces a realloc */
        int k = bast_map_add_entity(m);
        assert(k == i);
        bast_map_entity *e = &m->entities[k];
        e->type = (uint16_t)(i % 3 == 0 ? BAST_ENT_WAYPOINT : BAST_ENT_MUTT_TROOPER + i % 7);
        e->x = (int32_t)(i * 65536 + 32768);
        e->y = -(int32_t)(i * 12345) - 1; /* negative, sign handling */
        e->angle = (uint16_t)(i * 1638);
        e->flags = (uint16_t)(BAST_ENT_SKILL_ALL | (i & 1 ? BAST_ENT_AMBUSH : 0));
        e->tag = (uint8_t)i;
        e->param = (uint8_t)(255 - i);
        if (i % 3 == 0)
            snprintf(e->name, sizeof e->name, "wp%d", i);
    }
    strcpy(m->entities[39].name, "sixteen_chars_xx"); /* exactly 16 */
    return m;
}

static void compare(const bast_map *a, const bast_map *b)
{
    assert(a->width == b->width && a->height == b->height);
    assert(strcmp(a->name, b->name) == 0);
    assert(a->sky_tex == b->sky_tex && a->music == b->music);
    assert(a->par_seconds == b->par_seconds);
    assert(a->texture_count == b->texture_count);
    for (int i = 0; i < a->texture_count; i++)
        assert(strcmp(a->textures[i], b->textures[i]) == 0);
    for (int i = 0; i < a->width * a->height; i++) {
        const bast_map_cell *p = &a->cells[i], *q = &b->cells[i];
        assert(p->wall == q->wall && p->floor == q->floor && p->ceil == q->ceil);
        assert(p->dark == q->dark && p->dark_alt == q->dark_alt);
        assert(p->flags == q->flags && p->tag == q->tag && p->param == q->param);
    }
    assert(a->entity_count == b->entity_count);
    for (int i = 0; i < a->entity_count; i++) {
        const bast_map_entity *p = &a->entities[i], *q = &b->entities[i];
        assert(p->type == q->type && p->x == q->x && p->y == q->y);
        assert(p->angle == q->angle && p->flags == q->flags);
        assert(p->tag == q->tag && p->param == q->param);
        assert(strcmp(p->name, q->name) == 0);
    }
}

static void test_roundtrip(void)
{
    bast_map *m = build();
    const char *path = tmp_path("test_map_roundtrip.vmap");
    assert(bast_map_save(m, path) == 0);
    bast_map *r = bast_map_load(path);
    assert(r);
    compare(m, r);

    long n = 0;
    uint8_t *d = slurp(path, &n);
    /* 48 header + 5*16 names + 15*9 cells + 40*16 entities + 2 + 15*18 names */
    assert(n == 48 + 5 * 16 + 15 * 9 + 40 * 16 + 2 + 14 * 18);
    assert(memcmp(d, "VMAP", 4) == 0 && d[4] == 1 && d[5] == 0);
    assert(d[6] == 5 && d[7] == 3);
    assert(memcmp(d + 8, "Roundtrip Test", 15) == 0);
    assert(d[40] == 3 && d[41] == 7 && d[42] == 0xEF && d[43] == 0xBE);
    assert(d[44] == 40 && d[45] == 0 && d[46] == 5 && d[47] == 0);
    for (int i = 0; i < 16; i++)
        assert(d[48 + i] == 0); /* texture 0 = "" */
    assert(memcmp(d + 48 + 16, "brick\0", 6) == 0);
    assert(memcmp(d + 48 + 64, "abcdefghijklmnop", 16) == 0);
    /* cell (1, 2): row-major from the south-west */
    const uint8_t *c = d + 48 + 80 + (2 * 5 + 1) * 9;
    const bast_map_cell *mc = bast_map_cell_at(m, 1, 2);
    assert(c[0] == mc->wall && c[1] == mc->floor && c[2] == mc->ceil);
    assert(c[3] == mc->dark && c[4] == mc->dark_alt);
    assert(c[5] == (mc->flags & 0xff) && c[6] == mc->flags >> 8);
    assert(c[7] == mc->tag && c[8] == mc->param);
    /* entity 1 */
    const uint8_t *e = d + 48 + 80 + 135 + 16;
    assert(e[0] == BAST_ENT_MUTT_TROOPER + 1 && e[1] == 0);
    assert(e[2] == 0x00 && e[3] == 0x80 && e[4] == 1 && e[5] == 0); /* x = 0x18000 */
    assert(e[6] == 0xC6 && e[7] == 0xCF && e[8] == 0xFF && e[9] == 0xFF); /* -12346 */
    assert(e[10] == (1638 & 0xff) && e[11] == 1638 >> 8);
    assert(e[12] == 0x0F && e[13] == 0 && e[14] == 1 && e[15] == 254);
    /* name table: count, then (index, name) sorted by index */
    const uint8_t *t = d + 48 + 80 + 135 + 640;
    assert(t[0] == 14 && t[1] == 0);
    assert(t[2] == 0 && t[3] == 0 && memcmp(t + 4, "wp0\0", 4) == 0);
    assert(t[20] == 3 && t[21] == 0 && memcmp(t + 22, "wp3\0", 4) == 0);
    assert(memcmp(d + n - 16, "sixteen_chars_xx", 16) == 0);

    /* load + save again is byte-identical */
    const char *path2 = tmp_path("test_map_roundtrip2.vmap");
    assert(bast_map_save(r, path2) == 0);
    long n2 = 0;
    uint8_t *d2 = slurp(path2, &n2);
    assert(n2 == n && memcmp(d, d2, (size_t)n) == 0);

    /* from_memory agrees with load */
    bast_map *mm = bast_map_from_memory(d, n);
    assert(mm);
    compare(m, mm);
    bast_map_free(mm);

    /* malformed input is rejected */
    uint8_t *bad = malloc((size_t)n);
    memcpy(bad, d, (size_t)n);
    assert(!bast_map_from_memory(bad, n - 1)); /* name table length off */
    assert(!bast_map_from_memory(bad, 40));
    bad[4] = 2;
    assert(!bast_map_from_memory(bad, n) && strstr(bast_error(), "version"));
    bad[4] = 1;
    bad[6] = 65;
    assert(!bast_map_from_memory(bad, n));
    bad[6] = 5;
    bad[48 + 80 + 3] = 32; /* dark out of range */
    assert(!bast_map_from_memory(bad, n) && strstr(bast_error(), "dark"));
    bad[48 + 80 + 3] = d[48 + 80 + 3];
    bad[48 + 80 + 0] = 5; /* wall texture index out of range */
    assert(!bast_map_from_memory(bad, n) && strstr(bast_error(), "texture"));
    bad[48 + 80 + 0] = d[48 + 80 + 0];
    bad[48 + 16 + 7] = 'x'; /* garbage after "brick\0" */
    assert(!bast_map_from_memory(bad, n));
    bad[48 + 16 + 7] = 0;
    bast_map *ok = bast_map_from_memory(bad, n); /* restored: valid again */
    assert(ok);
    bast_map_free(ok);
    free(bad);

    /* a map without names has no table; with it truncated it still loads */
    for (int i = 0; i < r->entity_count; i++)
        r->entities[i].name[0] = 0;
    assert(bast_map_save(r, path2) == 0);
    free(d2);
    d2 = slurp(path2, &n2);
    assert(n2 == 48 + 80 + 135 + 640);
    bast_map *nn = bast_map_load(path2);
    assert(nn && nn->entity_count == 40 && nn->entities[0].name[0] == 0);
    bast_map_free(nn);

    /* save validates */
    r->cells[0].dark = 40;
    assert(bast_map_save(r, path2) == -1 && strstr(bast_error(), "dark"));
    r->cells[0].dark = 0;
    r->cells[0].ceil = 9;
    assert(bast_map_save(r, path2) == -1);
    r->cells[0].ceil = 0;
    assert(bast_map_remove_entity(r, 0) == 0 && r->entity_count == 39);
    assert(r->entities[0].type == BAST_ENT_MUTT_TROOPER + 1);
    assert(bast_map_remove_entity(r, 39) == -1);
    assert(!bast_map_load(tmp_path("does_not_exist.vmap")));

    free(d);
    free(d2);
    bast_map_free(r);
    bast_map_free(m);
    remove(path);
    remove(path2);
}

static void test_names(void)
{
    assert(strcmp(bast_entity_type_name(BAST_ENT_PLAYER_START), "PLAYER_START") == 0);
    assert(bast_entity_type_from_name("WAYPOINT") == BAST_ENT_WAYPOINT);
    assert(bast_entity_type_from_name("NINTH_LIFE") == BAST_ENT_NINTH_LIFE);
    assert(bast_entity_type_from_name("nope") == 0);
    assert(bast_entity_type_name(0) == NULL && bast_entity_type_name(7) == NULL);
    for (int t = 1; t < BAST_ENT_TYPE_LIMIT; t++) {
        const char *n = bast_entity_type_name(t);
        if (n)
            assert(bast_entity_type_from_name(n) == t);
    }
    assert(strcmp(bast_cell_flag_name(0), "SOLID") == 0);
    assert(strcmp(bast_cell_flag_name(11), "TRIGGER") == 0);
    assert(bast_cell_flag_name(12) == NULL && bast_cell_flag_name(16) == NULL);
    assert(1u << 10 == BAST_CELL_BLOCK_ALL);
    assert(strcmp(bast_entity_flag_name(5), "PICKUP_TRIGGER") == 0);
    assert(bast_entity_flag_name(6) == NULL);
}

int main(void)
{
    test_names();
    test_roundtrip();
    puts("map_roundtrip: ok");
    return 0;
}
