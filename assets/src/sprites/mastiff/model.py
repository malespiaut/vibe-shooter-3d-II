"""Mastiff Juggernaut model and animations (docs/design/enemies.md, art-bible.md).

Generates the .blend rendered by tools/blender_render.py; run by
tools/build.d/60-sprites.sh:

    blender -b --factory-startup --python assets/src/sprites/mastiff/model.py -- <out_dir>

writes <out_dir>/mastiff.blend and copies sprite.json next to it.

The look: a massive chrome-plated (`steel`) dog torso, a barrel chest with
a blocky, low-slung mastiff head in front (heavy `gray` jowls and floppy ears,
one `fb_red` visor eye), standing on two short piston legs that end in
`gray` track units with steel road wheels. Two square missile pods sit on
the shoulders, `gray` housings behind steel clamshell hatches, an `fb_red` pod light
on each. The wind-up opens the clamshell hatches (upper plate up, lower down) to show four
`red` missile noses per pod (f0), then the noses glow `fb_red` (f1).
Silhouette: a wide box with a block on each shoulder, wider than tall at the
shoulders; 58x64 px (0.9 x 1.0 units) seen from the front.

Model convention (blender_render.py): faces -Y, stands on z = 0, origin at
(0, 0, 0). Low-poly, flat shaded: big chamfered planes read as polished
chrome panels at 64 px per unit instead of airbrushed gradients.

Rig: one armature "MastiffRig" whose bones all point straight up (+Z) with
roll 0, so in pose space local X = world X (pitch: legs swing, jaw opens,
lids open), local Z = world -Y (roll: body tips over) and local Y = world Z
(location along local Y lifts, along local Z moves forward). Every vertex
belongs to exactly one bone (rigid parts). Parts that appear or disappear
(missile glow, eye and pod lights on the corpse) are scaled to 0.01 around a
bone head hidden inside the hull. Actions use constant interpolation.
"""
import json
import math
import os
import shutil
import sys

import bmesh
import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
out_dir = sys.argv[sys.argv.index("--") + 1]
os.makedirs(out_dir, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)

# Base colours in sRGB (Standard view transform: a face square to the key
# renders at exactly this colour, art-bible.md Pre-render exposure).
MATS = {
    "MastChrome": (0.80, 0.87, 0.95),      # chrome plating: top of steel
    "MastChromeDark": (0.52, 0.58, 0.66),  # panel seams, lid frames: mid steel
    "MastGray": (0.80, 0.80, 0.79),        # pod housings, jowls, joints
    "MastTread": (0.56, 0.55, 0.55),       # track belts, nose: low gray
    "MastMissile": (0.92, 0.30, 0.24),     # missile noses (unlit wind-up f0)
    "MastEye": (1.00, 0.13, 0.13),         # fb_red visor eye and pod lights
    "MastGlow": (1.00, 0.13, 0.13),        # fb_red missile glow (wind-up f1, attack)
}
ORDER = list(MATS)


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


mats = {}
for name, rgb in MATS.items():
    m = bpy.data.materials.new(name)
    if not m.node_tree:
        m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    lin = tuple(srgb_to_linear(c) for c in rgb) + (1.0,)
    bsdf.inputs["Base Color"].default_value = lin
    bsdf.inputs["Roughness"].default_value = 1.0
    for key in ("Specular IOR Level", "Specular"):
        if key in bsdf.inputs:
            bsdf.inputs[key].default_value = 0.0
    m.diffuse_color = lin
    mats[name] = m

# ------------------------------------------------------------------ geometry
verts, faces, face_mat, vgroup = [], [], [], []

# Everything (vertices and bone pivots) moves SHIFT toward +Y (back) so the
# head and the opened hatches fit the 80 px canvas (+-0.625 u) in side views.
SHIFT = 0.04


def add(vs, fs, mat, bone):
    b = len(verts)
    verts.extend((x, y + SHIFT, z) for x, y, z in vs)
    vgroup.extend([bone] * len(vs))
    for f in fs:
        faces.append(tuple(b + i for i in f))
        face_mat.append(ORDER.index(mat))


