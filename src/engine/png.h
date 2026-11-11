#ifndef ENGINE_PNG_H
#define ENGINE_PNG_H

/* Minimal indexed PNG writer for frame dumps (roadmap.md Verification
 * tooling contract). Colour type 3, bit depth 8, one PLTE chunk, image data
 * in stored (uncompressed) deflate blocks; CRC32 and Adler-32 computed here.
 * No dependency beyond libc. */

#include <stddef.h>
#include <stdint.h>

/* Encodes a w x h image of palette indices (row-major, w*h bytes) with a
 * 768-byte RGB palette. Returns a malloc'd buffer (free() it) and its size
 * in *size, or NULL on allocation failure / bad dimensions. */
uint8_t *png_encode_indexed(const uint8_t *pixels, int w, int h, const uint8_t *pal768,
                            size_t *size);

/* Encodes and writes to `path`. Returns 1 on success, 0 on failure. */
int png_write_indexed(const char *path, const uint8_t *pixels, int w, int h,
                      const uint8_t *pal768);

uint32_t png_crc32(const uint8_t *data, size_t len);
uint32_t png_adler32(const uint8_t *data, size_t len);

#endif
