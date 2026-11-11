"""Pre-render a Blender model into 8-direction palette sprites (GAME.md 4.1).

Run inside Blender:

    blender -b <file.blend> --python tools/blender_render.py -- \
        --sprite <name> --canvas WxH --out <dir> [--preview <dir>] [--raw <dir>]

Reads `sprite.json` next to the .blend:

    {"object": "Mutt",          # optional: object that gets the actions
     "canvas": [48, 64],        # read by tools/build.d/60-sprites.sh, not here
     "actions": {"MuttWalk": {"anim": "walk", "frames": [1, 5, 9, 13]}}}

and writes `<out>/<sprite>_<anim>_d<dir>_f<frame>.tex` for every action, all
8 directions and every listed frame (f0 = first frame of the list), origin
bottom-centre (docs/design/tech.md, Sprite naming and TEX).

Model convention: the thing faces -Y (Blender "front" view), stands on z = 0
with its origin at (0, 0, 0). If "object" is omitted, the first object with
animation data gets the actions.

Camera and lights (docs/design/art-bible.md), all built here; cameras and
lights already in the .blend are removed:
- orthographic, looking horizontally (eye-level, no pitch), 64 px per Blender
  unit, rendered at exactly the canvas size; z = 0 is the bottom pixel edge,
  x = 0 the canvas centre, so the origin is (W/2, H).
- d0 camera at -Y, looking at the thing's face; dir k = camera moved 45*k deg
  counter-clockwise (seen from above) around the Z axis. The rig (camera and
  both lamps) rotates as one, so the light is the same for every direction.
- key: sun lamp, rotation X=45 Z=-45 relative to the camera (from the
  upper-left-front), strength 1, hard shadow. Fill: sun at 20% strength from
  the opposite side, rotation X=135 Z=45 (lower-right-front), no shadow, so
  faces turned away from the key and key shadows stay readable instead of
  black. Both lamps face the camera's side: no rim light. No world light.
- key and fill are rendered separately (float EXR) and added in linear
  light: with 1 sample Cycles would pick one lamp per pixel at random.
- Cycles on the CPU, 1 sample with a 0.01 px filter (one ray through each
  pixel centre: no anti-aliasing, alpha is 0 or 1), no bounces, Standard view
  transform. Workbench cannot use a sun lamp and EEVEE needs a GPU context in
  the headless build containers, so Cycles is the deterministic choice.

Post-pass on the RGBA render, before quantization (art-bible.md):
1. Any alpha strictly between 0 and 255 is an error (no AA mush).
2. Edge contrast: unsharp mask over opaque pixels only (3x3 mean of the
   opaque neighbours, amount 0.6), so shading edges inside the silhouette
   separate and the background never bleeds in.
3. Selective outline: every opaque pixel whose right or bottom neighbour is
   transparent (or outside the canvas) becomes the darkest entry (`<ramp>_00`)
   of its own ramp. Lit (left/top) edges get no outline. Fullbright pixels keep
   their colour.
Then tools/quantize.py quantize_rgba() writes the TEX (libbast Oklab nearest).
"""
import argparse
import json
import math
import os
import shutil
import sys

import numpy as np

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import bast  # noqa: E402
import palette as gpl  # noqa: E402
import quantize  # noqa: E402

PX_PER_UNIT = 64
DIRS = 8
EDGE_AMOUNT = 0.6
KEY_ROT = (45.0, 0.0, -45.0)    # degrees, relative to the camera rig
FILL_ROT = (135.0, 0.0, 45.0)      # lower-right-front: opposite the key
FILL_STRENGTH = 0.2


# ---------------------------------------------------------------- post-pass
# Pure numpy/libbast: importable without Blender (used by the tests).