def box(x0, x1, y0, y1, z0, z1, mat, bone):
    vs = [(x, y, z) for z in (z0, z1) for (x, y) in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]
    fs = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    add(vs, fs, mat, bone)


def prism_y(prof, y0, y1, mat, bone, back=None):
    """Convex profile [(x, z)] extruded from y0 (front) to y1 (back). `back`
    is an optional material for the back cap."""
    n = len(prof)
    vs = [(x, y0, z) for x, z in prof] + [(x, y1, z) for x, z in prof]
    add(vs, [tuple(range(n)), tuple(range(2 * n - 1, n - 1, -1))] +
        [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)], mat, bone)


def prism_x(prof, x0, x1, mat, bone):
    """Convex profile [(y, z)] extruded from x0 to x1."""
    n = len(prof)
    vs = [(x0, y, z) for y, z in prof] + [(x1, y, z) for y, z in prof]
    add(vs, [tuple(range(n)), tuple(range(2 * n - 1, n - 1, -1))] +
        [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)], mat, bone)


def wheel(x0, x1, y, z, r, mat, bone, seg=8):
    prism_x([(y + r * math.cos(2 * math.pi * (k + 0.5) / seg),
              z + r * math.sin(2 * math.pi * (k + 0.5) / seg)) for k in range(seg)],
            x0, x1, mat, bone)


# Pivots (bone heads), before SHIFT.
TORSO = (0.0, 0.0, 0.40)
NECK = (0.0, -0.24, 0.62)
HINGE = (0.0, -0.36, 0.555)
EYE = (0.0, -0.36, 0.66)
HIP = {1: (0.215, 0.0, 0.40), -1: (-0.215, 0.0, 0.40)}
ANKLE = {1: (0.25, 0.0, 0.16), -1: (-0.25, 0.0, 0.16)}
POD = {1: (0.36, 0.0, 0.62), -1: (-0.36, 0.0, 0.62)}
LID = {1: (0.36, -0.245, 0.96), -1: (-0.36, -0.245, 0.96)}  # top front edge
FLAP = {1: (0.36, -0.245, 0.60), -1: (-0.36, -0.245, 0.60)}  # bottom front edge
GLOW = {1: (0.36, -0.06, 0.78), -1: (-0.36, -0.06, 0.78)}   # inside the housing
PLIGHT = {1: (0.40, -0.10, 0.93), -1: (-0.40, -0.10, 0.93)}


def side(s, name):
    return name + ("_l" if s > 0 else "_r")


for s in (1, -1):
    # Track unit (foot): rubber belt with rounded ends, three steel road
    # wheels and a steel mud guard on the outer face.
    xi, xo = s * 0.165, s * 0.335
    lo, hi = min(xi, xo), max(xi, xo)
    prism_x([(-0.31, 0.10), (-0.25, 0.0), (0.21, 0.0), (0.27, 0.10),
             (0.21, 0.20), (-0.25, 0.20)], lo, hi, "MastTread", side(s, "foot"))
    wo = s * 0.345
    for y in (-0.18, -0.03, 0.12):
        wheel(min(xo, wo), max(xo, wo), y, 0.10, 0.068, "MastChrome", side(s, "foot"))
    box(lo - 0.005, hi + 0.005, -0.27, 0.23, 0.20, 0.225, "MastChromeDark", side(s, "foot"))
    # Piston leg: steel shin into a gray knee block on top of the track.
    box(s * 0.215 - 0.05, s * 0.215 + 0.05, -0.06, 0.06, 0.20, 0.26, "MastGray", side(s, "foot"))
    box(s * 0.215 - 0.035, s * 0.215 + 0.035, -0.04, 0.04, 0.24, 0.42, "MastChrome", side(s, "leg"))

# Hull: gray belly armour, chamfered chrome barrel chest above it.
prism_y([(-0.21, 0.32), (0.21, 0.32), (0.25, 0.38), (0.25, 0.50), (-0.25, 0.50),
         (-0.25, 0.38)], -0.24, 0.22, "MastGray", "torso")
