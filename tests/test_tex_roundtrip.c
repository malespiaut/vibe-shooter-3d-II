/* libbast TEX: header layout, column-major order, save/load round trip,
 * rgba conversion. Usage: test_tex_roundtrip <palette.bin> */
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

/* 3 wide, 2 high, every pixel distinct so the order is unambiguous.
 * Row-major view:   y0: 10 11 12
 *                   y1: 20 21 22
 * Column-major data: 10 20 11 21 12 22 */
static void test_column_major_and_header(void)
{
    bast_tex *t = bast_tex_new(3, 2);
    assert(t && t->width == 3 && t->height == 2 && t->flags == 0);
    for (int x = 0; x < 3; x++)
        for (int y = 0; y < 2; y++)
            t->pixels[x * 2 + y] = (uint8_t)(10 * (y + 1) + x);
    t->origin_x = -2;  /* negative: checks int16 two's complement */
    t->origin_y = 0x0102;
    t->flags = 0x03;
    const char *p = tmp_path("bast_test_3x2.tex");
    assert(bast_tex_save(t, p) == 0);

    long n;
    uint8_t *b = slurp(p, &n);
    static const uint8_t want[16 + 6] = {
        'T', 'E', 'X', '1',
        3, 0,          /* width  */
        2, 0,          /* height */
        0xFE, 0xFF,    /* origin_x = -2 */
        0x02, 0x01,    /* origin_y = 0x0102 */
        0x03,          /* flags */
        0, 0, 0,       /* reserved */
        10, 20, 11, 21, 12, 22,
    };
    assert(n == (long)sizeof want);
    for (long i = 0; i < n; i++) {
        if (b[i] != want[i])
            fprintf(stderr, "byte %ld: got %u want %u\n", i, b[i], want[i]);
        assert(b[i] == want[i]);
    }

    bast_tex *u = bast_tex_load(p);
    assert(u);
    assert(u->width == 3 && u->height == 2);
    assert(u->origin_x == -2 && u->origin_y == 0x0102 && u->flags == 0x03);
    assert(memcmp(u->pixels, t->pixels, 6) == 0);

    /* Corruptions are rejected. */
    b[13] = 1;
    assert(!bast_tex_from_memory(b, n));
    b[13] = 0;
    assert(!bast_tex_from_memory(b, n - 1));
    b[0] = 'X';
    assert(!bast_tex_from_memory(b, n));
    b[0] = 'T';
    b[4] = 0;
    b[5] = 0; /* width 0 */
    assert(!bast_tex_from_memory(b, n));

    free(b);
    bast_tex_free(t);
    bast_tex_free(u);
    remove(p);
}

static void test_big_roundtrip(void)
{
    bast_tex *t = bast_tex_new(64, 37);
    assert(t);
    uint32_t s = 12345;
    for (int i = 0; i < 64 * 37; i++) {
        s = s * 1103515245u + 12345u;
        t->pixels[i] = (uint8_t)(s >> 16);
    }
    t->origin_x = 32;
    t->origin_y = 37;
    const char *p = tmp_path("bast_test_big.tex");
    assert(bast_tex_save(t, p) == 0);
    bast_tex *u = bast_tex_load(p);
    assert(u && u->width == 64 && u->height == 37 && u->origin_x == 32 && u->origin_y == 37);
    assert(memcmp(u->pixels, t->pixels, 64 * 37) == 0);
    bast_tex_free(t);
    bast_tex_free(u);
    remove(p);
    assert(!bast_tex_new(0, 4) && !bast_tex_new(4, 1025));
    assert(bast_tex_load(tmp_path("bast_no_such_file.tex")) == NULL);
    assert(strstr(bast_error(), "bast_no_such_file.tex"));
}

static void test_from_rgba(const char *palpath)
{
    bast_palette *pal = bast_palette_load(palpath);
    assert(pal);
    /* 2x2: white, black / translucent white (alpha 127), opaque magenta (alpha 128) */
    const uint8_t rgba[16] = {
        255, 255, 255, 255,   0, 0, 0, 255,
        255, 255, 255, 127,   255, 0, 255, 128,
    };
    bast_tex *t = bast_tex_from_rgba(pal, rgba, 2, 2);
    assert(t && t->width == 2 && t->height == 2);
    assert(t->pixels[0] == 1);   /* (0,0) white */
    assert(t->pixels[1] == 255); /* (0,1) alpha 127 -> transparent */
    assert(t->pixels[2] == 0);   /* (1,0) black */
    /* (1,1) opaque #FF00FF must not become transparent index 255 */
    assert(t->pixels[3] != 255 && !(pal->flags[t->pixels[3]] & 0x07));
    assert(t->flags == BAST_TEX_HAS_TRANSPARENCY);

    const uint8_t opaque[4] = {255, 255, 255, 255};
    bast_tex *o = bast_tex_from_rgba(pal, opaque, 1, 1);
    assert(o && o->flags == 0 && o->pixels[0] == 1);
    bast_tex_free(o);
    bast_tex_free(t);
    bast_palette_free(pal);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    test_column_major_and_header();
    test_big_roundtrip();
    test_from_rgba(argv[1]);
    printf("test_tex_roundtrip: OK\n");
    return 0;
}