def ramp_darkest_table(entries):
    """index -> index of the darkest entry of its ramp (`<ramp>_00`).

    Fullbright and transparent entries map to themselves. `white` has a
    single entry and belongs to the gray family, so it maps to gray_00.
    """
    by_name = {name: i for i, (_, name) in enumerate(entries)}
    fl = gpl.flags(entries)
    table = list(range(256))
    for i, (_, name) in enumerate(entries):
        if fl[i] & (gpl.FLAG_FULLBRIGHT | gpl.FLAG_TRANSPARENT):
            continue
        ramp = name.rsplit("_", 1)[0] if "_" in name else name
        if name == "white":
            ramp = "gray"
        table[i] = by_name.get(ramp + "_00", i)
    return table


def edge_contrast(rgb, opaque, amount=EDGE_AMOUNT):
    """Unsharp mask over opaque pixels only. rgb float HxWx3 (0..255)."""
    h, w = opaque.shape
    pad_rgb = np.zeros((h + 2, w + 2, 3))
    pad_op = np.zeros((h + 2, w + 2))
    pad_rgb[1:-1, 1:-1] = rgb * opaque[..., None]
    pad_op[1:-1, 1:-1] = opaque
    acc = np.zeros_like(rgb)
    cnt = np.zeros(opaque.shape)
    for dy in range(3):
        for dx in range(3):
            acc += pad_rgb[dy:dy + h, dx:dx + w]
            cnt += pad_op[dy:dy + h, dx:dx + w]
    mean = acc / np.maximum(cnt, 1)[..., None]
    out = rgb + amount * (rgb - mean)
    return np.where(opaque[..., None], np.clip(out, 0, 255), rgb)


def outline_mask(opaque):
    """Opaque pixels whose right or bottom neighbour is transparent/outside."""
    right = np.zeros_like(opaque)
    right[:, :-1] = opaque[:, 1:]
    below = np.zeros_like(opaque)
    below[:-1, :] = opaque[1:, :]
    return opaque & ~(right & below)


def postprocess(rgba, w, h, pal, darkest):
    """Row-major RGBA8 bytes -> TEX file bytes (edge contrast, outline, quantize)."""
    img = np.frombuffer(rgba, dtype=np.uint8).reshape(h, w, 4)
    alpha = img[..., 3]
    partial = int(np.count_nonzero((alpha > 0) & (alpha < 255)))
    if partial:
        raise ValueError("%d pixel(s) with partial alpha (anti-aliasing)" % partial)
    opaque = alpha == 255
    rgb = edge_contrast(img[..., :3].astype(np.float64), opaque)
    out = np.zeros((h, w, 4), dtype=np.uint8)
    out[..., :3] = np.rint(rgb).astype(np.uint8)
    out[..., 3] = np.where(opaque, 255, 0)
    # Classify through libbast, then repaint the outline pixels with the exact
    # colour of their ramp's darkest entry (it quantizes to itself).
    t = quantize.quantize_tex(out.tobytes(), w, h, palette=pal)
    colors = pal.colors
    edge = outline_mask(opaque)
    for y, x in zip(*np.nonzero(edge)):
        idx = t.get(int(x), int(y))
        out[y, x, :3] = colors[darkest[idx]]
    return quantize.quantize_rgba(out.tobytes(), w, h, origin=quantize.BOTTOM_CENTRE,
                                  palette=pal)


# ------------------------------------------------------------------ Blender

def parse_args(argv):
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    ap = argparse.ArgumentParser(prog="blender_render.py")
    ap.add_argument("--sprite", required=True)
    ap.add_argument("--canvas", required=True, help="WxH in px, e.g. 48x64")
    ap.add_argument("--out", required=True)
    ap.add_argument("--preview", help="also write indexed PNG previews here")
    ap.add_argument("--raw", help="also keep the raw RGBA renders here")
    a = ap.parse_args(argv)
    try:
        w, h = (int(v) for v in a.canvas.lower().split("x"))
    except ValueError:
        ap.error("--canvas must be WxH")
    if not (1 <= w <= 1024 and 1 <= h <= 1024) or w % 2:
        ap.error("--canvas: width must be even (bottom-centre origin), sizes 1..1024")
    a.w, a.h = w, h
    return a


