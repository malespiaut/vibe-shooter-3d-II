"""Dachshund Howitzer model and animations (docs/design/enemies.md, art-bible.md).

Generates the .blend rendered by tools/blender_render.py; run by
tools/build.d/60-sprites.sh:

    blender -b --factory-startup --python assets/src/sprites/dach/model.py -- <out_dir>

writes <out_dir>/dach.blend and copies sprite.json next to it.

The look: a long, low `brown` sausage of a dog on short legs, long hanging
ears, with a `steel` mortar tube strapped upright on its back by a steel
saddle and belly band. Silhouette: horizontal sausage, vertical tube.
64x32 px (1.0 x 0.5 units) seen from the side: nose to tail sets the width,
the muzzle of the tube the height. The wind-up tilts the tube forward and
lights an `fb_red` glow ring round its muzzle.

Model convention (blender_render.py): faces -Y, stands on z = 0, origin at
(0, 0, 0). Low-poly, flat shaded: at 64 px per unit every facet is a few
pixels, which reads as painted planes instead of airbrushed gradients.

Rig (same convention as chomp/model.py): one armature "DachRig" whose bones
all point straight up (+Z) with roll 0, so in pose space local X = world X
(pitch: positive tips the top of a bone forward, swings a leg's paw back),
local Z = world -Y (roll: body topples sideways) and local Y = world Z
(location along local Y lifts). Every vertex belongs to exactly one bone
(rigid parts). Actions use constant interpolation: every Blender frame is a
pose, sprite.json lists which frames are rendered.
"""
import json  # noqa: F401  (kept for parity with the other sprite generators)
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
    "DachFur": (0.72, 0.51, 0.35),       # brown coat, lit faces near brown_13
    "DachFurDark": (0.44, 0.28, 0.15),   # ears, paws, nose, tail tip: mid brown
    "DachTube": (0.74, 0.80, 0.88),      # steel mortar tube
    "DachMetal": (0.50, 0.55, 0.62),     # darker steel: saddle, straps, muzzle lip
    "DachGlow": (1.00, 0.13, 0.13),      # fb_red muzzle glow (fullbright)
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


def add(vs, fs, mat, bone):
    b = len(verts)
    verts.extend(vs)
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


def cylinder(cx, cy, r, z0, z1, mat, bone, seg=8):
    """Upright low-poly cylinder, capped."""
    vs = []
    for z in (z0, z1):
        for j in range(seg):
            a = 2 * math.pi * (j + 0.5) / seg
            vs.append((cx + r * math.cos(a), cy + r * math.sin(a), z))
    fs = [tuple(range(seg - 1, -1, -1)), tuple(range(seg, 2 * seg))]
    for j in range(seg):
        k = (j + 1) % seg
        fs.append((j, k, seg + k, seg + j))
    add(vs, fs, mat, bone)


# Pivots (bone heads).
NECK = (0.0, -0.27, 0.22)
EAR_L, EAR_R = (0.058, -0.35, 0.30), (-0.058, -0.35, 0.30)
HIPS = {"leg_fl": (0.05, -0.22, 0.12), "leg_fr": (-0.05, -0.22, 0.12),
        "leg_bl": (0.05, 0.22, 0.12), "leg_br": (-0.05, 0.22, 0.12)}
TAIL = (0.0, 0.31, 0.21)
MOUNT = (0.0, 0.04, 0.26)       # tube base on the saddle
TUBE_XY = (0.0, 0.04)
GLOW = (0.0, 0.04, 0.44)        # inside the tube: the hidden glow ring collapses here

# Body: one long sausage plus a deep chest at the front and a rump.
ellipsoid((0.0, 0.0, 0.17), (0.082, 0.31, 0.08), "DachFur", "root", seg=10, rings=6)
ellipsoid((0.0, -0.2, 0.165), (0.09, 0.13, 0.092), "DachFur", "root")
ellipsoid((0.0, 0.24, 0.175), (0.08, 0.09, 0.08), "DachFur", "root")
# Short legs, darker paws pointing forward.
for bone, (x, y, z) in HIPS.items():
    box(x - 0.022, x + 0.022, y - 0.022, y + 0.022, 0.025, z + 0.01, "DachFur", bone)
    box(x - 0.025, x + 0.025, y - 0.04, y + 0.025, 0.0, 0.03, "DachFurDark", bone)
