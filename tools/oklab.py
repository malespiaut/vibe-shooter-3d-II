"""sRGB <-> Oklab conversion (Bjorn Ottosson's reference matrices).

Tool-side only: used by the palette generator. Runtime code never touches
floating point colour maths.
"""


def _to_linear(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def _to_srgb(c):
    c = 12.92 * c if c <= 0.0031308 else 1.055 * (c ** (1 / 2.4)) - 0.055
    return c * 255.0


def _cbrt(x):
    return x ** (1 / 3) if x >= 0 else -((-x) ** (1 / 3))


def srgb_to_oklab(rgb):
    r, g, b = (_to_linear(v) for v in rgb)
    l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b
    m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b
    s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b
    l, m, s = _cbrt(l), _cbrt(m), _cbrt(s)
    return (
        0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
        1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
        0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s,
    )


def oklab_to_srgb_float(lab):
    """Return unclamped sRGB floats (0..255 when in gamut)."""
    L, a, b = lab
    l = L + 0.3963377774 * a + 0.2158037573 * b
    m = L - 0.1055613458 * a - 0.0638541728 * b
    s = L - 0.0894841775 * a - 1.2914855480 * b
    l, m, s = l ** 3, m ** 3, s ** 3
    r = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s
    g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s
    bb = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s
    return tuple(_to_srgb(max(0.0, v)) if v >= 0 else -1.0 for v in (r, g, bb))


def in_gamut(lab, eps=1e-6):
    L, a, b = lab
    l = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3
    m = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3
    s = (L - 0.0894841775 * a - 1.2914855480 * b) ** 3
    lin = (
        4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
        -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
        -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s,
    )
    return all(-eps <= v <= 1 + eps for v in lin)


def oklab_to_srgb(lab):
    """Gamut-map by reducing chroma (keeps L and hue), then round to 0..255."""
    L, a, b = lab
    L = min(max(L, 0.0), 1.0)
    if not in_gamut((L, a, b)):
        lo, hi = 0.0, 1.0
        for _ in range(40):
            mid = (lo + hi) / 2
            if in_gamut((L, a * mid, b * mid)):
                lo = mid
            else:
                hi = mid
        a, b = a * lo, b * lo
    return tuple(min(255, max(0, round(v))) for v in oklab_to_srgb_float((L, a, b)))


def hex_to_rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
