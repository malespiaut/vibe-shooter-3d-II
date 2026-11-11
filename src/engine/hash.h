#ifndef ENGINE_HASH_H
#define ENGINE_HASH_H

/* Incremental 64-bit FNV-1a, used for the game state hash (tech.md).
 * Integers are hashed as little-endian bytes on every host. */

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t value;
} hash_t;

void hash_init(hash_t *h);
void hash_bytes(hash_t *h, const void *data, size_t len);
void hash_u32(hash_t *h, uint32_t v);
void hash_i32(hash_t *h, int32_t v);

#endif