def setup_scene(bpy, w, h):
    scene = bpy.context.scene
    for o in list(scene.objects):
        if o.type in ("CAMERA", "LIGHT"):
            bpy.data.objects.remove(o, do_unlink=True)

    r = scene.render
    r.engine = "CYCLES"
    r.resolution_x, r.resolution_y, r.resolution_percentage = w, h, 100
    r.pixel_aspect_x = r.pixel_aspect_y = 1.0
    r.film_transparent = True
    r.use_compositing = False
    r.use_sequencer = False
    r.image_settings.file_format = "OPEN_EXR"
    r.image_settings.color_mode = "RGBA"
    r.image_settings.color_depth = "32"
    r.dither_intensity = 0.0
    cy = scene.cycles
    cy.device = "CPU"
    cy.samples = 1
    cy.use_adaptive_sampling = False
    cy.use_denoising = False
    cy.filter_width = 0.01
    cy.pixel_filter_type = "BOX"
    cy.seed = 0
    cy.max_bounces = 0
    cy.diffuse_bounces = cy.glossy_bounces = cy.transmission_bounces = 0
    cy.caustics_reflective = cy.caustics_refractive = False
    vs = scene.view_settings
    vs.view_transform = "Standard"
    vs.look = "None"
    vs.exposure, vs.gamma = 0.0, 1.0
    scene.display_settings.display_device = "sRGB"

    world = scene.world or bpy.data.worlds.new("World")
    scene.world = world
    world.color = (0, 0, 0)
    if world.node_tree:
        for n in world.node_tree.nodes:
            if n.type == "BACKGROUND":
                n.inputs["Strength"].default_value = 0.0

    lamps = []
    rig = bpy.data.objects.new("vs_rig", None)
    scene.collection.objects.link(rig)
    cam_data = bpy.data.cameras.new("vs_cam")
    cam_data.type = "ORTHO"
    cam_data.sensor_fit = "AUTO"
    cam_data.ortho_scale = max(w, h) / PX_PER_UNIT
    cam_data.clip_start, cam_data.clip_end = 0.01, 100.0
    cam = bpy.data.objects.new("vs_cam", cam_data)
    cam.parent = rig
    # d0: at -Y looking +Y; image centre at height h/2 px so z = 0 is the
    # bottom edge.
    cam.location = (0.0, -20.0, h / 2 / PX_PER_UNIT)
    cam.rotation_euler = (math.radians(90), 0.0, 0.0)
    scene.collection.objects.link(cam)
    scene.camera = cam
    for name, rot, strength in (("vs_key", KEY_ROT, 1.0), ("vs_fill", FILL_ROT, FILL_STRENGTH)):
        ld = bpy.data.lights.new(name, "SUN")
        ld.energy = strength
        ld.angle = 0.0
        if hasattr(ld, "specular_factor"):
            ld.specular_factor = 0.0
        if name == "vs_fill":
            ld.use_shadow = False
        lo = bpy.data.objects.new(name, ld)
        lo.parent = rig
        lo.rotation_euler = tuple(math.radians(v) for v in rot)
        scene.collection.objects.link(lo)
        lamps.append(lo)
    return scene, rig, lamps


def assign_action(obj, action):
    if obj.animation_data is None:
        obj.animation_data_create()
    ad = obj.animation_data
    ad.action = action
    if hasattr(ad, "action_slot") and ad.action_slot is None and len(action.slots):
        ad.action_slot = action.slots[0]


def find_object(bpy, cfg):
    name = cfg.get("object")
    if name:
        obj = bpy.data.objects.get(name)
        if obj is None:
            raise SystemExit("blender_render: object %r not in the .blend" % name)
        return obj
    for o in bpy.context.scene.objects:
        if o.animation_data is not None:
            return o
    raise SystemExit("blender_render: no animated object; name it in sprite.json \"object\"")


