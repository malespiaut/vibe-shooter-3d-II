"""Woofu, the Chrome Pharaoh: boss model and animations (docs/design/enemies.md,
Boss; art-bible.md Sizes: 96x96 on a 128x112 canvas).

    blender -b --factory-startup --python assets/src/sprites/woofu/model.py -- <out_dir> [--lidless]

writes <out_dir>/woofu.blend (or woofu2.blend with --lidless) and copies the
matching sprite.json next to it (tools/build.d/60-sprites.sh runs this, then
renders with tools/blender_render.py). assets/src/sprites/woofu2/model.py
runs this script with --lidless: the phase-2 set (`woofu2_*`) is the same
model with the sarcophagus lid gone.

Look: a cybernetic greyhound pharaoh fused into a walking chrome
sarcophagus. A tall gold mummy case, wide at the shoulders and narrowing to
the feet, walks on two short steel mech legs. The front lid (two halves)
carries blue lapis bands, a gold collar and a crossed-paws relief. Out of
the case top rises a narrow steel greyhound head with a long thin snout,
fullbright cyan eyes, a gold-and-lapis striped nemes headdress whose
lappets fall onto the chest, and a pompous blue lapis false beard. The
right arm (character's right, -X, on the left in d0) holds a tall gold
plasma sceptre raised beside the head, tip an fb_cyan orb; the left arm
swings a chain mace (steel chain, gold spiked head). Under the lid: a steel
ribcage of machinery with a blue lapis heart, framed by the gold case rim.

Animation: wind-up raises the sceptre and the orb swells (glow on f1);
attack thrusts it forward with a cyan burst; `phase` blows both lid halves
off (cracks of cyan light, burst, halves tumbling away); `death` (lidless,
the boss only dies in phase 2) topples the case backwards into a wreck.

Rig: one armature "Woofu" and one mesh skinned rigidly to it (every part's
vertices belong 100% to one bone). Every bone points along +Z with roll 0;
poses are world-axis rotations relative to the parent bone (helper `pose`),
pure forward kinematics.

All lengths are in pixels (1 world unit = 64 px); the model faces -Y and
stands on z = 0 (tools/blender_render.py convention).
"""
import math
import os
import shutil
import sys

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
argv = sys.argv[sys.argv.index("--") + 1:]
out_dir = argv[0]
LIDLESS = "--lidless" in argv[1:]
NAME = "woofu2" if LIDLESS else "woofu"
os.makedirs(out_dir, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)

PX = 1.0 / 64.0
HIDE = 0.001  # scale of a hidden part: smaller than a pixel


def V(x, y, z):
    return Vector((x * PX, y * PX, z * PX))


# --------------------------------------------------------------- materials
# Base colours are sRGB (lit faces render at the base colour); the ramp
# each maps to is in sprite.json.
MATS = {
    "WoofuGold": (0.90, 0.76, 0.20),       # gold: case, sceptre, nemes
    "WoofuGoldDark": (0.66, 0.53, 0.06),   # gold: rims, relief, feet
    "WoofuLapis": (0.30, 0.50, 0.82),      # blue: inlay bands, beard, heart
    "WoofuLapisDark": (0.18, 0.31, 0.56),  # blue: inner bands
    "WoofuSteel": (0.70, 0.76, 0.84),      # steel: chrome head, arms
    "WoofuSteelDark": (0.42, 0.46, 0.53),  # steel: legs, chain, machinery
    "WoofuGlow": (0.25, 1.0, 0.94),        # fb_cyan: eyes, orb, plasma
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

# ------------------------------------------------------------------ mesh
verts, faces, face_mat, face_smooth, vgroup = [], [], [], [], []


def add_bm(bm, mat, bone, smooth):
    base = len(verts)
    for v in bm.verts:
        verts.append(tuple(v.co))
        vgroup.append(bone)
    for f in bm.faces:
        faces.append(tuple(base + v.index for v in f.verts))
        face_mat.append(ORDER.index(mat))
        face_smooth.append(smooth)
    bm.free()


def xform(center, size, rot=(0, 0, 0)):
    s = Matrix.Diagonal((size[0] * PX, size[1] * PX, size[2] * PX, 1.0))
    r = Euler(tuple(math.radians(a) for a in rot)).to_matrix().to_4x4()
    return Matrix.Translation(V(*center)) @ r @ s


def box(center, size, mat, bone, rot=(0, 0, 0)):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0, matrix=xform(center, size, rot))
    add_bm(bm, mat, bone, False)


