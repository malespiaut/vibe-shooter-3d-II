"""Pre-render a Blender model into 8-direction palette sprites (GAME.md 4.1).

Run inside Blender:

    blender -b <file.blend> --python tools/blender_render.py -- \
        --sprite <name> --canvas WxH --out <dir> [--preview <dir>] [--raw <dir>]

Reads `sprite.json` next to the .blend. Schema (all keys but "actions" and
"ramps" optional):

    {"object": "Mutt",          # object that gets the actions; default: the
                                # first object with animation data
     "canvas": [48, 64],        # W, H in px; read by tools/build.d/60-sprites.sh,
                                # which passes it as --canvas (W must be even)
     "ramps": {"MuttFur": "fur", "MuttCoat": "steel", "MuttEye": "fb_red"},
                                # Blender material name -> palette ramp (the
                                # `<ramp>` of `<ramp>_NN` entries) or one
                                # fullbright entry (`fb_*`). Every material
                                # seen by the camera must be listed. At most 3
                                # distinct ramps plus any number of `fb_*`
                                # (art-bible.md, colour budget).
     "actions": {"MuttWalk": {"anim": "walk", "frames": [1, 5, 9, 13]}}}
                                # Blender action -> anim name and the frames
                                # rendered as f0, f1, ...

and writes `<out>/<sprite>_<anim>_d<dir>_f<frame>.tex` for every action, all
8 directions and every listed frame (f0 = first frame of the list), origin
bottom-centre (docs/design/tech.md, Sprite naming and TEX).

Model convention: the thing faces -Y (Blender "front" view), stands on z = 0
with its origin at (0, 0, 0).

Camera and lights (docs/design/art-bible.md), all built here; cameras and
lights already in the .blend are removed:
- orthographic, looking horizontally (eye-level, no pitch), 64 px per Blender
  unit, rendered at exactly the canvas size; z = 0 is the bottom pixel edge,
  x = 0 the canvas centre, so the origin is (W/2, H).
- d0 camera at -Y, looking at the thing's face; dir k = camera moved 45*k deg
  counter-clockwise (seen from above) around the Z axis. The rig (camera and
  both lamps) rotates as one, so the light is the same for every direction.
- key: sun lamp, rotation X=45 Z=-45 relative to the camera (from the
  upper-left-front), strength pi, hard shadow. A Lambert face square to a sun
  of strength pi returns exactly its base colour, so lit faces reach the top
  of their ramp. Fill: sun at 20% (0.2*pi) along the camera axis (from the
  viewer), no shadow: every face the camera sees gets some light, so key
  shadows and faces turned away from the key stay readable instead of black,
  and a light from the viewer can never rim-light. No world light.
- key and fill are rendered separately (float EXR) and added in linear
  light: with 1 sample Cycles would pick one lamp per pixel at random. A third
  render swaps every material for a flat emission that encodes its id: the
  material pass used to pick each pixel's ramp.
- Cycles on the CPU, 1 sample with a 0.01 px filter (one ray through each
  pixel centre: no anti-aliasing, alpha is 0 or 1), no bounces, Standard view
  transform. Workbench cannot use a sun lamp and EEVEE needs a GPU context in
  the headless build containers, so Cycles is the deterministic choice.

Post-pass on the RGBA render (art-bible.md), then the TEX is written:
1. Any alpha strictly between 0 and 255 is an error (no AA mush).
2. Edge contrast: unsharp mask over opaque pixels only (3x3 mean of the
   opaque neighbours, amount 0.6), so shading edges inside the silhouette
   separate and the background never bleeds in.
3. Ramp-constrained quantization: each opaque pixel becomes the Oklab-nearest
   (libbast bast_srgb_to_oklab) entry of its material's ramp, never another
   ramp, black or `shadow_*`. Inside the silhouette the candidates are
   `<ramp>_01` and up: `_00` is kept for the outline so it always reads as an
   edge. A pixel whose material has no ramp is an error.
4. Selective outline: every opaque pixel whose right or bottom neighbour is
   transparent (or outside the canvas) becomes `<ramp>_00` of its own
   material's ramp. Lit (left/top) edges get no outline. Fullbright materials
   keep their single entry.
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
KEY_STRENGTH = math.pi           # Lambert face square to the key = base colour
FILL_ROT = (90.0, 0.0, 0.0)      # along the camera axis: from the viewer
FILL_STRENGTH = 0.2 * math.pi
MAX_RAMPS = 3                    # art-bible.md: 3 ramps plus fullbright accents
ID_SCALE = 64.0                  # material id k renders as emission k / 64


# ---------------------------------------------------------------- post-pass
# Pure numpy/libbast: importable without Blender (used by the tests).

def ramp_entries(entries, ramp):
    """Palette indices of ramp `ramp`, dark to light (`<ramp>_00` first).

    A fullbright entry name (`fb_*`) is a one-entry ramp. Raises ValueError
    for anything else that is not a paintable ramp.
    """
    fl = gpl.flags(entries)
    names = [n for _, n in entries]
    if ramp in names and fl[names.index(ramp)] & gpl.FLAG_FULLBRIGHT:
        return [names.index(ramp)]
    if ramp in ("reserved", "shadow"):
        raise ValueError("ramp %r is not for painting" % ramp)
    idx = [i for i, n in enumerate(names)
           if n.startswith(ramp + "_") and n[len(ramp) + 1:].isdigit()
           and not fl[i] & (gpl.FLAG_FULLBRIGHT | gpl.FLAG_TRANSPARENT | gpl.FLAG_RESERVED)]
    if not idx:
        raise ValueError("unknown ramp %r" % ramp)
    idx.sort(key=lambda i: int(names[i][len(ramp) + 1:]))
    return idx


def resolve_ramps(entries, ramps):
    """sprite.json "ramps" {material: ramp} -> {material: [indices]}.

    Enforces the colour budget: at most MAX_RAMPS distinct ramps plus any
    fullbright (`fb_*`) entries.
    """
    if not ramps:
        raise ValueError('sprite.json has no "ramps" {material: ramp} map')
    fl = gpl.flags(entries)
    out = {m: ramp_entries(entries, r) for m, r in ramps.items()}
    used = sorted({r for m, r in ramps.items()
                   if not (len(out[m]) == 1 and fl[out[m][0]] & gpl.FLAG_FULLBRIGHT)})
    if len(used) > MAX_RAMPS:
        raise ValueError("%d ramps %s, the budget is %d plus fb_*"
                         % (len(used), ", ".join(used), MAX_RAMPS))
    return out


class RampQuantizer:
    """Oklab-nearest within one ramp, with libbast's colour maths."""

    def __init__(self, pal):
        self.colors = pal.colors
        self.lab = [bast.srgb_to_oklab(c) for c in self.colors]
        self.cache = {}

    def nearest(self, rgb, candidates):
        key = (rgb, tuple(candidates))
        hit = self.cache.get(key)
        if hit is None:
            L, A, B = bast.srgb_to_oklab(rgb)
            hit = min(candidates, key=lambda i: ((self.lab[i][0] - L) ** 2
                                                 + (self.lab[i][1] - A) ** 2
                                                 + (self.lab[i][2] - B) ** 2, i))
            self.cache[key] = hit
        return hit


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


