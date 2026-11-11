/* Specimen screen for src/engine/draw2d (issue #44 evidence): all three fonts,
 * remapped colour variants, fills and patches clipped at the screen edges.
 * Usage: draw2d_specimen <assets/gen dir> <out.png> */
#include <stdio.h>

#include "bast.h"
#include "engine/colormap.h"
#include "engine/draw2d.h"
#include "engine/png.h"
#include "engine/video.h"

static uint8_t fb[SCREEN_W * SCREEN_H];

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: draw2d_specimen <assets/gen dir> <out.png>\n");
        return 2;
    }
    const char *dir = argv[1];
    if (!colormap_load(dir)) {
        fprintf(stderr, "draw2d_specimen: %s\n", colormap_error());
        return 1;
    }
    font *small = font_load(dir, "small");
    font *menu = font_load(dir, "menu");
    font *big = font_load(dir, "bigdigits");
    if (!small || !menu || !big) {
        fprintf(stderr, "draw2d_specimen: cannot load fonts from %s/fonts\n", dir);
        return 1;
    }
    uint8_t to_red[256], to_green[256], to_cyan[256], gray_to_gold[256], gray_to_steel[256];
    remap_ramp(to_red, "gold", "red");
    remap_ramp(to_green, "gold", "green");
    remap_ramp(to_cyan, "gold", "cyan");
    remap_ramp(gray_to_gold, "gray", "gold");
    remap_ramp(gray_to_steel, "gray", "steel");

    int sand0, sandn;
    ramp_find("sand", &sand0, &sandn);
    draw_fill(fb, 0, 0, SCREEN_W - 1, SCREEN_H - 1, (uint8_t)(sand0 + 3));
    draw_fill(fb, 4, 8, 315, 205, (uint8_t)(sand0 + 1));

    /* Patch clipped at the top-left corner: bigdigits sheet, cyan remap. */
    draw_patch_remap(fb, big->tex, -6, -9, to_cyan);

    /* Menu font: full glyph set, plus remapped variants. */
    draw_text(fb, menu, 8, 10, " !\"#$%&'()*+,-./0123456789:;<=>?", NULL);
    draw_text(fb, menu, 8, 24, "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_", NULL);
    draw_text(fb, menu, 8, 38, "lower case -> UPPER", NULL);
    draw_text(fb, menu, 8, 52, "WARNING", to_red);
    draw_text(fb, menu, 88, 52, "GREEN", to_green);
    draw_text(fb, menu, 148, 52, "CYAN", to_cyan);
    draw_text_right(fb, menu, 311, 52, "RIGHT", NULL);

    /* Small font: full glyph set, two lines, plus remaps. */
    draw_text(fb, small, 8, 70,
              " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNO\n"
              "PQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~",
              NULL);
    draw_text(fb, small, 8, 90, "PICKED UP A TUNA TIN.\nsecond line, gold remap", gray_to_gold);
    draw_text(fb, small, 160, 90, "steel remap: C 120\nY  12  S   4", gray_to_steel);

    /* Big digits: full set and HUD-style right alignment with remaps; a red
     * marker column sits at x_right + 1. */
    draw_text(fb, big, 8, 110, "0123456789%/-", NULL);
    draw_text(fb, big, 170, 110, "1A2", NULL); /* 'A' missing -> blank */
    draw_text_right(fb, big, 103, 130, "100%", NULL);
    draw_text_right(fb, big, 227, 130, "25%", to_red);
    draw_text_right(fb, big, 311, 130, "-7", to_green);
    draw_fill(fb, 104, 130, 104, 145, 245); /* fb_red */
    draw_fill(fb, 228, 130, 228, 145, 245);

    /* Patches clipped at the left and right edges. */
    draw_patch(fb, big->tex, -40, 152);
    draw_patch_remap(fb, big->tex, 200, 152, to_red);
    draw_text(fb, small, 8, 178, "clipped patches: top-left, left, right,\nbottom-right", NULL);

    /* Status-bar strip (y 208-239) with HUD numbers in their hud.md boxes. */
    draw_fill(fb, 0, 208, 319, 239, (uint8_t)(sand0 + 5));
    draw_text_right(fb, big, 103, 210, "100%", NULL);
    draw_text_right(fb, small, 103, 226, "200", NULL);
    draw_text_right(fb, big, 227, 210, "50", NULL);
    draw_text(fb, small, 122, 231, "1234567", NULL);

    /* Patch clipped at the bottom-right corner, drawn last. */
    draw_patch(fb, menu->tex, 236, 196);

    font_free(small);
    font_free(menu);
    font_free(big);
    if (!png_write_indexed(argv[2], fb, SCREEN_W, SCREEN_H, colormap_palette())) {
        fprintf(stderr, "draw2d_specimen: cannot write %s\n", argv[2]);
        return 1;
    }
    printf("draw2d_specimen: wrote %s\n", argv[2]);
    return 0;
}