def frustum(z0, z1, bot, top, mat, bone, y=0.0, x=0.0, ytop=None, xtop=None):
    """Box from z0 to z1 whose (w, d) goes from `bot` at z0 to `top` at z1;
    centred on (x, y) at the bottom and (xtop, ytop) at the top."""
    ytop = y if ytop is None else ytop
    xtop = x if xtop is None else xtop
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        t = v.co.z + 0.5
        w = bot[0] + (top[0] - bot[0]) * t
        d = bot[1] + (top[1] - bot[1]) * t
        cy = y + (ytop - y) * t
        cx = x + (xtop - x) * t
        v.co = V(cx + v.co.x * w, cy + v.co.y * d, z0 + (z1 - z0) * t)
    add_bm(bm, mat, bone, False)


def ball(center, size, mat, bone, rot=(0, 0, 0), seg=12, ring=8):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=ring, radius=0.5,
                              matrix=xform(center, size, rot))
    add_bm(bm, mat, bone, True)


def tube(a, b, radius, mat, bone, seg=8, r2=None, smooth=True):
    a, b = Vector(a), Vector(b)
    d = b - a
    q = Vector((0, 0, 1)).rotation_difference(d.normalized())
    m = (Matrix.Translation((a + b) / 2 * PX) @ q.to_matrix().to_4x4()
         @ Matrix.Diagonal((PX, PX, PX, 1.0)))
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=seg,
                          radius1=radius, radius2=radius if r2 is None else r2,
                          depth=d.length, matrix=m)
    add_bm(bm, mat, bone, smooth)


# Legs: short steel mech legs under the case, gold feet. Hip z 18, knee 9.
for side, x in (("R", -9.0), ("L", 9.0)):
    box((x, -3.0, 2.5), (11.0, 16.0, 5.0), "WoofuGoldDark", "shin_" + side)
    box((x, -10.5, 3.5), (9.0, 3.0, 3.0), "WoofuGold", "shin_" + side)      # toe cap
    tube((x, 0, 4.0), (x, -1.0, 11.0), 3.4, "WoofuSteelDark", "shin_" + side)
    ball((x, -1.0, 10.5), (8.0, 8.0, 7.0), "WoofuSteel", "shin_" + side)      # knee
    tube((x, -1.0, 10.0), (x * 0.9, 0, 19.0), 4.0, "WoofuSteelDark", "thigh_" + side)

# Case (bone "case"): the gold mummy case, wide at the shoulders (z 68),
# narrowing to the feet (z 16) and in at the neck (z 80). The front of the
# case is open (cavity at y -7); the side walls reach y -10, just behind the
# lid (y -10.5 .. -13.5), so the gold rim frames the cavity when it is gone.
frustum(16, 68, (32, 18), (46, 22), "WoofuGold", "case", y=6.0)         # back shell
frustum(68, 80, (46, 28), (34, 22), "WoofuGold", "case", y=3.0)         # shoulders
for sx in (-1, 1):
    frustum(16, 68, (4, 12), (4, 12), "WoofuGold", "case",
            x=sx * 14.0, xtop=sx * 21.0, y=-4.0)  # side walls, following the taper
box((0, -6.0, 15.0), (34.0, 14.0, 3.0), "WoofuGoldDark", "case")        # base rim
box((0, -6.5, 67.0), (44.0, 13.0, 3.0), "WoofuGoldDark", "case")        # shoulder rim
# Blue lapis bands round the back shell (seen from the sides and back).
for z in (30.0, 46.0):
    t = (z - 16.0) / 52.0
    frustum(z - 2, z + 2, (33 + 14 * t, 19 + 4 * t), (33.5 + 14 * t, 19.5 + 4 * t),
            "WoofuLapis", "case", y=6.0)
# Cavity under the lid: steel machinery, ribs and a lapis heart.
frustum(18, 66, (26, 3), (38, 3), "WoofuSteelDark", "case", y=-4.5)
for k, z in enumerate((58.0, 52.0, 46.0, 40.0)):
    w = 30.0 - 2.5 * k
    box((0, -7.6, z), (w, 2.0, 2.4), "WoofuSteel", "case")                 # ribs
