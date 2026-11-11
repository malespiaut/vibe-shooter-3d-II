#include "engine/files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bast.h"

static char g_dir[512];
static int g_init;
static bast_pak *g_pak;
static char g_err[768] = "files not initialised";

const char *files_error(void)
{
    return g_err;
}

void files_shutdown(void)
{
    bast_pak_close(g_pak);
    g_pak = NULL;
    g_dir[0] = '\0';
    g_init = 0;
}

int files_init(const char *dir)
{
    if (!dir)
        dir = "assets/gen";
    if (g_init && strcmp(dir, g_dir) == 0)
        return 1;
    files_shutdown();
    snprintf(g_dir, sizeof g_dir, "%s", dir);
    g_init = 1;
    char path[600];
    snprintf(path, sizeof path, "%s/game.pak", g_dir);
    FILE *f = fopen(path, "rb");
    if (!f)
        return 1; /* no archive: loose files only */
    fclose(f);
    g_pak = bast_pak_open(path);
    if (!g_pak) {
        snprintf(g_err, sizeof g_err, "%s", bast_error());
        return 0;
    }
    return 1;
}

int files_have_pak(void)
{
    return g_pak != NULL;
}

uint8_t *files_read(const char *name, int64_t *size)
{
    if (!g_init)
        files_init(NULL);
    uint32_t n = 0;
    const uint8_t *p = bast_pak_find(g_pak, name, &n);
    if (p) {
        uint8_t *buf = malloc(n ? n : 1);
        if (!buf) {
            snprintf(g_err, sizeof g_err, "%s: out of memory", name);
            return NULL;
        }
        memcpy(buf, p, n);
        *size = n;
        return buf;
    }
    char path[600];
    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(g_err, sizeof g_err, "%s: not in %s and no loose file %s", name,
                 g_pak ? "game.pak" : "an archive (no game.pak)", path);
        return NULL;
    }
    long len = -1;
    if (fseek(f, 0, SEEK_END) == 0)
        len = ftell(f);
    uint8_t *buf = NULL;
    if (len >= 0 && fseek(f, 0, SEEK_SET) == 0)
        buf = malloc(len > 0 ? (size_t)len : 1);
    if (!buf || (len > 0 && fread(buf, 1, (size_t)len, f) != (size_t)len)) {
        snprintf(g_err, sizeof g_err, "%s: cannot read", path);
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = len;
    return buf;
}
