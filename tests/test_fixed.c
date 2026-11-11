/* Fixed-point 16.16 maths (tech.md, Simulation). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "engine/fixed.h"

int main(void)
{
    assert(FIX_ONE == 65536);
    assert(fix_from_int(3) == 3 * 65536);
    assert(fix_from_int(-2) == -2 * 65536);

    /* fix_to_int floors. */
    assert(fix_to_int(fix_from_int(5)) == 5);
    assert(fix_to_int(FIX_ONE + FIX_ONE / 2) == 1);
    assert(fix_to_int(-FIX_ONE / 2) == -1);
    assert(fix_to_int(-FIX_ONE - 1) == -2);
    assert(fix_to_int(-1) == -1);

    /* mul: (a*b)>>16 with arithmetic shift (rounds toward -inf). */
    assert(fix_mul(fix_from_int(3), fix_from_int(4)) == fix_from_int(12));
    assert(fix_mul(fix_from_int(-3), fix_from_int(4)) == fix_from_int(-12));
    assert(fix_mul(fix_from_int(-3), fix_from_int(-4)) == fix_from_int(12));
    assert(fix_mul(FIX_ONE / 2, FIX_ONE / 2) == FIX_ONE / 4);
    assert(fix_mul(1, 1) == 0);
    assert(fix_mul(-1, 1) == -1); /* floor, not truncation */
    assert(fix_mul(-FIX_ONE / 2, FIX_ONE / 2) == -FIX_ONE / 4);
    /* Overflow-safe: intermediate exceeds 32 bits. */
    assert(fix_mul(fix_from_int(1000), fix_from_int(30)) == fix_from_int(30000));
    assert(fix_mul(fix_from_int(-30000), FIX_ONE) == fix_from_int(-30000));
    assert(fix_mul(INT32_MAX, FIX_ONE) == INT32_MAX);

    /* div: (a<<16)/b, truncation toward zero. */
    assert(fix_div(fix_from_int(12), fix_from_int(4)) == fix_from_int(3));
    assert(fix_div(fix_from_int(-12), fix_from_int(4)) == fix_from_int(-3));
    assert(fix_div(fix_from_int(-12), fix_from_int(-4)) == fix_from_int(3));
    assert(fix_div(FIX_ONE, fix_from_int(3)) == 21845);   /* 21845.33 */
    assert(fix_div(-FIX_ONE, fix_from_int(3)) == -21845); /* toward zero */
    assert(fix_div(fix_from_int(2), fix_from_int(3)) == 43690); /* 43690.67 */
    assert(fix_div(fix_from_int(-2), fix_from_int(3)) == -43690);
    assert(fix_div(1, 2) == 32768);
    assert(fix_div(-1, 2) == -32768);
    assert(fix_div(fix_from_int(30000), FIX_ONE / 1) == fix_from_int(30000));
    assert(fix_div(fix_from_int(20000), FIX_ONE) == fix_from_int(20000));
    assert(fix_div(fix_from_int(1), fix_from_int(30000)) == 2); /* 2.18 */

    /* abs */
    assert(fix_abs(-5) == 5);
    assert(fix_abs(5) == 5);
    assert(fix_abs(0) == 0);

    /* sqrt: floor of the true 16.16 root. */
    assert(fix_sqrt(0) == 0);
    assert(fix_sqrt(FIX_ONE) == FIX_ONE);
    assert(fix_sqrt(fix_from_int(4)) == fix_from_int(2));
    assert(fix_sqrt(fix_from_int(9)) == fix_from_int(3));
    assert(fix_sqrt(FIX_ONE / 4) == FIX_ONE / 2);
    assert(fix_sqrt(fix_from_int(2)) == 92681); /* 1.41421 * 65536 = 92681.9 */
    assert(fix_sqrt(INT32_MAX) == 11863283);    /* sqrt(32768)*65536 = 11863283.2 */
    assert(fix_sqrt(-FIX_ONE) == 0);

    assert(isqrt64(0) == 0);
    assert(isqrt64(15) == 3);
    assert(isqrt64(16) == 4);
    assert(isqrt64(UINT64_MAX) == 0xFFFFFFFFu);

    puts("fixed: ok");
    return 0;
}
