"""Chihuahua Chomper model and animations (docs/design/enemies.md, art-bible.md).

Generates the .blend rendered by tools/blender_render.py; run by
tools/build.d/60-sprites.sh:

    blender -b --factory-startup --python assets/src/sprites/chomp/model.py -- <out_dir>

writes <out_dir>/chomp.blend and copies sprite.json next to it.

The look: a tiny four-legged dog-man with two huge triangle ears, an
oversized steel cyber-jaw that juts past the snout, `beige` fur and a `red`
visor band with two `fb_red` lenses. 24x28 px (0.375 x 0.4375 units) seen
from the front; the ear tips set both the width and the height.

Model convention (blender_render.py): faces -Y, stands on z = 0, origin at
(0, 0, 0). Low-poly, flat shaded: at 64 px per unit every facet is a few
pixels, which reads as painted planes instead of airbrushed gradients.

Rig: one armature "ChompRig" whose bones all point straight up (+Z) with
roll 0, so in pose space local X = world X (pitch: legs swing, jaw opens,
head nods), local Z = world -Y (roll: ears splay, body tips over) and local
Y = world Z (location along local Y lifts). Every vertex belongs to exactly
one bone (rigid parts, no blending). Actions use constant interpolation:
every Blender frame is a pose, sprite.json lists which frames are rendered.
"""
import json
import math
import os
import shutil
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
out_dir = sys.argv[sys.argv.index("--") + 1]
os.makedirs(out_dir, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)

