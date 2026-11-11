/* Mock-up of the text panels through src/engine/draw2d (issue #156 evidence).
 *
 *   panel_mockup <assets/gen> papyrus <text file> <out.png>
 *       papyrus_panel + the text in the menu font, word-wrapped to the
 *       300x168 text box (x 10-309, y 36-203, the #102 decision): story.md
 *       line breaks are soft, blank lines are paragraph breaks. Fails (exit 1)
 *       if the wrapped text needs more than 12 lines.
 *   panel_mockup <assets/gen> tally <level name> <out.png>
 *       tally_panel + the level name and a full tally (KILLS, ITEMS, SECRETS,
 *       TIME/PAR) in the menu font, at the panel's header and body recesses.
 *
 * Text colours: the menu font as shipped (gold) on the tally's dark recesses;
 * on the papyrus it is drawn as ink, the font's gold ramp remapped to brown
 * (remap_ramp, draw2d.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bast.h"
#include "engine/colormap.h"
#include "engine/draw2d.h"
#include "engine/png.h"
#include "engine/video.h"

enum { BOX_X = 10, BOX_Y = 36, BOX_W = 300, BOX_H = 168, MAX_LINES = 12 };

static uint8_t fb[SCREEN_W * SCREEN_H];

static bast_tex *load_panel(const char *dir, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/ui/%s.tex", dir, name);
    bast_tex *t = bast_tex_load(path);
    if (!t)
        fprintf(stderr, "panel_mockup: cannot load %s\n", path);
    return t;
}

/* Greedy word wrap of `src` to `cols` characters into `out` ('\n' separated).
 * Single newlines are soft (joined with a space); a blank line ends a
 * paragraph and is kept as one empty line. Returns the number of lines. */
static int wrap(const char *src, int cols, char *out, size_t cap)
{
    size_t o = 0;
    int lines = 0, col = 0;
    const char *p = src;
    while (*p) {
        if (p[0] == '\n' && p[1] == '\n') { /* paragraph break */
            while (*p == '\n')
                p++;
            if (col > 0 && o + 2 < cap) {
                out[o++] = '\n';
                out[o++] = '\n';
                lines += 2;
                col = 0;
            }
            continue;
        }
        if (*p == ' ' || *p == '\n') {
            p++;
            continue;
        }
        const char *w = p;
        while (*p && *p != ' ' && *p != '\n')
            p++;
        int len = (int)(p - w);
        if (col > 0 && col + 1 + len > cols) {
            out[o++] = '\n';
            lines++;
            col = 0;
        } else if (col > 0) {
            out[o++] = ' ';
            col++;
        }
        if (o + (size_t)len + 3 >= cap)
            break;
        memcpy(out + o, w, (size_t)len);
        o += (size_t)len;
        col += len;
    }
    out[o] = '\0';
    return col > 0 ? lines + 1 : lines;
}

int main(int argc, char **argv)
{
    if (argc != 5 || (strcmp(argv[2], "papyrus") && strcmp(argv[2], "tally"))) {
        fprintf(stderr, "usage: panel_mockup <assets/gen> papyrus <text file> <out.png>\n"
                        "       panel_mockup <assets/gen> tally <level name> <out.png>\n");
        return 2;
    }
    const char *dir = argv[1];
    if (!colormap_load(dir)) {
        fprintf(stderr, "panel_mockup: %s\n", colormap_error());
        return 1;
    }
    font *menu = font_load(dir, "menu");
    if (!menu) {
        fprintf(stderr, "panel_mockup: cannot load the menu font from %s/fonts\n", dir);
        return 1;
    }
    int rc = 0;
    if (!strcmp(argv[2], "papyrus")) {
        bast_tex *panel = load_panel(dir, "papyrus_panel");
        FILE *f = fopen(argv[3], "rb");
        if (!panel || !f) {
            fprintf(stderr, "panel_mockup: cannot read %s\n", argv[3]);
            return 1;
        }
        static char text[4096], wrapped[8192];
        size_t n = fread(text, 1, sizeof text - 1, f);
        fclose(f);
        text[n] = '\0';
        while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == ' '))
            text[--n] = '\0';
        int cols = BOX_W / menu->cell_w;
        int lines = wrap(text, cols, wrapped, sizeof wrapped);
        uint8_t ink[256];
        remap_ramp(ink, "gold", "brown");
        draw_patch(fb, panel, 0, 0);
        draw_text(fb, menu, BOX_X, BOX_Y, wrapped, ink);
        int w = text_width(menu, wrapped);
        printf("papyrus: %d lines (max %d), widest %d px (box %d), height %d px (box %d)\n",
               lines, MAX_LINES, w, BOX_W, lines * menu->cell_h, BOX_H);
        if (lines > MAX_LINES || w > BOX_W)
            rc = 1;
        bast_tex_free(panel);
    } else {
        bast_tex *panel = load_panel(dir, "tally_panel");
        if (!panel)
            return 1;
        draw_patch(fb, panel, 0, 0);
        /* header recess x 28-291, y 22-45; body recess x 28-291, y 72-183 */
        const char *name = argv[3];
        int nw = text_width(menu, name);
        draw_text(fb, menu, 160 - nw / 2, 27, name, NULL);
        /* Body: labels at x=44, values right-aligned to x=279, 24 px pitch;
         * the last line splits TIME (value to x=163) and PAR (x=192). */
        static const char *const label[3] = {"KILLS", "ITEMS", "SECRETS"};
        static const char *const value[3] = {"100%", "100%", "3/3"};
        for (int i = 0; i < 3; i++) {
            draw_text(fb, menu, 44, 84 + 24 * i, label[i], NULL);
            draw_text_right(fb, menu, 279, 84 + 24 * i, value[i], NULL);
        }
        draw_text(fb, menu, 44, 156, "TIME", NULL);
        draw_text_right(fb, menu, 163, 156, "4:12", NULL);
        draw_text(fb, menu, 192, 156, "PAR", NULL);
        draw_text_right(fb, menu, 279, 156, "4:00", NULL);
        printf("tally: level name %d px (header recess interior 260 px)\n", nw);
        if (nw > 260)
            rc = 1;
        bast_tex_free(panel);
    }
    if (!png_write_indexed(argv[4], fb, SCREEN_W, SCREEN_H, colormap_palette())) {
        fprintf(stderr, "panel_mockup: cannot write %s\n", argv[4]);
        return 1;
    }
    font_free(menu);
    return rc;
}