box((0, -7.8, 49.0), (3.0, 2.4, 22.0), "WoofuSteel", "case")               # spine
ball((0, -9.0, 50.0), (9.0, 6.0, 10.0), "WoofuLapis", "case")              # heart
for x in (-7.0, 7.0):
    tube((x, -7.5, 34.0), (x * 0.6, -7.5, 22.0), 1.6, "WoofuSteel", "case", seg=6)  # pistons
box((0, -7.5, 26.0), (16.0, 2.0, 6.0), "WoofuSteelDark", "case")           # pelvis block

# Lid: two halves, hinged at the outer edges (bones lid_R / lid_L), gold
# with lapis bands, a gold collar and a crossed-paws relief.
for side, sx in (("R", -1), ("L", 1)):
    b = "lid_" + side
    frustum(16, 66, (16, 3), (22, 3), "WoofuGold", b, x=sx * 8.0, xtop=sx * 11.0,
            y=-12.0, ytop=-11.5)
    frustum(66, 68, (22, 3), (22, 3), "WoofuGoldDark", b, x=sx * 11.0, y=-12.0)
    for z in (24.0, 30.0, 36.0):
        t = (z - 16.0) / 50.0
        box((sx * (8.0 + 3.0 * t), -13.8, z), (14.0 + 6.0 * t, 1.2, 2.6),
            "WoofuLapis" if z != 30.0 else "WoofuLapisDark", b)
    # Collar (wesekh): gold and lapis arcs under the lappets.
    for k, (z, m) in enumerate(((60.0, "WoofuLapis"), (56.5, "WoofuGoldDark"),
                                (53.0, "WoofuLapisDark"))):
        box((sx * 10.5, -13.6, z), (20.0 - 3.0 * k, 1.4, 3.0), m, b,
            rot=(0, sx * -12.0, 0))
    # Crossed paws on the chest, each paw from its own side across.
    tube((sx * 14.0, -13.8, 50.0), (sx * -4.0, -14.2, 43.0), 2.6, "WoofuGoldDark", b)
    ball((sx * -5.0, -14.6, 42.6), (5.4, 3.0, 4.0), "WoofuGold", b)
    box((sx * 6.0, -13.6, 46.0), (2.0, 1.0, 2.0), "WoofuLapis", b)
# Seam light (phase f0..f1: cyan light through the cracking lid).
box((0, -12.6, 42.0), (2.0, 2.0, 48.0), "WoofuGlow", "seam")
for z, rz in ((54.0, 30.0), (34.0, -35.0)):
    box((0, -13.0, z), (10.0, 1.2, 1.4), "WoofuGlow", "seam", rot=(0, rz, 0))
# Burst (phase f1..f2): cyan blast rays out of the opening.
for k in range(8):
    a = math.radians(22.5 + 45 * k)
    d = Vector((math.cos(a), 0, math.sin(a)))
    c = Vector((0, -15.0, 44.0))
    tube(tuple(c + d * 4.0), tuple(c + d * 22.0), 2.4, "WoofuGlow", "burst", seg=4, r2=0.3)
ball((0, -15.0, 44.0), (14.0, 4.0, 18.0), "WoofuGlow", "burst")

# Neck and head: narrow chrome greyhound skull, long thin snout pointing
# forward and a little down, folded rose ears, cyan eyes.
tube((0, 0, 76.0), (0, -1.0, 84.0), 4.2, "WoofuSteelDark", "head")
ball((0, 0.5, 86.0), (11.0, 14.0, 10.0), "WoofuSteel", "head")
tube((0, -5.0, 86.0), (0, -21.0, 82.5), 3.6, "WoofuSteel", "head", r2=1.8)  # snout
box((0, -12.0, 83.2), (4.6, 12.0, 2.0), "WoofuSteelDark", "head", rot=(-10, 0, 0))  # jaw
ball((0, -21.5, 82.8), (3.0, 2.4, 2.4), "WoofuSteelDark", "head", seg=8, ring=6)  # nose
for x in (-2.9, 2.9):
    ball((x, -5.6, 88.0), (2.2, 1.6, 1.8), "WoofuGlow", "head", seg=6, ring=4)
for side, sx in (("R", -1), ("L", 1)):
    box((sx * 4.0, 6.0, 92.0), (2.4, 7.0, 4.0), "WoofuSteelDark", "head",
        rot=(-30, 0, sx * 20))                                             # rose ears
