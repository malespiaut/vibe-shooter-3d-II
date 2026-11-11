/* Gameplay RNG: xorshift32 (13,17,5), level seed formula (tech.md). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "engine/rng.h"

int main(void)
{
    /* Reference: xorshift32 from state 1 (Marsaglia 2003, shifts 13/17/5). */
    static const uint32_t ref[5] = { 270369u, 67634689u, 2647435461u, 307599695u, 2398689233u };
    rng_t r;
    rng_seed(&r, 1);
    for (int i = 0; i < 5; i++)
        assert(rng_next(&r) == ref[i]);

    /* Same seed, same sequence. */
    rng_t a, b;
    rng_seed(&a, 12345);
    rng_seed(&b, 12345);
    for (int i = 0; i < 1000; i++)
        assert(rng_next(&a) == rng_next(&b));

    /* State 0 is a fixed point of xorshift: seeding with 0 must not stick. */
    rng_seed(&a, 0);
    assert(rng_next(&a) != 0);

    /* Level seed formula. */
    assert(rng_level_seed(1, 0) == (0x9E3779B9u ^ 2654435761u));
    assert(rng_level_seed(3, 0) == (0x9E3779B9u ^ (3u * 2654435761u)));
    assert(rng_level_seed(3, 7) == (0x9E3779B9u ^ (3u * 2654435761u) ^ 7u));
    rng_seed_level(&a, 2, 0);
    assert(a.state == rng_level_seed(2, 0));

    /* rng_range stays in [0, n). Damage = base + rng_range(spread + 1). */
    rng_seed(&a, 99);
    int seen[6] = { 0 };
    for (int i = 0; i < 6000; i++) {
        uint32_t v = rng_range(&a, 6);
        assert(v < 6);
        seen[v]++;
    }
    for (int i = 0; i < 6; i++)
        assert(seen[i] > 800);
    rng_seed(&a, 5);
    rng_seed(&b, 5);
    assert(rng_range(&a, 1000) == rng_next(&b) % 1000);

    /* The UI RNG is a separate instance: drawing from it leaves the game RNG alone. */
    rng_seed(&rng_game, 42);
    rng_seed(&rng_ui, 42);
    uint32_t before = rng_game.state;
    for (int i = 0; i < 10; i++)
        rng_next(&rng_ui);
    assert(rng_game.state == before);

    puts("rng: ok");
    return 0;
}
