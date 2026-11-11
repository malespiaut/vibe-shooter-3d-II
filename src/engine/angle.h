#ifndef ENGINE_ANGLE_H
#define ENGINE_ANGLE_H

/* Binary angles (tech.md, Simulation): 65536 = full turn, 0 = east (+x),
 * counter-clockwise, y grows north. Wraparound is free on uint16_t. */

#include <stdint.h>

#include "engine/fixed.h"

typedef uint16_t angle_t;

#define ANG45 ((angle_t)8192)
#define ANG90 ((angle_t)16384)
#define ANG180 ((angle_t)32768)
#define ANG270 ((angle_t)49152)

#define FINEANGLES 4096
#define ANGLETOFINESHIFT 4 /* fine index = angle >> 4 */

/* Table lookups, exact at the four cardinal angles. */
fix_t fine_sin(angle_t a);
fix_t fine_cos(angle_t a);

/* Integer atan2: direction of the vector (dx, dy). angle_of(0, 0) == 0.
 * Accurate to a few angle units (well under one fine step of 16). */
angle_t angle_of(fix_t dx, fix_t dy);

/* Euclidean length of (dx, dy), floored, exact. Saturates at INT32_MAX. */
fix_t fix_dist(fix_t dx, fix_t dy);

#endif
