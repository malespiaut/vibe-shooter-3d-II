#ifndef ENGINE_RNG_H
#define ENGINE_RNG_H

/* xorshift32 (tech.md, Simulation). rng_game is the one gameplay RNG; its
 * state is part of the state hash. rng_ui serves rendering, audio and menus
 * and never touches game state. */

#include <stdint.h>

typedef struct {
    uint32_t state;
} rng_t;

extern rng_t rng_game;
extern rng_t rng_ui;

/* Seed 0 is a fixed point of xorshift and is replaced by 0x9E3779B9. */
void rng_seed(rng_t *r, uint32_t seed);

/* level_seed = 0x9E3779B9 ^ (level * 2654435761u), then XOR the demo's
 * `seed` line (0 for normal play). */
uint32_t rng_level_seed(int level, uint32_t demo_seed);
void rng_seed_level(rng_t *r, int level, uint32_t demo_seed);

/* Next output (the new state). */
uint32_t rng_next(rng_t *r);

/* rng_next(r) % n, n > 0. Damage = base + rng_range(r, spread + 1). */
uint32_t rng_range(rng_t *r, uint32_t n);

#endif
