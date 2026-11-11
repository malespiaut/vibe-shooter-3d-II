#include "bast_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *bast_read_file(const char *path, int64_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        bast_set_error("%s: %s", path, strerror(errno));
        return NULL;
    }
    uint8_t *buf = NULL;
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0)
        n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) {
        bast_set_error("%s: cannot get size", path);
        fclose(f);
        return NULL;
    }
    buf = malloc(n > 0 ? (size_t)n : 1);
    if (!buf) {
        bast_set_error("%s: out of memory", path);
        fclose(f);
        return NULL;
    }
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        bast_set_error("%s: read error", path);
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = n;
    return buf;
}

bast_palette *bast_palette_from_memory(const uint8_t rgb[768], const uint8_t flags[256])
{
    bast_palette *pal = malloc(sizeof *pal);
    if (!pal) {
        bast_set_error("out of memory");
        return NULL;
    }
    memcpy(pal->rgb, rgb, 768);
    memcpy(pal->flags, flags, 256);
    for (int i = 0; i < 256; i++)
        bast_srgb_to_oklab(pal->rgb[i][0], pal->rgb[i][1], pal->rgb[i][2], pal->lab[i]);
    return pal;
}

bast_palette *bast_palette_load(const char *path)
{
    /* palette_flags.bin lives next to palette.bin. */
    const char *slash = strrchr(path, '/');
    size_t dirlen = slash ? (size_t)(slash - path + 1) : 0;
    static const char flags_name[] = "palette_flags.bin";
    char *flags_path = malloc(dirlen + sizeof flags_name);
    if (!flags_path) {
        bast_set_error("out of memory");
        return NULL;
    }
    memcpy(flags_path, path, dirlen);
    memcpy(flags_path + dirlen, flags_name, sizeof flags_name);

    bast_palette *pal = NULL;
    int64_t nrgb = 0, nflags = 0;
    uint8_t *rgb = bast_read_file(path, &nrgb);
    uint8_t *fl = rgb ? bast_read_file(flags_path, &nflags) : NULL;
    if (rgb && fl) {
        if (nrgb != 768)
            bast_set_error("%s: %lld bytes, need 768", path, (long long)nrgb);
        else if (nflags != 256)
            bast_set_error("%s: %lld bytes, need 256", flags_path, (long long)nflags);
        else
            pal = bast_palette_from_memory(rgb, fl);
    }
    free(rgb);
    free(fl);
    free(flags_path);
    return pal;
}

void bast_palette_free(bast_palette *pal)
{
    free(pal);
}