# Nemes headdress: striped gold/lapis cloth over the skull, lappets that
# fall in front of the shoulders, a tail at the back, a uraeus on the brow.
ball((0, 1.5, 89.5), (15.0, 16.0, 9.0), "WoofuGold", "head")
box((0, 1.5, 88.0), (16.0, 15.0, 2.0), "WoofuLapis", "head")
for side, sx in (("R", -1), ("L", 1)):
    frustum(70, 88, (8, 4), (6, 5), "WoofuGold", "head", x=sx * 9.0, y=-5.0, ytop=-1.0)
    for z in (74.0, 79.0, 84.0):
        box((sx * 9.2, -7.4 + (z - 70) * 0.2, z), (8.4, 1.0, 1.8), "WoofuLapis", "head")
frustum(72, 88, (10, 4), (14, 6), "WoofuGold", "head", y=9.0, ytop=7.0)    # tail
box((0, -5.5, 93.0), (2.4, 2.0, 5.0), "WoofuGoldDark", "head")             # uraeus
ball((0, -6.5, 95.0), (3.2, 2.4, 2.4), "WoofuGold", "head", seg=8, ring=6)
# False beard: a pompous lapis block under the chin, banded.
frustum(70, 81, (3.4, 3.4), (4.6, 4.2), "WoofuLapis", "head", y=-15.0, ytop=-13.0)
box((0, -15.4, 70.5), (4.4, 4.0, 1.6), "WoofuGold", "head")
box((0, -14.6, 75.5), (5.0, 4.4, 1.2), "WoofuLapisDark", "head")

# Arms: chrome arms out of the case shoulders. Shoulder ball gold (pauldron),
# upper arm and forearm steel, hand dark steel.
SHOULDER = {"R": (-25.0, 0.0, 70.0), "L": (25.0, 0.0, 70.0)}
ELBOW = {"R": (-34.0, -3.0, 55.0), "L": (34.0, -3.0, 55.0)}
HAND = {"R": (-40.0, -10.0, 62.0), "L": (39.0, -8.0, 44.0)}
for s in ("R", "L"):
    ball(SHOULDER[s], (13.0, 13.0, 10.0), "WoofuGold", "uparm_" + s)
    ball(tuple(Vector(SHOULDER[s]) + Vector((0, 0, 3.0))), (12.0, 11.0, 4.0),
         "WoofuLapis", "uparm_" + s)
    tube(SHOULDER[s], ELBOW[s], 3.6, "WoofuSteel", "uparm_" + s)
    ball(ELBOW[s], (7.0, 7.0, 7.0), "WoofuSteelDark", "forearm_" + s)
    tube(ELBOW[s], HAND[s], 3.4, "WoofuSteel", "forearm_" + s)
    tube(tuple(Vector(ELBOW[s]).lerp(Vector(HAND[s]), 0.5)),
         tuple(Vector(ELBOW[s]).lerp(Vector(HAND[s]), 0.85)), 4.2, "WoofuGold", "forearm_" + s)
    ball(HAND[s], (6.4, 6.4, 6.4), "WoofuSteelDark", "forearm_" + s)

# Sceptre (bone "sceptre", held in the right hand): a tall gold staff,
# a lapis grip band, a forked foot, an angled greyhound-head top and the
# plasma orb (fb_cyan) above it.
HR = Vector(HAND["R"])
tube(tuple(HR + Vector((0, 0, -34.0))), tuple(HR + Vector((0, 0, 26.0))), 1.8,
     "WoofuGold", "sceptre", seg=6)
tube(tuple(HR + Vector((0, 0, -4.0))), tuple(HR + Vector((0, 0, 4.0))), 2.4,
     "WoofuLapis", "sceptre", seg=6)
for sx in (-1, 1):
    tube(tuple(HR + Vector((0, 0, -33.0))), tuple(HR + Vector((sx * 3.0, 0, -38.0))),
         1.2, "WoofuGoldDark", "sceptre", seg=4)
