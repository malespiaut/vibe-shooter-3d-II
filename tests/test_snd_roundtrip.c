/* libbast SND: header layout, save/load round trip, bad input rejected.
 * docs/design/tech.md, "Audio files". */
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

static void write_bytes(const char *path, const uint8_t *b, size_t n)
{
    FILE *f = fopen(path, "wb");
    assert(f && fwrite(b, 1, n, f) == n);
    fclose(f);
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

static void test_roundtrip(void)
{
    enum { N = 300 };
    bast_snd *s = bast_snd_new(N);
    assert(s && s->rate == BAST_SND_RATE && s->count == N);
    for (int i = 0; i < N; i++)
        s->samples[i] = (int8_t)((i * 37) % 256 - 128); /* covers -128..127 */
    const char *p = tmp_path("bast_test.snd");
    assert(bast_snd_save(s, p) == 0);

    long n;
    uint8_t *b = slurp(p, &n);
    assert(n == BAST_SND_HEADER_SIZE + N);
    static const uint8_t hdr[12] = {'S', 'N', 'D', '1', 0x22, 0x56, 0, 0, 0x2c, 0x01, 0, 0};
    assert(memcmp(b, hdr, 12) == 0); /* 22050 = 0x5622, 300 = 0x12c, LE */
    assert(memcmp(b + 12, s->samples, N) == 0);

    bast_snd *r = bast_snd_load(p);
    assert(r && r->rate == 22050 && r->count == N);
    assert(memcmp(r->samples, s->samples, N) == 0);
    bast_snd_free(r);

    /* From memory gives the same result. */
    r = bast_snd_from_memory(b, n);
    assert(r && r->count == N && memcmp(r->samples, s->samples, N) == 0);
    bast_snd_free(r);
    free(b);
    bast_snd_free(s);
}

static void test_rejects(void)
{
    const char *p = tmp_path("bast_bad.snd");
    uint8_t bad[16] = {'S', 'N', 'D', '2', 0x22, 0x56, 0, 0, 4, 0, 0, 0, 1, 2, 3, 4};
    write_bytes(p, bad, sizeof bad);
    assert(bast_snd_load(p) == NULL);
    assert(strstr(bast_error(), "SND1"));

    memcpy(bad, "SND1", 4);
    bad[8] = 5; /* count says 5, file holds 4 samples */
    write_bytes(p, bad, sizeof bad);
    assert(bast_snd_load(p) == NULL);

    bad[8] = 4;
    bad[4] = 0x44; /* rate 22084: only 22050 is valid */
    write_bytes(p, bad, sizeof bad);
    assert(bast_snd_load(p) == NULL);

    bad[4] = 0x22;
    write_bytes(p, bad, sizeof bad);
    bast_snd *ok = bast_snd_load(p);
    assert(ok && ok->count == 4 && ok->samples[3] == 4);
    bast_snd_free(ok);

    assert(bast_snd_from_memory(bad, 11) == NULL); /* truncated header */
    assert(bast_snd_load(tmp_path("does_not_exist.snd")) == NULL);
    assert(bast_snd_new(-1) == NULL);
}

int main(void)
{
    test_roundtrip();
    test_rejects();
    puts("snd_roundtrip: ok");
    return 0;
}