def postprocess(rgba, mat, w, h, ramp_of, quant):
    """Row-major RGBA8 bytes + material ids -> TEX file bytes.

    `mat` is a row-major HxW array of material ids (0 = none), `ramp_of`
    maps an id to its ramp's palette indices (dark to light), `quant` is a
    RampQuantizer. Edge contrast, ramp-constrained quantization, outline.
    """
    img = np.frombuffer(rgba, dtype=np.uint8).reshape(h, w, 4)
    alpha = img[..., 3]
    partial = int(np.count_nonzero((alpha > 0) & (alpha < 255)))
    if partial:
        raise ValueError("%d pixel(s) with partial alpha (anti-aliasing)" % partial)
    opaque = alpha == 255
    rgb = np.rint(edge_contrast(img[..., :3].astype(np.float64), opaque)).astype(int)
    edge = outline_mask(opaque)
    px = bytearray([bast.TRANSPARENT]) * (w * h)
    for y, x in zip(*np.nonzero(opaque)):
        ramp = ramp_of.get(int(mat[y, x]))
        if ramp is None:
            raise ValueError("pixel (%d,%d) has material id %d with no ramp"
                             % (x, y, int(mat[y, x])))
        if len(ramp) == 1:
            i = ramp[0]
        elif edge[y, x]:
            i = ramp[0]
        else:
            i = quant.nearest(tuple(int(v) for v in rgb[y, x]), ramp[1:])
        px[int(x) * h + int(y)] = i  # TEX is column-major
    flags = bast.TEX_HAS_TRANSPARENCY if not opaque.all() else 0
    t = bast.Tex(w, h, px, (w // 2, h), flags)
    return quantize.tex_bytes(t)


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
    for name, rot, strength in (("vs_key", KEY_ROT, KEY_STRENGTH),
                                ("vs_fill", FILL_ROT, FILL_STRENGTH)):
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
    for lamp in lamps:
        lamp.hide_render = False
    out = np.empty(acc.shape, dtype=np.uint8)
    alpha = acc[..., 3:4]
    out[..., :3] = linear_to_srgb8(acc[..., :3] / np.maximum(alpha, 1e-6))
    out[..., 3] = np.rint(np.clip(acc[..., 3], 0, 1) * 255).astype(np.uint8)
    out[..., :3] *= (out[..., 3:4] > 0)
    return out.tobytes()


def material_ids(bpy, scene):
    """Material name -> id (1..) for every material on a visible mesh, plus
    one flat emission material per id that renders as linear id / ID_SCALE."""
    names = sorted({s.material.name for o in scene.objects if o.type == "MESH"
                    for s in o.material_slots if s.material is not None})
    if len(names) >= ID_SCALE:
        raise SystemExit("blender_render: too many materials (%d)" % len(names))
    ids, flat = {}, {}
    for k, name in enumerate(names, 1):
        ids[name] = k
        m = bpy.data.materials.new("vs_id_%d" % k)
        if not m.node_tree:
            m.use_nodes = True
        nt = m.node_tree
        nt.nodes.clear()
        em = nt.nodes.new("ShaderNodeEmission")
        v = k / ID_SCALE
        em.inputs["Color"].default_value = (v, v, v, 1.0)
        em.inputs["Strength"].default_value = 1.0
        outn = nt.nodes.new("ShaderNodeOutputMaterial")
        nt.links.new(em.outputs["Emission"], outn.inputs["Surface"])
        flat[name] = m
    return ids, flat


def render_material_pass(bpy, scene, lamps, flat, stem, tmp):
    """Every material swapped for its flat id emission, lamps off.
    Returns a row-major HxW int array of material ids (0 = background or a
    surface without material)."""
    saved = []
    for o in scene.objects:
        if o.type != "MESH":
            continue
        for s in o.material_slots:
            if s.material is not None and s.material.name in flat:
                saved.append((s, s.material))
                s.material = flat[s.material.name]
    for lamp in lamps:
        lamp.hide_render = True
    try:
        lin = render_linear(bpy, scene, os.path.join(tmp, "%s_id.exr" % stem))
    finally:
        for s, m in saved:
            s.material = m
        for lamp in lamps:
            lamp.hide_render = False
    ids = np.rint(lin[..., 0] * ID_SCALE).astype(int)
    return np.where(lin[..., 3] > 0.5, ids, 0)


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
    try:
        ramps = resolve_ramps(gpl.load_gpl(), cfg.get("ramps"))
    except ValueError as e:
        raise SystemExit("blender_render: %s" % e)
    quant = RampQuantizer(pal)
    scene, rig, lamps = setup_scene(bpy, a.w, a.h)
    obj = find_object(bpy, cfg)
    ids, flat = material_ids(bpy, scene)
    unknown = sorted(set(ramps) - set(ids))
    if unknown:
        raise SystemExit("blender_render: sprite.json ramps name unknown material(s) %s"
                         % ", ".join(unknown))
    ramp_of = {ids[m]: r for m, r in ramps.items()}
    id_name = {k: m for m, k in ids.items()}
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
                mat = render_material_pass(bpy, scene, lamps, flat, stem, tmp)
                if a.raw:
                    from PIL import Image
                    Image.frombytes("RGBA", (a.w, a.h), rgba).save(
                        os.path.join(a.raw, stem + ".png"))
                    Image.fromarray(mat.astype(np.uint8), "L").save(
                        os.path.join(a.raw, stem + "_mat.png"))
                try:
                    data = postprocess(rgba, mat, a.w, a.h, ramp_of, quant)
                except ValueError as e:
                    m = str(e)
                    for k, name in id_name.items():
                        m = m.replace("material id %d " % k, "material %r " % name)
                    raise SystemExit("blender_render: %s: %s" % (stem, m))
                dst = os.path.join(a.out, stem + ".tex")
                with open(dst, "wb") as f:
                    f.write(data)
                if a.preview:
                    bast.Tex.load(dst).to_image(pal).save(os.path.join(a.preview, stem + ".png"))
                count += 1
    shutil.rmtree(tmp, ignore_errors=True)
    if a.raw:
        with open(os.path.join(a.raw, "materials.json"), "w") as f:
            json.dump({str(k): m for k, m in sorted(id_name.items())}, f, indent=2)
    print("blender_render: %d sprite(s) -> %s (%dx%d, origin %d,%d)"
          % (count, a.out, a.w, a.h, a.w // 2, a.h))


if __name__ == "__main__":
    main()