TOP = HR + Vector((0, 0, 26.0))
box(tuple(TOP + Vector((0, -2.5, 1.5))), (3.6, 8.0, 3.6), "WoofuGold", "sceptre", rot=(-20, 0, 0))
box(tuple(TOP + Vector((0, 1.5, 3.5))), (2.0, 2.0, 3.0), "WoofuGoldDark", "sceptre")
ORB = TOP + Vector((0, -2.0, 7.5))
ball(tuple(ORB), (4.0, 4.0, 4.0), "WoofuGlow", "orb", seg=8, ring=6)
# Wind-up glow (bone "glow", hidden unless posed): a bigger orb and rays.
ball(tuple(ORB), (10.0, 10.0, 10.0), "WoofuGlow", "glow", seg=10, ring=8)
for k in range(4):
    a = math.radians(45 + 90 * k)
    d = Vector((math.cos(a), 0, math.sin(a)))
    tube(tuple(ORB + d * 4.0), tuple(ORB + d * 9.0), 1.0, "WoofuGlow", "glow", seg=4, r2=0.2)
# Attack bolt (bone "bolt"): plasma burst out of the orb toward the camera.
tube(tuple(ORB), tuple(ORB + Vector((0, -9.0, 0))), 4.0, "WoofuGlow", "bolt", seg=6, r2=1.0)
for k in range(4):
    a = math.radians(90 * k)
    d = Vector((math.cos(a), 0, math.sin(a)))
    tube(tuple(ORB - Vector((0, 2.0, 0))), tuple(ORB + d * 8.0 - Vector((0, 2.0, 0))),
         1.4, "WoofuGlow", "bolt", seg=4, r2=0.2)

# Chain mace (bone "mace", left hand): a short steel handle, a chain of
# links hanging down, a gold spiked ball.
HL = Vector(HAND["L"])
tube(tuple(HL + Vector((0, 0, 4.0))), tuple(HL + Vector((0, 0, -6.0))), 1.8,
     "WoofuGoldDark", "mace", seg=6)
for k in range(5):
    c = HL + Vector((0.6 * k, 0, -8.5 - 3.2 * k))
    ball(tuple(c), (2.6, 2.0, 3.6) if k % 2 else (2.0, 2.6, 3.6), "WoofuSteelDark", "mace",
         seg=6, ring=4)
MACE = HL + Vector((3.0, 0, -30.0))
ball(tuple(MACE), (10.0, 10.0, 10.0), "WoofuGold", "mace", seg=10, ring=8)
ball(tuple(MACE), (10.6, 10.6, 2.4), "WoofuLapis", "mace", seg=10, ring=4)
for d in ((1, 0, 0), (-1, 0, 0), (0, -1, 0), (0, 1, 0), (0, 0, -1),
          (0.7, -0.7, 0.5), (-0.7, -0.7, 0.5), (0.7, 0.7, -0.5), (-0.7, 0.7, -0.5)):
    d = Vector(d).normalized()
    tube(tuple(MACE + d * 4.5), tuple(MACE + d * 8.0), 1.4, "WoofuSteel", "mace",
         seg=4, r2=0.2, smooth=False)

mesh = bpy.data.meshes.new("WoofuBody")
mesh.from_pydata(verts, [], faces)
for m in ORDER:
    mesh.materials.append(mats[m])
for poly, mi, sm in zip(mesh.polygons, face_mat, face_smooth):
    poly.material_index = mi
    poly.use_smooth = sm
mesh.validate()
mesh.update()
body = bpy.data.objects.new("WoofuBody", mesh)
bpy.context.scene.collection.objects.link(body)

# ---------------------------------------------------------------- armature
arm_data = bpy.data.armatures.new("WoofuRig")
rig = bpy.data.objects.new("Woofu", arm_data)
bpy.context.scene.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode="EDIT")
eb = arm_data.edit_bones

BONES = [
    ("root", None, (0, 0, 0)),
    ("thigh_R", "root", (-9.0, 0, 19.0)),
    ("shin_R", "thigh_R", (-9.0, -1.0, 10.5)),
    ("thigh_L", "root", (9.0, 0, 19.0)),
    ("shin_L", "thigh_L", (9.0, -1.0, 10.5)),
    ("case", "root", (0, 0, 18.0)),
    ("lid_R", "case", (-20.0, -12.0, 18.0)),
    ("lid_L", "case", (20.0, -12.0, 18.0)),
    ("seam", "case", (0, -13.0, 42.0)),
    ("burst", "case", (0, -15.0, 44.0)),
    ("head", "case", (0, 0, 76.0)),
    ("uparm_R", "case", SHOULDER["R"]),
    ("forearm_R", "uparm_R", ELBOW["R"]),
    ("sceptre", "forearm_R", HAND["R"]),
    ("orb", "sceptre", tuple(ORB)),
    ("glow", "sceptre", tuple(ORB)),
    ("bolt", "sceptre", tuple(ORB)),
    ("uparm_L", "case", SHOULDER["L"]),
    ("forearm_L", "uparm_L", ELBOW["L"]),
    ("mace", "forearm_L", tuple(HL)),
]
for name, parent, head in BONES:
    b = eb.new(name)
    b.head = V(*head)
    b.tail = V(head[0], head[1], head[2] + 4.0)
    b.roll = 0.0
    if parent:
        b.parent = eb[parent]