prism_y([(-0.22, 0.46), (0.22, 0.46), (0.30, 0.56), (0.29, 0.74), (0.17, 0.84),
         (-0.17, 0.84), (-0.29, 0.74), (-0.30, 0.56)], -0.27, 0.24, "MastChrome", "torso")
# Chest plate seam and a centre keel: dark steel bands just proud of the hull.
prism_y([(-0.04, 0.48), (0.04, 0.48), (0.05, 0.84), (-0.05, 0.84)],
        -0.285, 0.0, "MastChromeDark", "torso")
box(-0.31, 0.31, -0.20, -0.16, 0.555, 0.75, "MastChromeDark", "torso")    # shoulder rib
box(-0.31, 0.31, 0.10, 0.14, 0.555, 0.75, "MastChromeDark", "torso")      # rear rib
# Back: twin exhaust stacks.
for x in (-0.10, 0.10):
    box(x - 0.035, x + 0.035, 0.16, 0.23, 0.80, 0.90, "MastChromeDark", "torso")

# Head: blocky chrome skull, flat brow, short square muzzle with a gray
# nose pad, heavy gray jowls and floppy ears hanging either side.
prism_y([(-0.12, 0.54), (0.12, 0.54), (0.145, 0.60), (0.13, 0.73), (0.08, 0.77),
         (-0.08, 0.77), (-0.13, 0.73), (-0.145, 0.60)], -0.42, -0.20, "MastChrome", "head")
box(-0.135, 0.135, -0.435, -0.38, 0.64, 0.70, "MastChromeDark", "head")   # visor brow
prism_y([(-0.09, 0.53), (0.09, 0.53), (0.10, 0.60), (0.08, 0.635), (-0.08, 0.635),
         (-0.10, 0.60)], -0.53, -0.40, "MastChrome", "head")               # muzzle
box(-0.06, 0.06, -0.55, -0.50, 0.59, 0.645, "MastTread", "head")         # nose pad
for s in (1, -1):
    box(min(s * 0.075, s * 0.15), max(s * 0.075, s * 0.15), -0.50, -0.34, 0.47, 0.58,
        "MastGray", "head")                                               # jowl
    # Floppy ear: a thick triangular flap hanging down the side of the skull.
    prism_x([(-0.37, 0.77), (-0.22, 0.77), (-0.30, 0.56)],
            min(s * 0.14, s * 0.185), max(s * 0.14, s * 0.185), "MastGray", "head")
# Lower jaw (hinged): gray slab with a chrome lip.
box(-0.085, 0.085, -0.52, -0.36, 0.47, 0.53, "MastGray", "jaw")
box(-0.07, 0.07, -0.525, -0.505, 0.53, 0.545, "MastChrome", "jaw")
# One fullbright visor eye set into the brow.
box(-0.09, 0.09, -0.442, -0.43, 0.655, 0.685, "MastEye", "eye")

