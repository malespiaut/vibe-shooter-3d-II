#include "engine/fixed.h"

uint32_t isqrt64(uint64_t v)
{
    uint64_t res = 0;
    uint64_t bit = (uint64_t)1 << 62;

    while (bit > v)
        bit >>= 2;
    while (bit != 0) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)res;
}

fix_t fix_sqrt(fix_t a)
{
    if (a <= 0)
        return 0;
    /* sqrt(a / 2^16) * 2^16 == sqrt(a * 2^16). Max result ~2^23.5, fits. */
    return (fix_t)isqrt64((uint64_t)a << FIX_SHIFT);
}
