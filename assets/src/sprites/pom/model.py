"""Pomeranian Plasmagunner model and animations (docs/design/enemies.md, art-bible.md).

Generates the .blend rendered by tools/blender_render.py; run by
tools/build.d/60-sprites.sh:

    blender -b --factory-startup --python assets/src/sprites/pom/model.py -- <out_dir>

writes <out_dir>/pom.blend and copies sprite.json next to it.

The look: a round puffball of `orange` fur (tufted, flat-shaded facets read
as fur clumps), a small face with a cream muzzle and `steel` bead eyes, two
tiny ears and a plumed tail curled over the back, tiny feet hidden under the
fur. On its right arm a thin `cyan` plasma cannon with `steel` bands. The
`fb_cyan` charge glow (muzzle and the coil window along the barrel) only
shows on windup f1 and on the attack shot frame. 40x48 px (0.625 x 0.75
units) seen from the front: the fur ball plus the cannon set the width, the
ear tips the height.

Model convention (blender_render.py): faces -Y, stands on z = 0, origin at
(0, 0, 0). Same rig convention as the Chomper (chomp/model.py): every bone
points straight up (+Z) with roll 0, so in pose space local X = world X
(pitch), local Z = world -Y (roll) and local Y = world Z (location along local
Y lifts, along local Z moves forward). Every vertex belongs to exactly one
bone. Actions use constant interpolation: every Blender frame is a pose,
sprite.json lists which frames are rendered.
"""
import json
import math
import random
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
    "PomFur": (1.00, 0.66, 0.30),       # orange fur, lit faces near the top
    "PomFurLight": (1.00, 0.80, 0.52),  # cream muzzle, chest ruff, tail tip
    "PomFurDark": (0.72, 0.40, 0.08),   # inner ears, feet, paw
    "PomCannon": (0.40, 0.82, 0.78),    # cyan cannon shell
    "PomCoil": (0.18, 0.50, 0.48),      # dark cyan coil window (unlit)
    "PomSteel": (0.74, 0.80, 0.88),     # steel bands, muzzle ring, grip
    "PomEye": (0.30, 0.33, 0.38),       # dark steel bead eyes and nose
    "PomGlow": (0.25, 1.00, 0.94),      # fb_cyan charge (fullbright)
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
RNG = random.Random(122)  # fixed seed: the same tufts every build


# Everything (vertices and bone pivots) is moved SHIFT toward +X (the dog's
# left) so the ball plus the cannon on its right sit centred on the origin and
# the cannon stays on the 56 px canvas (+-0.4375 u) in the diagonal views.
SHIFT = 0.035


def add(vs, fs, mat, bone):
    b = len(verts)
    verts.extend((x + SHIFT, y, z) for x, y, z in vs)
    vgroup.extend([bone] * len(vs))
    for f in fs:
        faces.append(tuple(b + i for i in f))
        face_mat.append(ORDER.index(mat))


def box(x0, x1, y0, y1, z0, z1, mat, bone):
    vs = [(x, y, z) for z in (z0, z1) for (x, y) in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]
    fs = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    add(vs, fs, mat, bone)


def ellipsoid(c, r, mat, bone, seg=8, rings=5, tuft=0.0, twist=0.5):
    """Low-poly UV ellipsoid; flat shaded facets. `tuft` > 0 pushes every
    other vertex outward (checkerboard, seeded random amount), which turns the
    facets into irregular pointed fur clumps instead of a smooth ball."""
    vs = [(c[0], c[1], c[2] - r[2])]
    for i in range(1, rings):
        t = math.pi * i / rings
        z, s = -math.cos(t), math.sin(t)
        for j in range(seg):
            a = 2 * math.pi * (j + twist * (i % 2)) / seg
            k = 1.0 + tuft * (0.35 + 0.65 * RNG.random() if (i + j) % 2 else 0.25 * RNG.random())
            vs.append((c[0] + k * r[0] * s * math.cos(a), c[1] + k * r[1] * s * math.sin(a),
                       c[2] + k * r[2] * z))
    vs.append((c[0], c[1], c[2] + r[2] * (1.0 + tuft)))
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