# Shoulder missile pods.
for s in (1, -1):
    lo, hi = min(s * 0.27, s * 0.45), max(s * 0.27, s * 0.45)
    pod = side(s, "pod")
    # Housing, steel saddle under it, side vents.
    box(lo, hi, -0.22, 0.20, 0.60, 0.96, "MastGray", pod)
    box(lo + 0.01, hi - 0.01, -0.18, 0.16, 0.56, 0.60, "MastChromeDark", pod)
    box(lo - 0.006 if s < 0 else hi, hi + 0.006 if s > 0 else lo, -0.10, 0.10, 0.70, 0.73,
        "MastChromeDark", pod)
    box(lo - 0.006 if s < 0 else hi, hi + 0.006 if s > 0 else lo, -0.10, 0.10, 0.78, 0.81,
        "MastChromeDark", pod)
    # Four missile noses (2x2) in the front face, hidden by the closed hatch.
    for x in (s * 0.315, s * 0.405):
        for z in (0.68, 0.87):
            box(x - 0.032, x + 0.032, -0.235, -0.215, z - 0.032, z + 0.032, "MastMissile", pod)
            box(x - 0.016, x + 0.016, -0.245, -0.235, z - 0.016, z + 0.016, "MastMissile", pod)
            # Glow caps over the noses: scaled to 0.01 inside the housing
            # unless the pod is lit.
            box(x - 0.034, x + 0.034, -0.25, -0.236, z - 0.034, z + 0.034, "MastGlow",
                side(s, "glow"))
    # Clamshell hatch: two chrome plates with dark frames, the upper hinged
    # on the top front edge (swings up), the lower on the bottom front edge
    # (swings down). Half-height plates keep the open pods inside the canvas
    # in the diagonal views.
    box(lo, hi, -0.275, -0.245, 0.78, 0.96, "MastChromeDark", side(s, "lid"))
    box(lo + 0.02, hi - 0.02, -0.285, -0.27, 0.80, 0.94, "MastChrome", side(s, "lid"))
    box(lo, hi, -0.275, -0.245, 0.60, 0.78, "MastChromeDark", side(s, "flap"))
    box(lo + 0.02, hi - 0.02, -0.285, -0.27, 0.62, 0.76, "MastChrome", side(s, "flap"))
    # Pod light on the outer top corner.
    px = s * 0.40
    box(px - 0.025, px + 0.025, -0.16, -0.06, 0.96, 0.995, "MastEye", side(s, "plight"))

mesh = bpy.data.meshes.new("Mastiff")
mesh.from_pydata(verts, [], faces)
for m in ORDER:
    mesh.materials.append(mats[m])
for poly, mi in zip(mesh.polygons, face_mat):
    poly.material_index = mi
    poly.use_smooth = False
mesh.validate()
bm = bmesh.new()
bm.from_mesh(mesh)
bmesh.ops.recalc_face_normals(bm, faces=bm.faces)  # every part is a closed convex solid
bm.to_mesh(mesh)
bm.free()
mesh.update()
body = bpy.data.objects.new("Mastiff", mesh)
scene = bpy.context.scene
scene.collection.objects.link(body)

# ---------------------------------------------------------------------- rig
BONES = {  # name: (head, parent)
    "root": ((0.0, 0.0, 0.0), None),
    "torso": (TORSO, "root"),
    "head": (NECK, "torso"),
    "jaw": (HINGE, "head"),
    "eye": (EYE, "head"),
}
for s in (1, -1):
    BONES[side(s, "leg")] = (HIP[s], "root")
    BONES[side(s, "foot")] = (ANKLE[s], "root")
    BONES[side(s, "pod")] = (POD[s], "torso")
    BONES[side(s, "lid")] = (LID[s], side(s, "pod"))
    BONES[side(s, "flap")] = (FLAP[s], side(s, "pod"))
    BONES[side(s, "glow")] = (GLOW[s], side(s, "pod"))
    BONES[side(s, "plight")] = (PLIGHT[s], side(s, "pod"))

arm = bpy.data.armatures.new("MastiffRig")
rig = bpy.data.objects.new("MastiffRig", arm)
scene.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
rig.select_set(True)
bpy.ops.object.mode_set(mode="EDIT")
for name, (head, parent) in BONES.items():
    eb = arm.edit_bones.new(name)
    eb.head = (head[0], head[1] + SHIFT, head[2])
    eb.tail = (head[0], head[1] + SHIFT, head[2] + 0.05)
    eb.roll = 0.0
for name, (head, parent) in BONES.items():
    if parent:
        arm.edit_bones[name].parent = arm.edit_bones[parent]
bpy.ops.object.mode_set(mode="OBJECT")

for name in BONES:
    body.vertex_groups.new(name=name)
for i, g in enumerate(vgroup):
    body.vertex_groups[g].add([i], 1.0, "REPLACE")
body.parent = rig
mod = body.modifiers.new("Armature", "ARMATURE")
mod.object = rig
for pb in rig.pose.bones:
    pb.rotation_mode = "XYZ"