bpy.ops.object.mode_set(mode="OBJECT")

for name in {g for g in vgroup}:
    body.vertex_groups.new(name=name)
by_group = {}
for i, g in enumerate(vgroup):
    by_group.setdefault(g, []).append(i)
for g, idx in by_group.items():
    body.vertex_groups[g].add(idx, 1.0, "REPLACE")
body.parent = rig
mod = body.modifiers.new("Armature", "ARMATURE")
mod.object = rig

POSED = [n for n, _, _ in BONES]
FX = ("seam", "burst", "glow", "bolt")   # hidden unless a pose shows them


# ------------------------------------------------------------------ poses
def to_local(pb, rot_deg=(0, 0, 0), loc=(0, 0, 0)):
    b = pb.bone.matrix_local.to_3x3()
    bi = b.inverted()
    rw = Euler(tuple(math.radians(a) for a in rot_deg)).to_matrix()
    return (bi @ rw @ b).to_quaternion(), bi @ V(*loc)


def pose(spec, lid):
    """spec: {bone: {"rot": (x,y,z) deg, "loc": (x,y,z) px, "scale": s}}
    relative to the parent; unlisted bones rest. lid=False hides the lid."""
    for name in POSED:
        pb = rig.pose.bones[name]
        pb.rotation_mode = "QUATERNION"
        s = spec.get(name, {})
        q, loc = to_local(pb, s.get("rot", (0, 0, 0)), s.get("loc", (0, 0, 0)))
        pb.rotation_quaternion = q
        pb.location = loc
        sc = s.get("scale", 1.0)
        if name in FX and "scale" not in s:
            sc = HIDE
        if name.startswith("lid_") and not lid:
            sc = HIDE
        pb.scale = (sc, sc, sc)


def walk(phase):
    """Contact (0/2) and passing (1/3): a heavy rolling stomp."""
    sw = {0: 1, 1: 0, 2: -1, 3: 0}[phase]
    lift = {0: 0, 1: 1.5, 2: 0, 3: 1.5}[phase]
    sp = {"root": {"loc": (0, 0, lift)}}
    sp["thigh_R"] = {"rot": (-22 * sw, 0, 0)}
    sp["thigh_L"] = {"rot": (22 * sw, 0, 0)}
    if phase in (1, 3):
        back = "R" if phase == 1 else "L"
        sp["thigh_" + back] = {"rot": (-14, 0, 0)}
        sp["shin_" + back] = {"rot": (36, 0, 0)}
    sp["case"] = {"rot": (0, 3 * sw, 4 * sw)}
    sp["head"] = {"rot": (0, 0, -3 * sw)}
    sp["uparm_L"] = {"rot": (-18 * sw, 0, 0)}
    sp["mace"] = {"rot": (16 * sw, 0, 0)}
    sp["uparm_R"] = {"rot": (6 * sw, 0, 0)}
    return sp


