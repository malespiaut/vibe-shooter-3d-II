/* files.c: game.pak first, loose files as fallback, broken pak reported.
 * argv[1]: scratch directory (created by the caller). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bast.h"
#include "engine/files.h"

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    assert(f);
    fputs(text, f);
    fclose(f);
}

static int reads(const char *name, const char *want)
{
    int64_t n = -1;
    uint8_t *d = files_read(name, &n);
    int ok = d && n == (int64_t)strlen(want) && memcmp(d, want, (size_t)n) == 0;
    free(d);
    return ok;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char p[1024], pak[1024];
    snprintf(pak, sizeof pak, "%s/game.pak", dir);
    remove(pak);
    snprintf(p, sizeof p, "%s/both.txt", dir);
    put(p, "loose");
    snprintf(p, sizeof p, "%s/loose_only.txt", dir);
    put(p, "fallback");

    /* No archive: loose files only. */
    assert(files_init(dir) && !files_have_pak());
    assert(reads("both.txt", "loose"));
    assert(files_read("pak_only.txt", &(int64_t){0}) == NULL);

    /* With game.pak: its entries win, missing ones fall back to loose. */
    const char *names[] = { "both.txt", "sub/pak_only.txt" };
    const uint8_t *datas[] = { (const uint8_t *)"packed", (const uint8_t *)"inside" };
    const uint32_t sizes[] = { 6, 6 };
    assert(bast_pak_write(pak, names, datas, sizes, 2) == 0);
    files_shutdown();
    assert(files_init(dir) && files_have_pak());
    assert(files_init(dir)); /* same dir: no-op */
    assert(reads("both.txt", "packed"));
    assert(reads("sub/pak_only.txt", "inside"));
    assert(reads("loose_only.txt", "fallback"));
    assert(files_read("nowhere.txt", &(int64_t){0}) == NULL);
    assert(strstr(files_error(), "nowhere.txt"));

    /* A corrupt game.pak is an error, loose files still served. */
    put(pak, "VPAK\x05");
    files_shutdown();
    assert(!files_init(dir) && strstr(files_error(), "game.pak"));
    assert(!files_have_pak() && reads("both.txt", "loose"));

    files_shutdown();
    remove(pak);
    puts("test_files: OK");
    return 0;
}
