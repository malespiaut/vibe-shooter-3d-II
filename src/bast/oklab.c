/* sRGB <-> Oklab and nearest-colour search. The only Oklab code in the repo.
 *
 * The conversions mirror the original Python palette generator operation by
 * operation (pow() for every power, same evaluation order, no FMA
 * contraction: see CMakeLists.txt) so catlantis.gpl stays bit-reproducible. */
#include "bast_internal.h"

#include <math.h>

static double to_linear(uint8_t v)
{
    double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static double to_srgb(double c)
{
    c = c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
    return c * 255.0;
}

static double cbrt_signed(double x)
{
    return x >= 0 ? pow(x, 1.0 / 3.0) : -pow(-x, 1.0 / 3.0);
}

void bast_srgb_to_oklab(uint8_t r8, uint8_t g8, uint8_t b8, double lab[3])
{
    double r = to_linear(r8), g = to_linear(g8), b = to_linear(b8);
    double l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b;
    double m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b;
    double s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b;
    l = cbrt_signed(l);
    m = cbrt_signed(m);
    s = cbrt_signed(s);
    lab[0] = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    lab[1] = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    lab[2] = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
}

/* Oklab -> linear sRGB. */
static void oklab_to_linear(double L, double a, double b, double lin[3])
{
    double l = pow(L + 0.3963377774 * a + 0.2158037573 * b, 3.0);
    double m = pow(L - 0.1055613458 * a - 0.0638541728 * b, 3.0);
    double s = pow(L - 0.0894841775 * a - 1.2914855480 * b, 3.0);
    lin[0] = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    lin[1] = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    lin[2] = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
}

static int in_gamut(double L, double a, double b)
{
    const double eps = 1e-6;
    double lin[3];
    oklab_to_linear(L, a, b, lin);
    for (int i = 0; i < 3; i++)
        if (!(-eps <= lin[i] && lin[i] <= 1 + eps))
            return 0;
    return 1;
}

void bast_oklab_to_srgb(double L, double a, double b, uint8_t rgb[3])
{
    L = L < 0.0 ? 0.0 : L > 1.0 ? 1.0 : L;
    if (!in_gamut(L, a, b)) {
        double lo = 0.0, hi = 1.0;
        for (int i = 0; i < 40; i++) {
            double mid = (lo + hi) / 2;
            if (in_gamut(L, a * mid, b * mid))
                lo = mid;
            else
                hi = mid;
        }
        a *= lo;
        b *= lo;
    }
    double lin[3];
    oklab_to_linear(L, a, b, lin);
    for (int i = 0; i < 3; i++) {
        double v = lin[i] >= 0 ? nearbyint(to_srgb(lin[i])) : 0.0; /* half to even */
        rgb[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
}

int bast_palette_nearest_ex(const bast_palette *pal, uint8_t r, uint8_t g, uint8_t b,
                            unsigned allow)
{
    unsigned skip = BAST_PAL_RESERVED;
    if (!(allow & BAST_ALLOW_FULLBRIGHT))
        skip |= BAST_PAL_FULLBRIGHT;
    if (!(allow & BAST_ALLOW_TRANSPARENT))
        skip |= BAST_PAL_TRANSPARENT;
    double lab[3];
    bast_srgb_to_oklab(r, g, b, lab);
    int best = -1;
    double best_d = 0.0;
    for (int i = 0; i < 256; i++) {
        if (pal->flags[i] & skip)
            continue;
        double dl = lab[0] - pal->lab[i][0];
        double da = lab[1] - pal->lab[i][1];
        double db = lab[2] - pal->lab[i][2];
        double d = dl * dl + da * da + db * db;
        if (best < 0 || d < best_d) { /* strict: ties keep the lowest index */
            best = i;
            best_d = d;
        }
    }
    return best < 0 ? 0 : best;
}

int bast_palette_nearest(const bast_palette *pal, uint8_t r, uint8_t g, uint8_t b)
{
    return bast_palette_nearest_ex(pal, r, g, b, 0);
}