# ---------------------------------------------------------------- animation
D = math.radians
U = 1.0 / 64.0  # one pixel
HIDDEN = {"glow_l": {"scale": 0.01}, "glow_r": {"scale": 0.01}}


def pose(**bones):
    """Reset every bone, then apply {bone: dict(rot=(x, y, z) deg, loc=(x, y, z),
    scale=s)}. Bone-local axes: X = world X, Y = up, Z = world -Y; +X pitch tips the
    top of a part forward. Glow caps
    are hidden unless a pose sets them."""
    bones = {**HIDDEN, **bones}
    for pb in rig.pose.bones:
        spec = bones.get(pb.name, {})
        pb.rotation_euler = tuple(D(v) for v in spec.get("rot", (0, 0, 0)))
        pb.location = spec.get("loc", (0, 0, 0))
        s = spec.get("scale", 1.0)
        pb.scale = (s, s, s)


def keyframe(frame):
    for pb in rig.pose.bones:
        for path in ("rotation_euler", "location", "scale"):
            pb.keyframe_insert(path, frame=frame)


def make_action(name, poses):
    rig.animation_data_create()
    act = bpy.data.actions.new(name)
    rig.animation_data.action = act
    if hasattr(rig.animation_data, "action_slot") and rig.animation_data.action_slot is None \
            and len(act.slots):
        rig.animation_data.action_slot = act.slots[0]
    for frame, bones in enumerate(poses, 1):
        pose(**bones)
        keyframe(frame)
    try:
        curves = act.fcurves
    except AttributeError:  # layered actions (Blender 4.4+)
        curves = [fc for layer in act.layers for strip in layer.strips
                  for bag in strip.channelbags for fc in bag.fcurves]
    for fc in curves:
        for kp in fc.keyframe_points:
            kp.interpolation = "CONSTANT"
    act.use_fake_user = True
    rig.animation_data.action = None


def up(px):
    return (0.0, px * U, 0.0)


def fwd(px):
    return (0.0, 0.0, px * U)


def step(l_fwd, l_up, r_fwd, r_up):
    """Track feet moved forward/up in px; the shins lean to follow."""
    out = {}
    for s, f, u in ((1, l_fwd, l_up), (-1, r_fwd, r_up)):
        out[side(s, "foot")] = {"loc": (0.0, u * U, f * U)}
        out[side(s, "leg")] = {"rot": (-1.6 * f, 0, 0), "loc": (0.0, u * U * 0.5, f * U * 0.5)}
    return out


def lids(l_deg, r_deg=None):
    """Clamshell hatches opened (degrees): upper plates up, lower plates down."""
    r_deg = l_deg if r_deg is None else r_deg
    return {"lid_l": {"rot": (-l_deg, 0, 0)}, "lid_r": {"rot": (-r_deg, 0, 0)},
            "flap_l": {"rot": (l_deg, 0, 0)}, "flap_r": {"rot": (r_deg, 0, 0)}}


def glow(l=True, r=True):
    out = {}
    if l:
        out["glow_l"] = {}
    if r:
        out["glow_r"] = {}
    return out


DARK = {"eye": {"scale": 0.01}, "plight_l": {"scale": 0.01}, "plight_r": {"scale": 0.01}}

# Idle: idling engine, 18 tics/frame. f1: hull settles a pixel, head nods.
make_action("MastIdle", [
    {},
    {"torso": {"loc": up(-1)}, "head": {"rot": (4, 0, 0)}, "jaw": {"rot": (6, 0, 0)}},
])
# Walk: the track feet take alternating heavy steps, the hull rocks.
make_action("MastWalk", [
    {**step(3, 0, -3, 0), "torso": {"loc": up(-1), "rot": (0, 0, 2)}, "head": {"rot": (-2, 0, 0)}},
    {**step(0, 0, 0, 3), "torso": {"rot": (0, 0, -1)}, "head": {"rot": (2, 0, 0)}},
    {**step(-3, 0, 3, 0), "torso": {"loc": up(-1), "rot": (0, 0, -2)}, "head": {"rot": (-2, 0, 0)}},
    {**step(0, 3, 0, 0), "torso": {"rot": (0, 0, 1)}, "head": {"rot": (2, 0, 0)}},
])
# Wind-up: braces (hull down, head low), hatches open; f1 the noses glow.
WIND = {"torso": {"loc": up(-2), "rot": (3, 0, 0)}, "head": {"rot": (8, 0, 0)},
        "jaw": {"rot": (14, 0, 0)}, **step(2, 0, -2, 0), **lids(96)}