# Tail: thin, sticking up and back.
box(-0.018, 0.018, 0.3, 0.38, 0.2, 0.235, "DachFur", "tail")
box(-0.015, 0.015, 0.37, 0.44, 0.22, 0.28, "DachFurDark", "tail")
# Head: long skull and snout; the nose tip sets the front of the 64 px.
ellipsoid((0.0, -0.33, 0.265), (0.064, 0.078, 0.062), "DachFur", "head")
box(-0.036, 0.036, -0.47, -0.36, 0.222, 0.27, "DachFur", "head")
box(-0.022, 0.022, -0.495, -0.462, 0.244, 0.272, "DachFurDark", "head")      # nose
box(-0.038, 0.038, -0.46, -0.38, 0.21, 0.228, "DachFurDark", "head")       # jaw line
# Steel range-finder goggle across the brow.
box(-0.07, 0.07, -0.385, -0.355, 0.278, 0.30, "DachMetal", "head")
# Long hanging ears.
for s, bone in ((1, "ear_l"), (-1, "ear_r")):
    box(s * 0.058 - 0.013, s * 0.058 + 0.013, -0.375, -0.30, 0.175, 0.31, "DachFurDark", bone)
# Saddle and belly band holding the mortar.
box(-0.095, 0.095, -0.07, 0.15, 0.232, 0.262, "DachMetal", "root")
box(-0.094, 0.094, 0.0, 0.05, 0.085, 0.236, "DachMetal", "root")
box(-0.1, 0.1, -0.25, -0.215, 0.12, 0.236, "DachMetal", "root")          # chest strap
# Mortar tube: base ring, tube, muzzle lip.
cylinder(*TUBE_XY, 0.062, 0.258, 0.295, "DachMetal", "tube")
cylinder(*TUBE_XY, 0.044, 0.29, 0.47, "DachTube", "tube")
cylinder(*TUBE_XY, 0.052, 0.462, 0.5, "DachMetal", "tube")
# Glow ring just below the lip (scaled to nothing except in windup/attack).
cylinder(*TUBE_XY, 0.05, 0.43, 0.46, "DachGlow", "glow")

mesh = bpy.data.meshes.new("Dach")
mesh.from_pydata(verts, [], faces)
for m in ORDER:
    mesh.materials.append(mats[m])
for poly, mi in zip(mesh.polygons, face_mat):
    poly.material_index = mi
    poly.use_smooth = False
mesh.validate()
mesh.update()
body = bpy.data.objects.new("Dach", mesh)
scene = bpy.context.scene
scene.collection.objects.link(body)

# ---------------------------------------------------------------------- rig
BONES = {  # name: (head, parent)
    "root": ((0.0, 0.0, 0.0), None),
    "head": (NECK, "root"),
    "ear_l": (EAR_L, "head"),
    "ear_r": (EAR_R, "head"),
    "tail": (TAIL, "root"),
    "tube": (MOUNT, "root"),
    "glow": (GLOW, "tube"),
}
BONES.update({k: (v, "root") for k, v in HIPS.items()})

arm = bpy.data.armatures.new("DachRig")
rig = bpy.data.objects.new("DachRig", arm)
scene.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
rig.select_set(True)
bpy.ops.object.mode_set(mode="EDIT")
for name, (head, parent) in BONES.items():
    eb = arm.edit_bones.new(name)
    eb.head = head
    eb.tail = (head[0], head[1], head[2] + 0.05)
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
OFF = {"glow": {"scale": 0.01}}  # glow hidden inside the tube


def pose(**bones):
    """Reset every bone, then apply {bone: dict(rot=(x, y, z) deg, loc=(x, y, z),
    scale=s)}. Bone-local axes: X = world X, Y = up, Z = world -Y."""
    bones = {**OFF, **bones}
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


up = lambda px: (0.0, px * U, 0.0)          # root lift in px (local Y = up)
GLOW_ON = {"glow": {"scale": 1.0}}


def legs(fl, fr, bl, br):
    """Leg pitch in degrees; negative swings the paw forward (-Y)."""
    return {"leg_fl": {"rot": (fl, 0, 0)}, "leg_fr": {"rot": (fr, 0, 0)},
            "leg_bl": {"rot": (bl, 0, 0)}, "leg_br": {"rot": (br, 0, 0)}}


def ears(swing, splay=0.0):
    """Swing: degrees the ears pitch (positive flaps them forward);
    splay: degrees each ear lifts outward."""
    return {"ear_l": {"rot": (swing, 0, -splay)}, "ear_r": {"rot": (swing, 0, splay)}}


