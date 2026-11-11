#include "engine/hash.h"

#define FNV64_OFFSET 0xcbf29ce484222325ull
#define FNV64_PRIME 0x100000001b3ull

void hash_init(hash_t *h)
{
    h->value = FNV64_OFFSET;
}

void hash_bytes(hash_t *h, const void *data, size_t len)
{
    const uint8_t *p = data;
    uint64_t v = h->value;
    for (size_t i = 0; i < len; i++) {
        v ^= p[i];
        v *= FNV64_PRIME;
    }
    h->value = v;
}

void hash_u32(hash_t *h, uint32_t v)
{
    const uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    hash_bytes(h, b, 4);
}

void hash_i32(hash_t *h, int32_t v)
{
    hash_u32(h, (uint32_t)v);
}