IDLE = [
    {"head": {"rot": (4, 0, 0)}, "mace": {"rot": (0, -4, 0)}},
    {"case": {"loc": (0, 0, 1)}, "head": {"rot": (-2, 0, 2)},
     "mace": {"rot": (0, 4, 0)}, "uparm_R": {"rot": (0, 0, 0), "loc": (0, 0, 1)}},
]
WINDUP = [
    {"uparm_R": {"rot": (-10, -20, 0)}, "forearm_R": {"rot": (-10, 0, 0)},
     "head": {"rot": (-6, 0, 0)}, "case": {"rot": (-3, 0, -4)}},
    {"uparm_R": {"rot": (-18, -32, 0)}, "forearm_R": {"rot": (-14, 0, 0)},
     "sceptre": {"rot": (0, 10, 0)}, "head": {"rot": (-10, 0, 0)},
     "case": {"rot": (-5, 0, -6)}, "glow": {"scale": 1.0},
     "uparm_L": {"rot": (0, 10, 0)}},
]
ATTACK = [
    {"uparm_R": {"rot": (-34, -10, 0)}, "forearm_R": {"rot": (-14, 0, 0)},
     "sceptre": {"rot": (-18, 0, 0)}, "head": {"rot": (6, 0, 0)},
     "case": {"rot": (6, 0, -4)}, "bolt": {"scale": 1.0}, "glow": {"scale": 0.7},
     "uparm_L": {"rot": (10, 0, 0)}},
    {"uparm_R": {"rot": (-24, -6, 0)}, "forearm_R": {"rot": (-6, 0, 0)},
     "sceptre": {"rot": (-10, 0, 0)}, "head": {"rot": (2, 0, 0)},
     "case": {"rot": (2, 0, -2)}},
]
PAIN = [
    {"case": {"rot": (-8, 0, 6)}, "head": {"rot": (-16, 8, 0)},
     "uparm_R": {"rot": (10, -12, 0)}, "uparm_L": {"rot": (0, 18, 0)},
     "mace": {"rot": (0, -20, 0)}, "thigh_R": {"rot": (-8, 0, 0)},
     "shin_R": {"rot": (10, 0, 0)}, "root": {"loc": (0, 2, 0)}},
]
# Lid halves swing about their outer hinges (z rotation), then fly off.
PHASE = [
    # f0: shudder, the lid cracks: cyan light through the seam.
    {"case": {"rot": (-4, 0, 3)}, "head": {"rot": (-12, 0, 0)},
     "seam": {"scale": 1.0}, "lid_R": {"rot": (0, 0, 4)}, "lid_L": {"rot": (0, 0, -4)},
     "uparm_R": {"rot": (0, -10, 0)}, "uparm_L": {"rot": (0, 10, 0)}},
    # f1: the halves burst open on their hinges, blast of light.
    {"case": {"rot": (-8, 0, 0)}, "head": {"rot": (-20, 0, 0)}, "seam": {"scale": 1.0},
     "burst": {"scale": 0.7},
     "lid_R": {"rot": (0, 0, 40), "loc": (0, -3, 0)},
     "lid_L": {"rot": (0, 0, -40), "loc": (0, -3, 0)},
     "uparm_R": {"rot": (0, -22, 0)}, "uparm_L": {"rot": (0, 22, 0)}},
    # f2: the halves are blown off, tumbling outward and up.
    {"case": {"rot": (-10, 0, 0)}, "head": {"rot": (-26, 0, 0)}, "burst": {"scale": 1.0},
     "lid_R": {"rot": (0, -30, 70), "loc": (-16, -8, 10)},
     "lid_L": {"rot": (0, 30, -70), "loc": (16, -8, 10)},
     "uparm_R": {"rot": (0, -30, 0)}, "uparm_L": {"rot": (0, 30, 0)},
     "mace": {"rot": (0, 30, 0)}},
    # f3: halves far out and falling, burst fading.
    {"case": {"rot": (-6, 0, 0)}, "head": {"rot": (-22, 0, 4)}, "burst": {"scale": 0.5},
     "lid_R": {"rot": (0, -110, 90), "loc": (-12, -6, -4), "scale": 0.7},
     "lid_L": {"rot": (0, 110, -90), "loc": (12, -6, -4), "scale": 0.7},
     "uparm_R": {"rot": (0, -20, 0)}, "uparm_L": {"rot": (0, 22, 0)},
     "mace": {"rot": (0, 18, 0)}},
    # f4: the halves are gone (blown away), the case rocks forward, the
    # last of the blast flickers in the cavity.
    {"case": {"rot": (6, 0, 0)}, "head": {"rot": (-4, 0, 0)}, "burst": {"scale": 0.3},
     "lid_R": {"scale": HIDE}, "lid_L": {"scale": HIDE},
     "uparm_R": {"rot": (0, -8, 0)}, "uparm_L": {"rot": (0, 8, 0)}},
    # f5: unleashed: chest out, head up, the lid gone.
    {"case": {"rot": (-4, 0, 0), "loc": (0, 0, 1)}, "head": {"rot": (-14, 0, 0)},
     "lid_R": {"scale": HIDE}, "lid_L": {"scale": HIDE},
     "uparm_R": {"rot": (0, -14, 0)}, "uparm_L": {"rot": (0, 16, 0)},
     "mace": {"rot": (0, 12, 0)}},
]
DEATH = [
    # f0: hit, staggering back, sparks out of the heart.
    {"case": {"rot": (-10, 0, 6)}, "head": {"rot": (-24, 10, 0)},
     "burst": {"scale": 0.6}, "uparm_R": {"rot": (20, -20, 0)},
     "uparm_L": {"rot": (0, 30, 0)}, "root": {"loc": (0, 3, 0)}},
    # f1: knees buckle, the sceptre slips.
    {"root": {"loc": (0, 4, -4)}, "thigh_R": {"rot": (-30, 0, 0)},
     "thigh_L": {"rot": (-26, 0, 0)}, "shin_R": {"rot": (55, 0, 0)},
     "shin_L": {"rot": (50, 0, 0)}, "case": {"rot": (-12, 0, -8)},
     "head": {"rot": (30, 0, -10)}, "uparm_R": {"rot": (30, -10, 0)},
     "sceptre": {"rot": (0, 30, 0)}, "uparm_L": {"rot": (14, 10, 0)}},
    # f2: toppling backwards.
    {"root": {"rot": (-35, 0, 0), "loc": (0, -12, 2)}, "thigh_R": {"rot": (-10, 0, 0)},
     "thigh_L": {"rot": (-4, 0, 0)}, "shin_R": {"rot": (40, 0, 0)},
     "shin_L": {"rot": (30, 0, 0)}, "head": {"rot": (34, 0, 10)},
     "uparm_R": {"rot": (60, -40, 0)}, "sceptre": {"rot": (0, 50, 0)},
     "uparm_L": {"rot": (40, 30, 0)}},
    # f3: crashing down on its back.
    {"root": {"rot": (-72, 0, 0), "loc": (0, -38, 15)}, "thigh_R": {"rot": (20, 0, 0)},
     "thigh_L": {"rot": (30, 0, 0)}, "shin_R": {"rot": (30, 0, 0)},
     "head": {"rot": (40, 0, 20)}, "uparm_R": {"rot": (70, -40, 0)},
     "sceptre": {"rot": (0, 80, 0)}, "uparm_L": {"rot": (60, 40, 0)}},
    # f4: the wreck, flat on its back, head lolling, arms out.
    {"root": {"rot": (-90, 0, 0), "loc": (0, -46, 18)}, "thigh_R": {"rot": (40, 0, -10)},
     "thigh_L": {"rot": (34, 0, 12)}, "shin_R": {"rot": (20, 0, 0)},
     "head": {"rot": (30, 0, 40)}, "uparm_R": {"rot": (80, -60, 0)},
     "sceptre": {"rot": (0, 90, 0)}, "uparm_L": {"rot": (80, 60, 0)},
     "mace": {"rot": (-60, 0, 0)}},
]

