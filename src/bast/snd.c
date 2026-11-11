/* SND1: 12-byte little-endian header + signed 8-bit mono PCM.
 * docs/design/tech.md, "Audio files". */
#include "bast_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bast_snd *bast_snd_new(int64_t count)
{
    if (count < 0 || count > (int64_t)BAST_SND_MAX_SAMPLES) {
        bast_set_error("bad SND sample count %lld (0..%u)", (long long)count,
                       BAST_SND_MAX_SAMPLES);
        return NULL;
    }
    bast_snd *s = calloc(1, sizeof *s);
    int8_t *pcm = calloc((size_t)count + 1, 1);
    if (!s || !pcm) {
        free(s);
        free(pcm);
        bast_set_error("out of memory");
        return NULL;
    }
    s->rate = BAST_SND_RATE;
    s->count = (uint32_t)count;
    s->samples = pcm;
    return s;
}

void bast_snd_free(bast_snd *snd)
{
    if (snd) {
        free(snd->samples);
        free(snd);
    }
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i) & 0xff);
}

bast_snd *bast_snd_from_memory(const uint8_t *d, int64_t size)
{
    if (size < BAST_SND_HEADER_SIZE || memcmp(d, "SND1", 4) != 0) {
        bast_set_error("not a SND1 file");
        return NULL;
    }
    uint32_t rate = rd32(d + 4), count = rd32(d + 8);
    if (rate != BAST_SND_RATE) {
        bast_set_error("SND rate %u, only %u is supported", rate, BAST_SND_RATE);
        return NULL;
    }
    if (size != BAST_SND_HEADER_SIZE + (int64_t)count) {
        bast_set_error("SND is %lld bytes, %u samples need %lld", (long long)size, count,
                       (long long)(BAST_SND_HEADER_SIZE + (int64_t)count));
        return NULL;
    }
    bast_snd *s = bast_snd_new(count);
    if (!s)
        return NULL;
    memcpy(s->samples, d + BAST_SND_HEADER_SIZE, count);
    return s;
}

bast_snd *bast_snd_load(const char *path)
{
    int64_t size = 0;
    uint8_t *d = bast_read_file(path, &size);
    if (!d)
        return NULL;
    bast_snd *s = bast_snd_from_memory(d, size);
    if (!s) {
        char msg[200];
        snprintf(msg, sizeof msg, "%s", bast_error());
        bast_set_error("%s: %s", path, msg);
    }
    free(d);
    return s;
}

int bast_snd_save(const bast_snd *s, const char *path)
{
    if (!s || !s->samples || s->rate != BAST_SND_RATE || s->count > BAST_SND_MAX_SAMPLES) {
        bast_set_error("%s: invalid sound", path);
        return -1;
    }
    uint8_t hdr[BAST_SND_HEADER_SIZE] = {'S', 'N', 'D', '1'};
    wr32(hdr + 4, s->rate);
    wr32(hdr + 8, s->count);
    FILE *f = fopen(path, "wb");
    if (!f) {
        bast_set_error("%s: %s", path, strerror(errno));
        return -1;
    }
    int ok = fwrite(hdr, 1, sizeof hdr, f) == sizeof hdr &&
             fwrite(s->samples, 1, s->count, f) == s->count;
    if (fclose(f) != 0)
        ok = 0;
    if (!ok) {
        bast_set_error("%s: write error", path);
        return -1;
    }
    return 0;
}