# Base colours in sRGB (Standard view transform: a face square to the key
# renders at exactly this colour, art-bible.md Pre-render exposure).
MATS = {
    "ChompFur": (0.93, 0.86, 0.72),      # beige, lit faces near the top
    "ChompFurDark": (0.72, 0.62, 0.48),  # muzzle patch, paws: lower beige
    "ChompVisor": (0.66, 0.10, 0.08),    # red visor band, darker than the lenses
    "ChompEar": (1.00, 0.52, 0.44),      # inner ear: light end of red
    "ChompMouth": (0.55, 0.08, 0.06),    # mouth inside: dark red
    "ChompJaw": (0.74, 0.80, 0.88),      # steel cyber-jaw
    "ChompTooth": (0.86, 0.90, 0.95),    # steel fangs, brightest steel
    "ChompGlow": (1.00, 0.13, 0.13),     # fb_red lenses (fullbright)
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


# Everything (vertices and bone pivots) is moved SHIFT toward +Y (back) so
# the open jaw and the lunges fit the 40 px canvas (+-0.3125 u) in the side
# views; the tail is short and upright for the same reason.
SHIFT = 0.03


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


def ellipsoid(c, r, mat, bone, seg=8, rings=5):
    """Low-poly UV ellipsoid; flat shaded facets."""
    vs = [(c[0], c[1], c[2] - r[2])]
    for i in range(1, rings):
        t = math.pi * i / rings
        z, s = -math.cos(t), math.sin(t)
        for j in range(seg):
            a = 2 * math.pi * (j + 0.5) / seg
            vs.append((c[0] + r[0] * s * math.cos(a), c[1] + r[1] * s * math.sin(a), c[2] + r[2] * z))
    vs.append((c[0], c[1], c[2] + r[2]))
    top = len(vs) - 1
    fs = []
    for j in range(seg):
        fs.append((0, 1 + (j + 1) % seg, 1 + j))
    for i in range(rings - 2):
        a0, a1 = 1 + i * seg, 1 + (i + 1) * seg
        for j in range(seg):
            k = (j + 1) % seg
            fs.append((a0 + j, a0 + k, a1 + k, a1 + j))
    last = 1 + (rings - 2) * seg
    for j in range(seg):
        fs.append((last + j, last + (j + 1) % seg, top))
    add(vs, fs, mat, bone)


def prism(tri, y0, y1, mat, bone):
    """Triangle `tri` [(x, z)] x3 extruded from y0 (front) to y1 (back)."""
    vs = [(x, y0, z) for x, z in tri] + [(x, y1, z) for x, z in tri]
    fs = [(0, 1, 2), (5, 4, 3), (0, 3, 4, 1), (1, 4, 5, 2), (2, 5, 3, 0)]
    add(vs, fs, mat, bone)


# Pivots (bone heads), before SHIFT.
NECK = (0.0, -0.09, 0.27)
HINGE = (0.0, -0.15, 0.265)
EAR_L, EAR_R = (0.06, -0.14, 0.37), (-0.06, -0.14, 0.37)
HIPS = {"leg_fl": (0.06, -0.06, 0.15), "leg_fr": (-0.06, -0.06, 0.15),
        "leg_bl": (0.06, 0.15, 0.15), "leg_br": (-0.06, 0.15, 0.15)}
TAIL = (0.0, 0.18, 0.22)
LENS = (0.0, -0.245, 0.33)

# Body: barrel chest, short back.
ellipsoid((0.0, 0.05, 0.19), (0.075, 0.15, 0.065), "ChompFur", "root")
ellipsoid((0.0, -0.06, 0.215), (0.07, 0.07, 0.07), "ChompFur", "root")
# Collar: steel band at the neck.
box(-0.062, 0.062, -0.115, -0.075, 0.215, 0.29, "ChompJaw", "root")
# Legs: thin, steel-capped paws.
for bone, (x, y, z) in HIPS.items():
    box(x - 0.021, x + 0.021, y - 0.021, y + 0.021, 0.025, z + 0.02, "ChompFur", bone)
    box(x - 0.026, x + 0.026, y - 0.034, y + 0.026, 0.0, 0.03, "ChompJaw", bone)
# Tail: two thin segments curling up and back.
box(-0.016, 0.016, 0.17, 0.215, 0.215, 0.25, "ChompFur", "tail")
box(-0.016, 0.016, 0.195, 0.228, 0.245, 0.31, "ChompFurDark", "tail")
# Head: apple dome.
ellipsoid((0.0, -0.15, 0.335), (0.095, 0.085, 0.07), "ChompFur", "head", seg=10)
# Huge triangle ears: outer fur prism, inner red prism just in front.
for s, bone in ((1, "ear_l"), (-1, "ear_r")):
    prism([(s * 0.035, 0.395), (s * 0.085, 0.335), (s * 0.186, 0.4375)],
          -0.15, -0.125, "ChompFur", bone)
    prism([(s * 0.070, 0.383), (s * 0.095, 0.355), (s * 0.150, 0.415)],
          -0.157, -0.15, "ChompEar", bone)
# Red visor band wrapping the face, two fullbright lenses on it.
box(-0.092, 0.092, -0.238, -0.12, 0.318, 0.348, "ChompVisor", "head")
for s in (1, -1):
    box(s * 0.045 - 0.02, s * 0.045 + 0.02, -0.244, -0.238, 0.324, 0.342, "ChompGlow", "lens")
# Oversized cyber-jaw: steel upper plate over the snout, fur muzzle patch.
box(-0.07, 0.07, -0.285, -0.17, 0.276, 0.312, "ChompJaw", "head")
box(-0.055, 0.055, -0.235, -0.15, 0.255, 0.30, "ChompFurDark", "head")
box(-0.058, 0.058, -0.292, -0.278, 0.258, 0.276, "ChompTooth", "head")    # upper fangs
# Lower jaw (hinged): heavy steel scoop wider than the head, red mouth on top,
# fang row at the front.
box(-0.08, 0.08, -0.30, -0.15, 0.21, 0.25, "ChompJaw", "jaw")
box(-0.058, 0.058, -0.275, -0.16, 0.25, 0.256, "ChompMouth", "jaw")
for x in (-0.054, -0.018, 0.018, 0.054):
    box(x - 0.012, x + 0.012, -0.298, -0.28, 0.25, 0.272, "ChompTooth", "jaw")

mesh = bpy.data.meshes.new("Chomp")
mesh.from_pydata(verts, [], faces)
for m in ORDER:
    mesh.materials.append(mats[m])
for poly, mi in zip(mesh.polygons, face_mat):
    poly.material_index = mi
    poly.use_smooth = False
mesh.validate()
mesh.update()
body = bpy.data.objects.new("Chomp", mesh)
scene = bpy.context.scene
scene.collection.objects.link(body)

# ---------------------------------------------------------------------- rig
BONES = {  # name: (head, parent)
    "root": ((0.0, 0.0, 0.0), None),
    "head": (NECK, "root"),
    "jaw": (HINGE, "head"),
    "lens": (LENS, "head"),
    "ear_l": (EAR_L, "head"),
    "ear_r": (EAR_R, "head"),
    "tail": (TAIL, "root"),
}
BONES.update({k: (v, "root") for k, v in HIPS.items()})

arm = bpy.data.armatures.new("ChompRig")
rig = bpy.data.objects.new("ChompRig", arm)
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


def pose(**bones):
    """Reset every bone, then apply {bone: dict(rot=(x, y, z) deg, loc=(x, y, z),
    scale=s)}. Bone-local axes: X = world X, Y = up, Z = world -Y."""
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


U = 1.0 / 64.0  # one pixel
EAR_YAW = 20.0  # ears turned slightly outward: they still read in 3/4 views
up = lambda px: (0.0, px * U, 0.0)          # root lift in px (local Y = up)


def legs(fl, fr, bl, br):
    """Leg pitch in degrees; negative swings the paw forward (-Y)."""
    return {"leg_fl": {"rot": (fl, 0, 0)}, "leg_fr": {"rot": (fr, 0, 0)},
            "leg_bl": {"rot": (bl, 0, 0)}, "leg_br": {"rot": (br, 0, 0)}}


def ears(splay, back=0.0):
    """Splay: degrees each ear tips outward; back: degrees pitched back."""
    return {"ear_l": {"rot": (back, EAR_YAW, -splay)}, "ear_r": {"rot": (back, -EAR_YAW, splay)}}


# Idle: breathing loop, 18 tics/frame. f1: chest drops a pixel, ears twitch.
make_action("ChompIdle", [
    {**ears(0), "tail": {"rot": (-10, 0, 0)}},
    {"root": {"loc": up(-1)}, **legs(4, 4, -4, -4), **ears(6),
     "head": {"rot": (4, 0, 0)}, "tail": {"rot": (8, 0, 0)}},
])
# Walk: diagonal pairs (trot), body bobs on the passing frames.
make_action("ChompWalk", [
    {**legs(-28, 22, 22, -28), **ears(3), "head": {"rot": (-3, 0, 0)}},
    {"root": {"loc": up(1)}, **legs(0, 0, 0, 0), **ears(-3), "tail": {"rot": (-20, 0, 0)}},
    {**legs(22, -28, -28, 22), **ears(3), "head": {"rot": (-3, 0, 0)}},
    {"root": {"loc": up(1)}, **legs(0, 0, 0, 0), **ears(-3), "tail": {"rot": (15, 0, 0)}},
])
# Wind-up: crouch, head up, jaws thrown wide open (single pose held).
make_action("ChompWindup", [
    {"root": {"loc": up(-2)}, **legs(-8, -8, 18, 18),
     "head": {"rot": (-14, 0, 0)}, "jaw": {"rot": (58, 0, 0)}, **ears(12, 20),
     "tail": {"rot": (-30, 0, 0)}},
])
# Attack: f0 lunge and snap shut on the target, f1 recover with jaws half open.
make_action("ChompAttack", [
    {"root": {"loc": (0.0, 2 * U, 1 * U), "rot": (-8, 0, 0)}, **legs(-38, -38, 30, 30),
     "head": {"rot": (10, 0, 0)}, "jaw": {"rot": (-4, 0, 0)}, **ears(-6, 25),
     "tail": {"rot": (10, 0, 0)}},
    {"root": {"loc": up(0)}, **legs(-8, -8, 6, 6), "jaw": {"rot": (22, 0, 0)}, **ears(4, 6)},
])
# Pain: jerked back and sideways, jaw slack, ears flattened.
make_action("ChompPain", [
    {"root": {"rot": (-10, 0, 12)}, **legs(18, 18, -10, -10),
     "head": {"rot": (-24, 12, 0)}, "jaw": {"rot": (30, 0, 0)}, **ears(28, 30),
     "tail": {"rot": (40, 0, 0)}},
])
# Death: yelp and rear up, legs buckle, topple onto the left side; the last
# frame is the corpse (lenses dark: the lens bone shrinks into the visor).
make_action("ChompDeath", [
    {"root": {"rot": (-14, 0, 0)}, **legs(-20, -20, 10, 10),
     "head": {"rot": (-30, 0, 0)},
     "jaw": {"rot": (50, 0, 0)}, **ears(20, 25), "tail": {"rot": (30, 0, 0)}},
    {"root": {"loc": (2 * U, -U, -U), "rot": (0, 0, 18)}, **legs(30, 30, -24, -24),
     "head": {"rot": (16, 0, 0)}, "jaw": {"rot": (32, 0, 0)}, **ears(30, 10)},
    {"root": {"loc": (5 * U, 0.0, -U), "rot": (0, 0, 42)}, **legs(50, 40, -40, -36),
     "head": {"rot": (24, 0, 10)}, "jaw": {"rot": (24, 0, 0)}, **ears(40, 20)},
    {"root": {"loc": (7 * U, 3 * U, 0.0), "rot": (0, 0, 70)}, **legs(16, 24, -10, -16),
     "head": {"rot": (14, 0, 18)}, "jaw": {"rot": (30, 0, 0)}, **ears(50, 60),
     "tail": {"rot": (50, 0, 0)}, "lens": {"scale": 0.01}},
    {"root": {"loc": (9 * U, 4 * U, -U), "rot": (0, 0, 84)}, **legs(20, 40, -16, -36),
     "head": {"rot": (10, 0, 22)}, "jaw": {"rot": (36, 0, 0)}, **ears(55, 70),
     "tail": {"rot": (60, 0, 0)}, "lens": {"scale": 0.01}},
])
pose()

scene.frame_start, scene.frame_end = 1, 5
blend = os.path.join(out_dir, "chomp.blend")
bpy.ops.wm.save_as_mainfile(filepath=blend)
shutil.copy(os.path.join(HERE, "sprite.json"), os.path.join(out_dir, "sprite.json"))
print("chomp: wrote %s" % blend)