make_action("MastWindup", [WIND, {**WIND, **glow()}])
# Attack: f0 the left pod fires (recoils back, its tubes flash), f1 recover
# with the hatches still open (the right pod fires on the next cycle in game).
make_action("MastAttack", [
    {"torso": {"loc": up(-1), "rot": (-4, 0, 3)}, "head": {"rot": (-4, 0, 0)},
     "jaw": {"rot": (22, 0, 0)}, **lids(100), "pod_l": {"loc": fwd(-3)}, **glow(True, False),
     **step(1, 0, -1, 0)},
    {"torso": {"rot": (-1, 0, 0)}, "jaw": {"rot": (8, 0, 0)}, **lids(80)},
])
# Pain: jolted back and sideways, head thrown up, jaw slack, hatches rattle.
make_action("MastPain", [
    {"torso": {"loc": up(-1), "rot": (-10, 0, -6)}, "head": {"rot": (-18, 8, 0)},
     "jaw": {"rot": (30, 0, 0)}, **lids(25, 10), **step(-2, 0, 2, 0)},
])
# Death: the pods blow open, the hull rears, buckles and drops onto the
# tracks, the head hits the floor; last frame is the corpse (eye and pod
# lights dark, one pod torn half off).
make_action("MastDeath", [
    {"torso": {"rot": (-8, 0, 4)}, "head": {"rot": (-20, 0, 0)}, "jaw": {"rot": (36, 0, 0)},
     **lids(60, 100), **glow(), **step(-2, 0, 2, 0)},
    {"torso": {"loc": (0.0, -4 * U, 0.0), "rot": (6, 0, -8)}, "head": {"rot": (10, 0, 6)},
     "jaw": {"rot": (28, 0, 0)}, **lids(30, 70), "pod_r": {"rot": (0, 0, -6)},
     **step(2, 0, 0, 0)},
    {"torso": {"loc": (0.0, -9 * U, -3 * U), "rot": (10, 0, -12)},
     "head": {"rot": (20, 0, 10)}, "jaw": {"rot": (20, 0, 0)}, **lids(35, 40),
     "pod_r": {"loc": (U, -2 * U, 0.0), "rot": (0, 0, -12)}, **step(4, 0, -2, 0),
     "eye": {"scale": 0.01}},
    {"torso": {"loc": (0.0, -14 * U, -5 * U), "rot": (12, 0, -10)},
     "head": {"rot": (24, 0, 12)}, "jaw": {"rot": (26, 0, 0)}, **lids(45, 30),
     "pod_r": {"loc": (2 * U, -4 * U, 0.0), "rot": (0, 0, -20)}, **step(6, 0, -3, 0),
     **DARK},
    {"torso": {"loc": (0.0, -16 * U, -5 * U), "rot": (13, 0, -8)},
     "head": {"rot": (26, 0, 14)}, "jaw": {"rot": (30, 0, 0)}, **lids(40, 20),
     "pod_r": {"loc": (2 * U, -6 * U, 0.0), "rot": (0, 0, -24)}, **step(6, 0, -3, 0),
     **DARK},
])
pose()

scene.frame_start, scene.frame_end = 1, 5
blend = os.path.join(out_dir, "mastiff.blend")
bpy.ops.wm.save_as_mainfile(filepath=blend)
shutil.copy(os.path.join(HERE, "sprite.json"), os.path.join(out_dir, "sprite.json"))
print("mastiff: wrote %s" % blend)