def cylinder(x, z, y0, y1, r, mat, bone, seg=6):
    """Cylinder along Y (front y0 < y1 back), axis at (x, z), capped."""
    ring = [(x + r * math.cos(2 * math.pi * (j + 0.5) / seg), z + r * math.sin(2 * math.pi * (j + 0.5) / seg))
            for j in range(seg)]
    vs = [(px, y0, pz) for px, pz in ring] + [(px, y1, pz) for px, pz in ring]
    fs = [tuple(range(seg - 1, -1, -1)), tuple(range(seg, 2 * seg))]
    for j in range(seg):
        k = (j + 1) % seg
        fs.append((j, k, seg + k, seg + j))
    add(vs, fs, mat, bone)


def prism(tri, y0, y1, mat, bone):
    """Triangle `tri` [(x, z)] x3 extruded from y0 (front) to y1 (back)."""
    vs = [(x, y0, z) for x, z in tri] + [(x, y1, z) for x, z in tri]
    fs = [(0, 1, 2), (5, 4, 3), (0, 3, 4, 1), (1, 4, 5, 2), (2, 5, 3, 0)]
    add(vs, fs, mat, bone)


# Pivots (bone heads).
BALL = (0.0, 0.02, 0.335)            # fur ball centre (the ruff bone scales about it)
FACE = (0.0, -0.17, 0.42)
SHOULDER_R = (-0.22, -0.06, 0.28)   # cannon arm (the dog's right is world -X)
SHOULDER_L = (0.20, -0.10, 0.30)
FEET = {"leg_l": (0.08, -0.04, 0.08), "leg_r": (-0.08, -0.04, 0.08)}
TAIL = (0.0, 0.20, 0.52)
CAN_X, CAN_Z = -0.30, 0.25         # cannon axis

# The fur ball: one big tufted ellipsoid, a cream chest ruff in front.
ellipsoid(BALL, (0.285, 0.25, 0.285), "PomFur", "ruff", seg=14, rings=8, tuft=0.12)
ellipsoid((0.0, -0.17, 0.25), (0.14, 0.10, 0.12), "PomFurLight", "ruff", seg=10, rings=6, tuft=0.12)
# Tiny feet barely peeking out under the fur.
for bone, (x, y, z) in FEET.items():
    box(x - 0.035, x + 0.035, y - 0.07, y + 0.02, 0.0, 0.05, "PomFurDark", bone)
# Plumed tail curled over the back.
ellipsoid((0.0, 0.18, 0.60), (0.10, 0.09, 0.07), "PomFur", "tail", seg=8, rings=5, tuft=0.18)
ellipsoid((0.0, 0.10, 0.65), (0.06, 0.06, 0.04), "PomFurLight", "tail", seg=6, rings=4, tuft=0.15)
# Small face on the front of the ball: cream muzzle, steel nose and eyes.
ellipsoid((0.0, -0.245, 0.40), (0.07, 0.05, 0.05), "PomFurLight", "head", seg=8, rings=5)
box(-0.018, 0.018, -0.30, -0.285, 0.415, 0.44, "PomEye", "head")              # nose
for s in (1, -1):
    box(s * 0.075 - 0.018, s * 0.075 + 0.018, -0.255, -0.235, 0.45, 0.485, "PomEye", "head")
# Two tiny pointed ears poking out of the top of the fur.
for s, bone in ((1, "ear_l"), (-1, "ear_r")):
    prism([(s * 0.05, 0.56), (s * 0.16, 0.54), (s * 0.115, 0.745)], -0.11, -0.03, "PomFur", bone)
    prism([(s * 0.075, 0.635), (s * 0.13, 0.625), (s * 0.112, 0.71)], -0.115, -0.11,
          "PomFurDark", bone)
# Left paw: a small fur stub on the front.
ellipsoid((0.17, -0.20, 0.26), (0.05, 0.05, 0.045), "PomFurDark", "arm_l", seg=6, rings=4)
# Right arm: fur cuff, then the plasma cannon (thin, along -Y), slung low
# on the outside of the ball so the barrel clears the fur in the side views.
ellipsoid((-0.25, -0.10, 0.25), (0.07, 0.08, 0.07), "PomFur", "arm_r", seg=8, rings=5, tuft=0.12)
cylinder(CAN_X, CAN_Z, -0.25, 0.10, 0.036, "PomCannon", "arm_r", seg=6)       # barrel shell
cylinder(CAN_X, CAN_Z, -0.27, -0.24, 0.046, "PomSteel", "arm_r", seg=6)       # muzzle ring
cylinder(CAN_X, CAN_Z, 0.02, 0.06, 0.046, "PomSteel", "arm_r", seg=6)         # rear band
cylinder(CAN_X, CAN_Z, 0.10, 0.13, 0.052, "PomCannon", "arm_r", seg=6)        # breech cap
box(CAN_X - 0.012, CAN_X + 0.012, -0.21, -0.03, CAN_Z + 0.032, CAN_Z + 0.046,
    "PomCoil", "arm_r")                                                      # coil window
