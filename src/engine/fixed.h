#ifndef ENGINE_FIXED_H
#define ENGINE_FIXED_H

/* 16.16 fixed point, the only number format in gameplay (tech.md, Simulation). */

#include <assert.h>
#include <stdint.h>

typedef int32_t fix_t;

#define FIX_SHIFT 16
#define FIX_ONE ((fix_t)65536)

/* Right shifts of negative values are implementation-defined in C17; the
 * engine requires arithmetic shifts (gcc and clang provide them). */
_Static_assert((-1 >> 1) == -1, "arithmetic right shift required");
_Static_assert(((int64_t)-1 >> 1) == -1, "arithmetic right shift required");

static inline fix_t fix_from_int(int32_t i)
{
    return (fix_t)((uint32_t)i << FIX_SHIFT);
}

/* Floor: fix_to_int(-0.5) == -1. */
static inline int32_t fix_to_int(fix_t a)
{
    return a >> FIX_SHIFT;
}

/* (a*b) >> 16 through int64, arithmetic shift (rounds toward -infinity). */
static inline fix_t fix_mul(fix_t a, fix_t b)
{
    return (fix_t)(((int64_t)a * b) >> FIX_SHIFT);
}

/* (a << 16) / b through int64, truncation toward zero. b == 0 asserts in
 * debug builds; the result does not fit when |a/b| >= 32768. */
static inline fix_t fix_div(fix_t a, fix_t b)
{
    assert(b != 0);
    return (fix_t)(((int64_t)a * FIX_ONE) / b);
}

/* Undefined for INT32_MIN, as for the C abs(). */
static inline fix_t fix_abs(fix_t a)
{
    return a < 0 ? -a : a;
}

/* floor(sqrt(v)), exact, integer only. */
uint32_t isqrt64(uint64_t v);

/* Square root of a 16.16 value, floored. Negative input returns 0. */
fix_t fix_sqrt(fix_t a);

#endif
