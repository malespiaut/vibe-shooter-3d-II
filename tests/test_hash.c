/* 64-bit FNV-1a (tech.md, State hash). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "engine/hash.h"

int main(void)
{
    hash_t h;
    hash_init(&h);
    assert(h.value == 0xcbf29ce484222325ull); /* FNV-1a 64 of "" */

    hash_bytes(&h, "a", 1);
    assert(h.value == 0xaf63dc4c8601ec8cull); /* FNV-1a 64 of "a" */

    hash_init(&h);
    hash_bytes(&h, "foobar", 6);
    assert(h.value == 0x85944171f73967e8ull);

    /* Incremental == one shot. */
    hash_t a, b;
    hash_init(&a);
    hash_bytes(&a, "foo", 3);
    hash_bytes(&a, "bar", 3);
    assert(a.value == h.value);

    /* u32/i32 hash 4 little-endian bytes. */
    const uint8_t le[4] = { 0x78, 0x56, 0x34, 0x12 };
    hash_init(&a);
    hash_u32(&a, 0x12345678u);
    hash_init(&b);
    hash_bytes(&b, le, 4);
    assert(a.value == b.value);

    const uint8_t m1[4] = { 0xff, 0xff, 0xff, 0xff };
    hash_init(&a);
    hash_i32(&a, -1);
    hash_init(&b);
    hash_bytes(&b, m1, 4);
    assert(a.value == b.value);

    puts("hash: ok");
    return 0;
}