box(CAN_X - 0.015, CAN_X + 0.015, -0.07, -0.03, CAN_Z - 0.10, CAN_Z - 0.03,
    "PomSteel", "arm_r")                                                     # grip
# Charge glow: muzzle disc and a strip over the coil window. Shrunk into the
# barrel (glow bone scale 0.01) except on windup f1 and the attack shot.
cylinder(CAN_X, CAN_Z, -0.28, -0.26, 0.032, "PomGlow", "glow", seg=6)
box(CAN_X - 0.014, CAN_X + 0.014, -0.22, -0.02, CAN_Z + 0.036, CAN_Z + 0.052, "PomGlow", "glow")

mesh = bpy.data.meshes.new("Pom")
mesh.from_pydata(verts, [], faces)
for m in ORDER:
    mesh.materials.append(mats[m])
for poly, mi in zip(mesh.polygons, face_mat):
    poly.material_index = mi
    poly.use_smooth = False
mesh.validate()
mesh.update()
body = bpy.data.objects.new("Pom", mesh)
scene = bpy.context.scene
scene.collection.objects.link(body)

# ---------------------------------------------------------------------- rig
BONES = {  # name: (head, parent)
    "root": ((0.0, 0.0, 0.0), None),
    "body": ((0.0, 0.0, 0.0), "root"),
    "ruff": (BALL, "body"),
    "head": (FACE, "body"),
    "ear_l": ((0.10, -0.07, 0.56), "head"),
    "ear_r": ((-0.10, -0.07, 0.56), "head"),
    "arm_r": (SHOULDER_R, "body"),
    "glow": ((CAN_X, -0.12, CAN_Z), "arm_r"),
    "arm_l": (SHOULDER_L, "body"),
    "tail": (TAIL, "body"),
}
BONES.update({k: (v, "root") for k, v in FEET.items()})

arm = bpy.data.armatures.new("PomRig")
rig = bpy.data.objects.new("PomRig", arm)
scene.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
rig.select_set(True)
bpy.ops.object.mode_set(mode="EDIT")
for name, (head, parent) in BONES.items():
    eb = arm.edit_bones.new(name)
    eb.head = (head[0] + SHIFT, head[1], head[2])
    eb.tail = (head[0] + SHIFT, head[1], head[2] + 0.05)
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
OFF = {"glow": {"scale": 0.01}}  # charge glow hidden


def pose(**bones):
    """Reset every bone, then apply {bone: dict(rot=(x, y, z) deg, loc=(x, y, z),
    scale=s or (x, y, z))}. Bone-local axes: X = world X, Y = up, Z = world -Y."""
    for pb in rig.pose.bones:
        spec = bones.get(pb.name, {})
        pb.rotation_euler = tuple(D(v) for v in spec.get("rot", (0, 0, 0)))
        pb.location = spec.get("loc", (0, 0, 0))
        s = spec.get("scale", 1.0)
        pb.scale = s if isinstance(s, tuple) else (s, s, s)


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


up = lambda px: (0.0, px * U, 0.0)          # lift in px (local Y = up)


def feet(l, r):
    """Foot pitch in degrees; negative swings the foot forward (-Y)."""
    return {"leg_l": {"rot": (l, 0, 0)}, "leg_r": {"rot": (r, 0, 0)}}


def ears(splay, back=0.0):
    return {"ear_l": {"rot": (back, 0, -splay)}, "ear_r": {"rot": (back, 0, splay)}}


