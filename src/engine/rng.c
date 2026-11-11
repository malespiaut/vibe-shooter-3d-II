#include "engine/rng.h"

#include <assert.h>

#define RNG_ZERO_REPLACEMENT 0x9E3779B9u

rng_t rng_game = { RNG_ZERO_REPLACEMENT };
rng_t rng_ui = { RNG_ZERO_REPLACEMENT };

void rng_seed(rng_t *r, uint32_t seed)
{
    r->state = seed ? seed : RNG_ZERO_REPLACEMENT;
}

uint32_t rng_level_seed(int level, uint32_t demo_seed)
{
    return 0x9E3779B9u ^ ((uint32_t)level * 2654435761u) ^ demo_seed;
}

void rng_seed_level(rng_t *r, int level, uint32_t demo_seed)
{
    rng_seed(r, rng_level_seed(level, demo_seed));
}

uint32_t rng_next(rng_t *r)
{
    uint32_t x = r->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->state = x;
    return x;
}

uint32_t rng_range(rng_t *r, uint32_t n)
{
    assert(n > 0);
    return rng_next(r) % n;
}
