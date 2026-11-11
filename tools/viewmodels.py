#!/usr/bin/env python3
"""Build the first-person view-model frames (docs/design/weapons.md, View-model
animation frames).

Sources: assets/src/viewmodels/legend.txt plus one vm<N>_<frame>.pix per frame,
in the tools/hud.py .pix format (hand-drawn pixel grids, 1 px drop shadow
bottom-right baked in, no outline). Each frame is screen-pixel art drawn
bottom-centred over the 320x208 3D view, so the TEX origin is bottom-centre
(width/2, height) like world sprites.

Output: <out>/vm<N>_<frame>.tex (libbast) and .png previews (indexed, game palette).

Usage:
  python3 tools/viewmodels.py [--src DIR] [-o OUT] [--clean]
          [--backdrop FRAME.png --composite DIR] [--sheet FILE.png]

--composite writes, for every frame, <DIR>/<name>.png: the frame drawn at its
in-game position (bottom-centre of the 320x208 view) over the first 320x208
rows of the backdrop (an indexed 320x208 or 320x240 game frame).
"""
import argparse
import glob
import os
import re
import shutil
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402
import hud  # noqa: E402
import palette  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SRC = os.path.join(ROOT, "assets", "src", "viewmodels")
DEFAULT_OUT = os.path.join(ROOT, "assets", "gen", "viewmodels")
VIEW_W, VIEW_H = 320, 208
NAME_RE = re.compile(r"^vm[1-7]_[a-z]+[0-9]*$")
T = hud.T


def build(src, pal):
    legend = hud.load_legend(os.path.join(src, "legend.txt"), pal)
    frames = {}
    for path in sorted(glob.glob(os.path.join(src, "*.pix"))):
        name, img = hud.load_pix(path, legend, pal)
        if not NAME_RE.match(name):
            raise hud.HudError("%s: frame name %r is not vm<N>_<frame>" % (path, name))
        if name != os.path.splitext(os.path.basename(path))[0]:
            raise hud.HudError("%s: name %s does not match the file name" % (path, name))
        if name in frames:
            raise hud.HudError("%s: duplicate name %s" % (path, name))
        frames[name] = img
    return frames


def to_tex(img):
    t = img.to_tex()
    return bast.Tex(t.width, t.height, t.pixels, (img.w // 2, img.h), t.flags)


def composite(img, backdrop):
    """Frame at its in-game position over a 320x208 view (Image8)."""
    v = hud.Image8(VIEW_W, VIEW_H, 0)
    v.px[:] = backdrop
    v.blit(img, VIEW_W // 2 - img.w // 2, VIEW_H - img.h)
    return v


def load_backdrop(path):
    im = Image.open(path)
    if im.mode != "P" or im.size[0] != VIEW_W or im.size[1] < VIEW_H:
        raise hud.HudError("%s: need an indexed %dx%d (or taller) game frame" % (path, VIEW_W, VIEW_H))
    return bytearray(im.crop((0, 0, VIEW_W, VIEW_H)).tobytes())


def sheet(frames, pal):
    """Every frame side by side, one weapon per row, on a dark stone backdrop."""
    rows = {}
    for k in sorted(frames):
        rows.setdefault(k.split("_")[0], []).append(k)
    order = ["idle", "swipe0", "swipe1", "swipe2", "charge0", "charge1", "fire0", "fire1", "pump0", "pump1",
             "reload0", "roar0", "roar1", "roar2"]
    pad = 6
    w = max(sum(frames[k].w + pad for k in r) for r in rows.values()) + pad
    h = sum(max(frames[k].h for k in r) + pad for r in rows.values()) + pad
    s = hud.Image8(w, h, pal("steel_04"))
    y = pad
    for wpn in sorted(rows):
        r = sorted(rows[wpn], key=lambda k: order.index(k.split("_")[1]) if k.split("_")[1] in order else 99)
        rh = max(frames[k].h for k in r)
        x = pad
        for k in r:
            s.blit(frames[k], x, y + rh - frames[k].h)
            x += frames[k].w + pad
        y += rh + pad
    return s


def main(argv=None):
    ap = argparse.ArgumentParser(description="view-model frames")
    ap.add_argument("--src", default=DEFAULT_SRC)
    ap.add_argument("-o", "--out", default=DEFAULT_OUT)
    ap.add_argument("--clean", action="store_true", help="delete the output directory first")
    ap.add_argument("--backdrop", help="indexed 320x208+ game frame for --composite")
    ap.add_argument("--composite", help="directory for in-position composites")
    ap.add_argument("--sheet", help="all frames side by side (PNG, 1x)")
    args = ap.parse_args(argv)
    try:
        pal = hud.Pal()
        frames = build(args.src, pal)
        if args.clean and os.path.isdir(args.out):
            shutil.rmtree(args.out)
        os.makedirs(args.out, exist_ok=True)
        for name, img in sorted(frames.items()):
            to_tex(img).save(os.path.join(args.out, name + ".tex"))
            img.to_pil(pal).save(os.path.join(args.out, name + ".png"), transparency=T)
        if args.composite:
            if not args.backdrop:
                raise hud.HudError("--composite needs --backdrop")
            bd = load_backdrop(args.backdrop)
            os.makedirs(args.composite, exist_ok=True)
            for name, img in sorted(frames.items()):
                composite(img, bd).to_pil(pal).save(os.path.join(args.composite, name + ".png"))
        if args.sheet:
            sheet(frames, pal).to_pil(pal).save(args.sheet)
    except (OSError, ValueError, KeyError, hud.HudError, bast.BastError,
            palette.PaletteError) as e:
        print("viewmodels: %s" % e, file=sys.stderr)
        return 1
    print("viewmodels: %d frames in %s" % (len(frames), args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