# Idle: breathing loop. f1: the ball settles a pixel, cannon dips, tail sways.
make_action("PomIdle", [
    {**OFF},
    {**OFF, "body": {"loc": up(-1)}, "ruff": {"scale": (1.03, 0.97, 1.03)},
     "arm_r": {"rot": (4, 0, 0)}, **ears(5), "tail": {"rot": (0, 0, 8)}},
])
# Walk: a bouncy waddle on hidden feet, the ball rolls side to side.
make_action("PomWalk", [
    {**OFF, "body": {"loc": up(-1), "rot": (0, 0, 4)}, **feet(-30, 20), "tail": {"rot": (0, 0, -8)}},
    {**OFF, "body": {"loc": up(1)}, **feet(0, 0), "arm_r": {"rot": (-4, 0, 0)}, **ears(-4)},
    {**OFF, "body": {"loc": up(-1), "rot": (0, 0, -4)}, **feet(20, -30), "tail": {"rot": (0, 0, 8)}},
    {**OFF, "body": {"loc": up(1)}, **feet(0, 0), "arm_r": {"rot": (4, 0, 0)}, **ears(4)},
])
# Wind-up: fur ruffs up (the ball puffs out), cannon raised and braced; f1
# the cannon charges: fb_cyan muzzle and coil.
WIND = {"body": {"loc": up(-1)}, "ruff": {"scale": 1.07}, "arm_r": {"rot": (-6, 0, 0)},
        "arm_l": {"rot": (-20, 0, 0)}, **ears(-8, -10), "tail": {"rot": (-15, 0, 0)}}
make_action("PomWindup", [
    {**WIND, **OFF},
    {**WIND, "glow": {"scale": 1.12}},
])
# Attack: f0 the shot (glow at the muzzle, cannon kicked back two pixels),
# f1 recoil settles, glow spent.
make_action("PomAttack", [
    {"body": {"loc": up(-1)}, "ruff": {"scale": 1.05},
     "arm_r": {"rot": (-10, 0, 0), "loc": (0, 0, -2 * U)}, "glow": {"scale": 1.12},
     **ears(-6, -8), "tail": {"rot": (-10, 0, 0)}},
    {**OFF, "body": {"rot": (6, 0, 0)}, "arm_r": {"rot": (-16, 0, 0), "loc": (0, 0, -U)},
     **ears(4)},
])
# Pain: knocked back and squashed, cannon flung up, ears flat.
make_action("PomPain", [
    {**OFF, "body": {"rot": (10, 0, -6), "loc": (0, 0, -U)}, "ruff": {"scale": (1.04, 0.97, 1.04)},
     "arm_r": {"rot": (-18, 0, 6)}, "arm_l": {"rot": (-30, 0, 0)}, **ears(8, 12),
     "head": {"rot": (10, 0, 0)}, "tail": {"rot": (20, 0, 0)}},
])
# Death: yelp and rear back, topple onto the side, the fur ball deflates into
# a flat pile with the cannon on the floor (f4 = corpse).
make_action("PomDeath", [
    {**OFF, "body": {"rot": (10, 0, 0), "loc": (0, 0, -3 * U)}, "arm_r": {"rot": (-20, 0, 0)}, "arm_l": {"rot": (-40, 0, 0)},
     **ears(0, -8), "head": {"rot": (12, 0, 0)}},
    {**OFF, "body": {"rot": (8, 0, -22), "loc": (-4 * U, 0, 0)}, "arm_r": {"rot": (-10, 0, 0)},
     **ears(8, 10)},
    {**OFF, "body": {"rot": (0, 0, -50), "loc": (-15 * U, -2 * U, 0)}, "ruff": {"scale": (1.0, 0.85, 1.0)},
     "arm_r": {"rot": (20, 0, 0)}, **ears(-6, 0), "tail": {"rot": (0, 0, 20)}},
    {**OFF, "body": {"rot": (0, 0, -70), "loc": (-14 * U, -5 * U, 0), "scale": (1.0, 0.85, 1.0)},
     "ruff": {"scale": (1.05, 0.85, 1.05)}, "arm_r": {"rot": (30, 0, 0)}, **ears(20, 20)},
    {**OFF, "body": {"rot": (0, 0, -84), "loc": (-14 * U, -9 * U, 0), "scale": (1.05, 0.75, 1.05)},
     "ruff": {"scale": (1.08, 0.8, 1.08)}, "arm_r": {"rot": (40, 0, 0)}, **ears(20, 25),
     "tail": {"rot": (0, 0, 30)}},
])
pose(**OFF)

scene.frame_start, scene.frame_end = 1, 5
blend = os.path.join(out_dir, "pom.blend")
bpy.ops.wm.save_as_mainfile(filepath=blend)
shutil.copy(os.path.join(HERE, "sprite.json"), os.path.join(out_dir, "sprite.json"))
print("pom: wrote %s" % blend)