def linear_to_srgb8(c):
    c = np.clip(c, 0.0, 1.0)
    s = np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1 / 2.4) - 0.055)
    return np.rint(s * 255).astype(np.uint8)


def render_linear(bpy, scene, path):
    """Render to a float EXR, return linear HxWx4 (top row first)."""
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    img = bpy.data.images.load(path)
    w, h = img.size
    px = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    bpy.data.images.remove(img)
    return px.reshape(h, w, 4)[::-1]


def render_rgba(bpy, scene, lamps, stem, tmp):
    """Key and fill rendered alone and added in linear light.

    With 1 sample and two lamps Cycles picks one lamp per pixel at random
    (speckles); one lamp per render makes every pixel exact. Returns
    row-major RGBA8 bytes (sRGB, Standard transform).
    """
    acc = None
    for lamp in lamps:
        for other in lamps:
            other.hide_render = other is not lamp
        lin = render_linear(bpy, scene, os.path.join(tmp, "%s_%s.exr" % (stem, lamp.name)))
        acc = lin if acc is None else acc + np.concatenate(
            [lin[..., :3], np.zeros_like(lin[..., 3:])], axis=2)
    out = np.empty(acc.shape, dtype=np.uint8)
    alpha = acc[..., 3:4]
    out[..., :3] = linear_to_srgb8(acc[..., :3] / np.maximum(alpha, 1e-6))
    out[..., 3] = np.rint(np.clip(acc[..., 3], 0, 1) * 255).astype(np.uint8)
    out[..., :3] *= (out[..., 3:4] > 0)
    return out.tobytes()


def main():
    import tempfile

    import bpy

    a = parse_args(sys.argv)
    blend = bpy.data.filepath
    if not blend:
        raise SystemExit("blender_render: run as blender -b <file.blend> --python ...")
    with open(os.path.join(os.path.dirname(blend), "sprite.json")) as f:
        cfg = json.load(f)
    actions = cfg.get("actions") or {}
    if not actions:
        raise SystemExit("blender_render: sprite.json lists no actions")

    pal = bast.load_palette()
    darkest = ramp_darkest_table(gpl.load_gpl())
    scene, rig, lamps = setup_scene(bpy, a.w, a.h)
    obj = find_object(bpy, cfg)
    os.makedirs(a.out, exist_ok=True)
    for d in (a.preview, a.raw):
        if d:
            os.makedirs(d, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="vs_render_")
    count = 0
    for act_name in sorted(actions):
        spec = actions[act_name]
        action = bpy.data.actions.get(act_name)
        if action is None:
            raise SystemExit("blender_render: action %r not in the .blend" % act_name)
        anim, frames = spec["anim"], spec["frames"]
        assign_action(obj, action)
        for fi, frame in enumerate(frames):
            scene.frame_set(int(frame))
            for d in range(DIRS):
                rig.rotation_euler = (0.0, 0.0, math.radians(45 * d))
                stem = "%s_%s_d%d_f%d" % (a.sprite, anim, d, fi)
                rgba = render_rgba(bpy, scene, lamps, stem, tmp)
                if a.raw:
                    from PIL import Image
                    Image.frombytes("RGBA", (a.w, a.h), rgba).save(
                        os.path.join(a.raw, stem + ".png"))
                try:
                    data = postprocess(rgba, a.w, a.h, pal, darkest)
                except ValueError as e:
                    raise SystemExit("blender_render: %s: %s" % (stem, e))
                dst = os.path.join(a.out, stem + ".tex")
                with open(dst, "wb") as f:
                    f.write(data)
                if a.preview:
                    bast.Tex.load(dst).to_image(pal).save(os.path.join(a.preview, stem + ".png"))
                count += 1
    shutil.rmtree(tmp, ignore_errors=True)
    print("blender_render: %d sprite(s) -> %s (%dx%d, origin %d,%d)"
          % (count, a.out, a.w, a.h, a.w // 2, a.h))


if __name__ == "__main__":
    main()
