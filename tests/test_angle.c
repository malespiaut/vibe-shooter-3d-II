/* Angles, trig tables, angle_of, fix_dist (tech.md, Simulation).
 * Floats here only compute the reference truth; the engine has none. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "engine/angle.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int ang_diff(angle_t a, angle_t b)
{
    int d = (int)(uint16_t)(a - b);
    return d > 32768 ? 65536 - d : d;
}

int main(void)
{
    assert(ANG90 == 16384 && ANG180 == 32768 && ANG270 == 49152);
    assert(FINEANGLES == 4096);

    /* Exact cardinal values. */
    assert(fine_sin(0) == 0);
    assert(fine_cos(0) == FIX_ONE);
    assert(fine_sin(ANG90) == FIX_ONE);
    assert(fine_cos(ANG90) == 0);
    assert(fine_sin(ANG180) == 0);
    assert(fine_cos(ANG180) == -FIX_ONE);
    assert(fine_sin(ANG270) == -FIX_ONE);
    assert(fine_cos(ANG270) == 0);
    /* Indexed by angle >> 4. */
    assert(fine_sin(ANG90 + 15) == FIX_ONE);
    assert(fine_sin(8192) == 46341); /* sin 45 = 0.7071068 * 65536 = 46340.95 */
    assert(fine_cos(8192) == 46341);

    /* angle_of on 360 sampled directions at several radii: within one
     * fine table step (16 angle units) of the truth. */
    int worst = 0;
    const double radii[] = { 0.01, 0.5, 1.0, 7.3, 100.0, 20000.0 };
    for (int r = 0; r < 6; r++) {
        for (int deg = 0; deg < 360; deg++) {
            double rad = deg * (M_PI / 180.0);
            fix_t dx = (fix_t)lround(cos(rad) * radii[r] * 65536.0);
            fix_t dy = (fix_t)lround(sin(rad) * radii[r] * 65536.0);
            double t = atan2((double)dy, (double)dx);
            if (t < 0)
                t += 2 * M_PI;
            angle_t truth = (angle_t)(uint32_t)lround(t * 65536.0 / (2 * M_PI));
            int d = ang_diff(angle_of(dx, dy), truth);
            if (d > worst)
                worst = d;
            if (d > 16)
                printf("angle_of fail: deg=%d r=%g got=%u truth=%u\n", deg, radii[r],
                       angle_of(dx, dy), truth);
            assert(d <= 16);
        }
    }
    printf("angle_of worst error: %d angle units (limit 16)\n", worst);
    assert(angle_of(FIX_ONE, 0) == 0);
    assert(angle_of(0, FIX_ONE) == ANG90);
    assert(angle_of(-FIX_ONE, 0) == ANG180);
    assert(angle_of(0, -FIX_ONE) == ANG270);
    assert(angle_of(FIX_ONE, FIX_ONE) == 8192);
    assert(angle_of(0, 0) == 0);

    /* fix_dist */
    assert(fix_dist(fix_from_int(3), fix_from_int(4)) == fix_from_int(5));
    assert(fix_dist(fix_from_int(-3), fix_from_int(-4)) == fix_from_int(5));
    assert(fix_dist(0, 0) == 0);
    assert(fix_dist(FIX_ONE, FIX_ONE) == 92681);
    assert(fix_dist(fix_from_int(64), fix_from_int(64)) == 5931641); /* 90.5097 */
    assert(fix_dist(INT32_MIN, INT32_MIN) == INT32_MAX);              /* saturates */

    puts("angle: ok");
    return 0;
}
