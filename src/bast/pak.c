/* VPAK archive: magic "VPAK", uint32 count, count x (char name[56],
 * uint32 offset, uint32 size), then data. Little-endian.
 * docs/design/tech.md, "Archives". */
#include "bast_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct bast_pak {
    uint8_t *data;   /* the whole file */
    int64_t size;
    uint32_t count;
};

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)(v >> 8 & 0xff);
    p[2] = (uint8_t)(v >> 16 & 0xff);
    p[3] = (uint8_t)(v >> 24 & 0xff);
}

static const uint8_t *entry(const bast_pak *pak, uint32_t i)
{
    return pak->data + BAST_PAK_HEADER_SIZE + (size_t)i * BAST_PAK_ENTRY_SIZE;
}

int bast_pak_write(const char *path, const char *const *names, const uint8_t *const *datas,
                   const uint32_t *sizes, int n)
{
    if (n < 0 || (uint32_t)n > BAST_PAK_MAX_ENTRIES) {
        bast_set_error("%s: bad entry count %d", path, n);
        return -1;
    }
    uint64_t off = BAST_PAK_HEADER_SIZE + (uint64_t)n * BAST_PAK_ENTRY_SIZE;
    for (int i = 0; i < n; i++) {
        size_t len = names[i] ? strlen(names[i]) : 0;
        if (len < 1 || len > BAST_PAK_NAME_MAX) {
            bast_set_error("%s: entry %d: name must be 1..%d chars", path, i, BAST_PAK_NAME_MAX);
            return -1;
        }
        for (int j = 0; j < i; j++) {
            if (strcmp(names[i], names[j]) == 0) {
                bast_set_error("%s: duplicate name \"%s\"", path, names[i]);
                return -1;
            }
        }
        if (sizes[i] && !datas[i]) {
            bast_set_error("%s: entry \"%s\" has no data", path, names[i]);
            return -1;
        }
        off += sizes[i];
        if (off > 0xffffffffu) {
            bast_set_error("%s: archive over 4 GiB", path);
            return -1;
        }
    }
    size_t dir_size = BAST_PAK_HEADER_SIZE + (size_t)n * BAST_PAK_ENTRY_SIZE;
    uint8_t *dir = calloc(dir_size, 1);
    if (!dir) {
        bast_set_error("out of memory");
        return -1;
    }
    memcpy(dir, "VPAK", 4);
    wr32(dir + 4, (uint32_t)n);
    uint32_t o = (uint32_t)dir_size;
    for (int i = 0; i < n; i++) {
        uint8_t *e = dir + BAST_PAK_HEADER_SIZE + (size_t)i * BAST_PAK_ENTRY_SIZE;
        memcpy(e, names[i], strlen(names[i])); /* rest stays NUL */
        wr32(e + BAST_PAK_NAME_SIZE, o);
        wr32(e + BAST_PAK_NAME_SIZE + 4, sizes[i]);
        o += sizes[i];
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        bast_set_error("%s: %s", path, strerror(errno));
        free(dir);
        return -1;
    }
    int ok = fwrite(dir, 1, dir_size, f) == dir_size;
    for (int i = 0; ok && i < n; i++)
        ok = sizes[i] == 0 || fwrite(datas[i], 1, sizes[i], f) == sizes[i];
    if (fclose(f) != 0)
        ok = 0;
    free(dir);
    if (!ok) {
        bast_set_error("%s: write failed", path);
        return -1;
    }
    return 0;
}

bast_pak *bast_pak_from_memory(uint8_t *data, int64_t size)
{
    if (size < BAST_PAK_HEADER_SIZE || memcmp(data, "VPAK", 4) != 0) {
        bast_set_error("not a VPAK archive");
        return NULL;
    }
    uint32_t count = rd32(data + 4);
    if (count > BAST_PAK_MAX_ENTRIES ||
        (int64_t)BAST_PAK_HEADER_SIZE + (int64_t)count * BAST_PAK_ENTRY_SIZE > size) {
        bast_set_error("VPAK directory (%u entries) does not fit", (unsigned)count);
        return NULL;
    }
    bast_pak tmp = { data, size, count };
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = entry(&tmp, i);
        if (e[0] == 0 || memchr(e, 0, BAST_PAK_NAME_SIZE) == NULL) {
            bast_set_error("VPAK entry %u: bad name", (unsigned)i);
            return NULL;
        }
        uint64_t off = rd32(e + BAST_PAK_NAME_SIZE), len = rd32(e + BAST_PAK_NAME_SIZE + 4);
        if (off + len > (uint64_t)size) {
            bast_set_error("VPAK entry \"%s\": data past end of file", (const char *)e);
            return NULL;
        }
        for (uint32_t j = 0; j < i; j++) {
            if (strcmp((const char *)e, (const char *)entry(&tmp, j)) == 0) {
                bast_set_error("VPAK: duplicate name \"%s\"", (const char *)e);
                return NULL;
            }
        }
    }
    bast_pak *pak = malloc(sizeof *pak);
    if (!pak) {
        bast_set_error("out of memory");
        return NULL;
    }
    *pak = tmp;
    return pak;
}

bast_pak *bast_pak_open(const char *path)
{
    int64_t size = 0;
    uint8_t *d = bast_read_file(path, &size);
    if (!d)
        return NULL;
    bast_pak *pak = bast_pak_from_memory(d, size);
    if (!pak) {
        char msg[200];
        snprintf(msg, sizeof msg, "%s", bast_error());
        bast_set_error("%s: %s", path, msg);
        free(d);
    }
    return pak;
}

void bast_pak_close(bast_pak *pak)
{
    if (pak) {
        free(pak->data);
        free(pak);
    }
}

int bast_pak_count(const bast_pak *pak)
{
    return pak ? (int)pak->count : 0;
}

const char *bast_pak_name(const bast_pak *pak, int i)
{
    if (!pak || i < 0 || (uint32_t)i >= pak->count)
        return NULL;
    return (const char *)entry(pak, (uint32_t)i);
}

const uint8_t *bast_pak_find(const bast_pak *pak, const char *name, uint32_t *size)
{
    for (uint32_t i = 0; pak && name && i < pak->count; i++) {
        const uint8_t *e = entry(pak, i);
        if (strcmp((const char *)e, name) == 0) {
            if (size)
                *size = rd32(e + BAST_PAK_NAME_SIZE + 4);
            return pak->data + rd32(e + BAST_PAK_NAME_SIZE);
        }
    }
    bast_set_error("%s: not in archive", name ? name : "(null)");
    return NULL;
}
