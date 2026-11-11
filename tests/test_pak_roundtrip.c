/* VPAK roundtrip (docs/design/tech.md "Archives"): write 3 entries, reopen,
 * find each, missing -> NULL, duplicate names rejected, corrupt files
 * rejected. argv[1]: scratch directory. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char path[1024];
    snprintf(path, sizeof path, "%s/test_pak_roundtrip.pak", dir);

    static const uint8_t a[] = { 1, 2, 3, 4, 5 };
    static const uint8_t c[] = { 0xff };
    const char *names[] = { "palette.bin", "sfx/door_open.snd", "empty" };
    const uint8_t *datas[] = { a, c, NULL };
    const uint32_t sizes[] = { sizeof a, sizeof c, 0 };
    assert(bast_pak_write(path, names, datas, sizes, 3) == 0);

    /* Exact layout: 8 + 3 * 64 directory, then data in order. */
    FILE *f = fopen(path, "rb");
    assert(f);
    uint8_t raw[256];
    size_t n = fread(raw, 1, sizeof raw, f);
    fclose(f);
    assert(n == 8 + 3 * 64 + 6);
    assert(memcmp(raw, "VPAK", 4) == 0 && raw[4] == 3 && raw[5] == 0);
    assert(strcmp((const char *)raw + 8, "palette.bin") == 0);
    assert(raw[8 + 56] == 200 && raw[8 + 60] == 5);           /* offset, size */
    assert(raw[8 + 64 + 56] == 205 && raw[8 + 64 + 60] == 1);

    bast_pak *pak = bast_pak_open(path);
    assert(pak);
    assert(bast_pak_count(pak) == 3);
    assert(strcmp(bast_pak_name(pak, 1), "sfx/door_open.snd") == 0);
    assert(bast_pak_name(pak, 3) == NULL);
    uint32_t sz = 99;
    const uint8_t *p = bast_pak_find(pak, "palette.bin", &sz);
    assert(p && sz == sizeof a && memcmp(p, a, sz) == 0);
    p = bast_pak_find(pak, "sfx/door_open.snd", &sz);
    assert(p && sz == 1 && p[0] == 0xff);
    p = bast_pak_find(pak, "empty", &sz);
    assert(p && sz == 0);
    assert(bast_pak_find(pak, "missing.bin", &sz) == NULL);
    assert(bast_pak_find(pak, "palette", &sz) == NULL); /* no prefix match */
    bast_pak_close(pak);

    /* Duplicates and bad names are rejected; no file is written. */
    char dup_path[1100];
    snprintf(dup_path, sizeof dup_path, "%s/test_pak_dup.pak", dir);
    remove(dup_path);
    const char *dup[] = { "x", "y", "x" };
    assert(bast_pak_write(dup_path, dup, datas, sizes, 3) == -1);
    assert(strstr(bast_error(), "duplicate"));
    assert(fopen(dup_path, "rb") == NULL);
    const char *longname[] = { "fx/01234567890123456789012345678901234567890123456789012" }; /* 56 */
    assert(bast_pak_write(dup_path, longname, datas, sizes, 1) == -1);
    const char *maxname[] = { "fx/teleport_sparkle_loop_d0_f0.tex_01234567890123456789" }; /* 55 */
    assert(bast_pak_write(dup_path, maxname, datas, sizes, 1) == 0);
    pak = bast_pak_open(dup_path);
    assert(pak && bast_pak_find(pak, maxname[0], &sz) && sz == 5);
    bast_pak_close(pak);

    /* Corrupt archives: truncated data, duplicate in directory, bad magic. */
    f = fopen(dup_path, "wb");
    fwrite(raw, 1, n - 1, f);
    fclose(f);
    assert(bast_pak_open(dup_path) == NULL);
    uint8_t bad[sizeof raw];
    memcpy(bad, raw, n);
    memcpy(bad + 8 + 64, "palette.bin\0\0\0\0\0\0\0", 18);
    f = fopen(dup_path, "wb");
    fwrite(bad, 1, n, f);
    fclose(f);
    assert(bast_pak_open(dup_path) == NULL && strstr(bast_error(), "duplicate"));
    bad[0] = 'X';
    f = fopen(dup_path, "wb");
    fwrite(bad, 1, n, f);
    fclose(f);
    assert(bast_pak_open(dup_path) == NULL);
    assert(bast_pak_open("/nonexistent/game.pak") == NULL);

    remove(path);
    remove(dup_path);
    puts("test_pak_roundtrip: OK");
    return 0;
}