# Idle: breathing loop. f1: body sinks a pixel, ears and tail sway.
make_action("DachIdle", [
    {"tail": {"rot": (-8, 0, 0)}},
    {"root": {"loc": up(-1)}, **legs(5, 5, -5, -5), **ears(-6),
     "head": {"rot": (4, 0, 0)}, "tail": {"rot": (10, 0, 0)}},
])
# Walk: diagonal pairs, short stubby strides; the tube rocks against its
# straps. Angled legs reach less far down, so those frames drop a pixel to
# keep the paws on the floor.
make_action("DachWalk", [
    {"root": {"loc": up(-1)}, **legs(-30, 26, 26, -30), **ears(8),
     "tube": {"rot": (4, 0, 0)}, "tail": {"rot": (-14, 0, 0)}},
    {**legs(8, -8, -8, 8), **ears(-4), "head": {"rot": (-3, 0, 0)}},
    {"root": {"loc": up(-1)}, **legs(26, -30, -30, 26), **ears(8),
     "tube": {"rot": (-4, 0, 0)}, "tail": {"rot": (14, 0, 0)}},
    {**legs(-8, 8, 8, -8), **ears(-4), "head": {"rot": (-3, 0, 0)}},
])
# Wind-up: brace low, head down, the tube tilts toward the target and its
# muzzle glows (single pose held for both frames).
WINDUP = {"root": {"loc": up(-2)}, **legs(-12, -12, 14, 14), "head": {"rot": (10, 0, 0)},
          **ears(10, 8), "tube": {"rot": (24, 0, 0)}, "tail": {"rot": (-24, 0, 0)},
          **GLOW_ON}
make_action("DachWindup", [WINDUP])
# Attack: f0 launch, the recoil kicks the tube back upright and squats the
# rear, glow at full; f1 recover, tube settling forward, glow out.
make_action("DachAttack", [
    {"root": {"loc": (0.0, -2 * U, 1 * U), "rot": (6, 0, 0)}, **legs(-20, -20, 24, 24),
     "head": {"rot": (-10, 0, 0)}, **ears(-20, 14), "tube": {"rot": (-6, 0, 0)},
     "tail": {"rot": (30, 0, 0)}, **GLOW_ON},
    {"root": {"loc": up(-1)}, **legs(-6, -6, 6, 6), **ears(6, 4),
     "tube": {"rot": (10, 0, 0)}, "tail": {"rot": (-10, 0, 0)}},
])
# Pain: jolted sideways and back, head thrown up, tube knocked askew.
make_action("DachPain", [
    {"root": {"loc": up(1), "rot": (-6, 0, 12)}, **legs(16, 16, -12, -12),
     "head": {"rot": (-26, 10, 0)}, **ears(-30, 30), "tube": {"rot": (-10, 0, -16)},
     "tail": {"rot": (40, 0, 0)}},
])
# Death: yelp with the head up, legs buckle, roll onto the left side; the
# tube breaks loose and ends lying on the floor beside the body. f4 = corpse.
make_action("DachDeath", [
    {"root": {"loc": up(2), "rot": (-10, 0, 0)}, **legs(20, 20, -16, -16),
     "head": {"rot": (-32, 0, 0)}, **ears(-34, 20), "tube": {"rot": (-8, 0, 10)},
     "tail": {"rot": (34, 0, 0)}},
    {"root": {"loc": (U, 0.0, -U), "rot": (0, 0, 24)}, **legs(30, 30, -24, -24),
     "head": {"rot": (8, 0, 6)}, **ears(10, 30), "tube": {"rot": (0, 0, 20)}},
    {"root": {"loc": (3 * U, 0.0, -U), "rot": (0, 0, 52)}, **legs(46, 40, -40, -34),
     "head": {"rot": (16, 0, 16)}, **ears(20, 40), "tube": {"rot": (0, 0, 10)}},
    {"root": {"loc": (5 * U, 0.0, -U), "rot": (0, 0, 78)}, **legs(20, 30, -14, -24),
     "head": {"rot": (10, 0, 22)}, **ears(30, 60), "tube": {"rot": (0, 0, -14)},
     "tail": {"rot": (40, 0, 0)}},
    {"root": {"loc": (6 * U, 0.0, -2 * U), "rot": (0, 0, 86)}, **legs(26, 44, -20, -40),
     "head": {"rot": (6, 0, 26)}, **ears(36, 70), "tube": {"rot": (0, 0, -40)},
     "tail": {"rot": (50, 0, 0)}},
])
pose()

scene.frame_start, scene.frame_end = 1, 5
blend = os.path.join(out_dir, "dach.blend")
bpy.ops.wm.save_as_mainfile(filepath=blend)
shutil.copy(os.path.join(HERE, "sprite.json"), os.path.join(out_dir, "sprite.json"))
print("dach: wrote %s" % blend)
