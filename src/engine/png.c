#include "engine/png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    static int ready;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = 1;
    }
    for (size_t i = 0; i < n; i++)
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

uint32_t png_crc32(const uint8_t *data, size_t len)
{
    return crc_update(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
}

uint32_t png_adler32(const uint8_t *data, size_t len)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

static uint8_t *put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
    return p + 4;
}

/* Writes a chunk (length, type, data, CRC over type+data) at p. */
static uint8_t *put_chunk(uint8_t *p, const char type[4], const uint8_t *data, size_t len)
{
    p = put_be32(p, (uint32_t)len);
    uint8_t *crc_start = p;
    memcpy(p, type, 4);
    p += 4;
    if (len)
        memcpy(p, data, len);
    p += len;
    return put_be32(p, png_crc32(crc_start, len + 4));
}

#define STORED_MAX 65535u

uint8_t *png_encode_indexed(const uint8_t *pixels, int w, int h, const uint8_t *pal768,
                            size_t *size)
{
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384)
        return NULL;
    /* Raw scanlines: filter byte 0 + w indices per row. */
    const size_t raw_len = (size_t)h * ((size_t)w + 1);
    const size_t blocks = raw_len / STORED_MAX + 1;
    /* zlib: 2 header + blocks * 5 + raw + 4 adler. */
    const size_t z_len = 2 + blocks * 5 + raw_len + 4;
    const size_t total = 8 + (12 + 13) + (12 + 768) + (12 + z_len) + 12;

    uint8_t *raw = malloc(raw_len);
    uint8_t *z = malloc(z_len);
    uint8_t *out = malloc(total);
    if (!raw || !z || !out) {
        free(raw);
        free(z);
        free(out);
        return NULL;
    }
    for (int y = 0; y < h; y++) {
        uint8_t *row = raw + (size_t)y * ((size_t)w + 1);
        row[0] = 0;
        memcpy(row + 1, pixels + (size_t)y * (size_t)w, (size_t)w);
    }

    uint8_t *q = z;
    *q++ = 0x78; /* CMF: deflate, 32K window */
    *q++ = 0x01; /* FLG: no dict, fastest; (0x7801 % 31 == 0) */
    size_t off = 0;
    for (size_t b = 0; b < blocks; b++) {
        size_t n = raw_len - off;
        if (n > STORED_MAX)
            n = STORED_MAX;
        *q++ = (uint8_t)(b + 1 == blocks ? 1 : 0); /* BFINAL, BTYPE=00 */
        *q++ = (uint8_t)(n & 0xFF);
        *q++ = (uint8_t)(n >> 8);
        *q++ = (uint8_t)(~n & 0xFF);
        *q++ = (uint8_t)((~n >> 8) & 0xFF);
        memcpy(q, raw + off, n);
        q += n;
        off += n;
    }
    q = put_be32(q, png_adler32(raw, raw_len));

    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    uint8_t ihdr[13];
    put_be32(ihdr, (uint32_t)w);
    put_be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 3;  /* colour type: indexed */
    ihdr[10] = 0; /* compression */
    ihdr[11] = 0; /* filter */
    ihdr[12] = 0; /* no interlace */

    uint8_t *p = out;
    memcpy(p, sig, 8);
    p += 8;
    p = put_chunk(p, "IHDR", ihdr, 13);
    p = put_chunk(p, "PLTE", pal768, 768);
    p = put_chunk(p, "IDAT", z, (size_t)(q - z));
    p = put_chunk(p, "IEND", NULL, 0);
    free(raw);
    free(z);
    *size = (size_t)(p - out);
    return out;
}

int png_write_indexed(const char *path, const uint8_t *pixels, int w, int h,
                      const uint8_t *pal768)
{
    size_t n;
    uint8_t *buf = png_encode_indexed(pixels, w, h, pal768, &n);
    if (!buf)
        return 0;
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(buf, 1, n, f) == n;
    if (f && fclose(f) != 0)
        ok = 0;
    free(buf);
    return ok;
}