if LIDLESS:
    POSES = {"idle": IDLE, "walk": [walk(k) for k in range(4)], "windup": WINDUP,
             "attack": ATTACK, "pain": PAIN}
else:
    POSES = {"idle": IDLE, "walk": [walk(k) for k in range(4)], "windup": WINDUP,
             "attack": ATTACK, "pain": PAIN, "phase": PHASE, "death": DEATH}
# The boss dies in phase 2 only, so the death frames are lidless too.
LIDDED = set() if LIDLESS else {"idle", "walk", "windup", "attack", "pain", "phase"}
PREFIX = "Woofu2" if LIDLESS else "Woofu"

scene = bpy.context.scene
rig.animation_data_create()
for anim, frames in POSES.items():
    rig.animation_data.action = None
    for i, spec in enumerate(frames):
        f = i + 1
        pose(spec, anim in LIDDED)
        for name in POSED:
            pb = rig.pose.bones[name]
            for path in ("location", "rotation_quaternion", "scale"):
                pb.keyframe_insert(path, frame=f)
    act = rig.animation_data.action
    act.name = PREFIX + anim.capitalize()
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
scene.frame_start, scene.frame_end = 1, 6

blend = os.path.join(out_dir, NAME + ".blend")
bpy.ops.wm.save_as_mainfile(filepath=blend)
shutil.copy(os.path.join(HERE, "..", NAME, "sprite.json"), os.path.join(out_dir, "sprite.json"))
print("%s model: wrote %s (%d verts, %d faces)" % (NAME, blend, len(verts), len(faces)))
