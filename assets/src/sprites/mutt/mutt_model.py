"""Mutt Trooper model and animations (docs/design/enemies.md, Mutt Trooper).

    blender -b --factory-startup --python assets/src/sprites/mutt/mutt_model.py -- <out_dir>

writes <out_dir>/mutt.blend (tools/build.d/60-sprites.sh runs this, copies
sprite.json next to it and renders with tools/blender_render.py).

Look: a basset-ish dog-man, upright, in an olive armour vest and olive
fatigues, beige fur, long droopy dark-beige ears hanging out under a small
olive helmet, a long steel rifle held diagonally across the chest (port
arms). Wind-up: the rifle comes up to the shoulder and a fullbright red LED
under the muzzle lights (fb_red). Attack: muzzle flash (fb_yellow) then
recoil.

Rig: one armature "Mutt" (the object that gets the actions) and one mesh
skinned rigidly to it: every part's vertices belong 100% to one bone, so
parts move as solid pieces (pixel art at 32x56 has no room for soft
deformation). Every bone that is posed by hand points along +Z with roll 0,
so its rest frame is fixed; poses are written below in world axes relative
to the parent bone (helper `pose`). The arms are 2-bone IK chains whose
targets ride on the rifle, so the hands stay on the rifle in every pose.

All lengths are in pixels (1 world unit = 64 px, art-bible.md); the model
faces -Y and stands on z = 0 (tools/blender_render.py convention). The
character's right side is -X (seen from the d0 camera it is on the left).
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Euler, Matrix, Quaternion, Vector

out_dir = sys.argv[sys.argv.index("--") + 1]
os.makedirs(out_dir, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)

PX = 1.0 / 64.0
HIDE = 0.001  # scale of a hidden part (LED, flash): smaller than a pixel


def V(x, y, z):
    return Vector((x * PX, y * PX, z * PX))


# --------------------------------------------------------------- materials
# Base colours are sRGB; the ramp each maps to is in sprite.json. Lit faces
# render at the base colour (blender_render.py exposure), so each sits in the
# upper half of its ramp.
MATS = {
    "MuttVest": (0.62, 0.64, 0.36),      # olive: vest, helmet
    "MuttCloth": (0.46, 0.48, 0.27),     # olive: fatigues, pouches
    "MuttFur": (0.72, 0.64, 0.52),       # beige: tan fur
    "MuttMuzzle": (0.80, 0.74, 0.64),    # beige: lighter muzzle and jowls
    "MuttEar": (0.44, 0.36, 0.28),       # beige: dark droopy ears
    "MuttDark": (0.42, 0.36, 0.29),      # beige_03..05: nose, eyes
    "MuttGun": (0.36, 0.41, 0.49),       # steel: rifle body, boots
    "MuttMetal": (0.66, 0.72, 0.80),     # steel: barrel, buckles
    "MuttLED": (1.0, 0.12, 0.12),        # fb_red
    "MuttFlash": (1.0, 0.94, 0.25),      # fb_yellow
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
    """Matrix: unit primitive -> size (px, full extents) at center (px),
    rotated by Euler degrees (world axes, XYZ)."""
    s = Matrix.Diagonal((size[0] * PX, size[1] * PX, size[2] * PX, 1.0))
    r = Euler(tuple(math.radians(a) for a in rot)).to_matrix().to_4x4()
    return Matrix.Translation(V(*center)) @ r @ s


def box(center, size, mat, bone, rot=(0, 0, 0)):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0, matrix=xform(center, size, rot))
    add_bm(bm, mat, bone, False)


def ball(center, size, mat, bone, rot=(0, 0, 0), seg=12, ring=8):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=ring, radius=0.5,
                              matrix=xform(center, size, rot))
    add_bm(bm, mat, bone, True)


def tube(a, b, radius, mat, bone, seg=8, r2=None):
    """Cylinder (or cone with r2) from point a to point b (px)."""
    a, b = Vector(a), Vector(b)
    d = b - a
    q = Vector((0, 0, 1)).rotation_difference(d.normalized())
    m = (Matrix.Translation((a + b) / 2 * PX) @ q.to_matrix().to_4x4()
         @ Matrix.Diagonal((PX, PX, PX, 1.0)))
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=seg,
                          radius1=radius, radius2=radius if r2 is None else r2,
                          depth=d.length, matrix=m)
    add_bm(bm, mat, bone, True)


# Legs: boots (steel), shins and thighs (olive fatigues). Hip z 25, knee 13.
for side, x in (("R", -4.5), ("L", 4.5)):
    box((x, -1.5, 2.0), (6.5, 10.0, 4.0), "MuttGun", "shin_" + side)
    tube((x, 0, 3.5), (x, 0, 14.0), 2.9, "MuttCloth", "shin_" + side)
    ball((x, -0.6, 13.5), (6.6, 6.6, 6.0), "MuttCloth", "shin_" + side)
    tube((x, 0, 13.0), (x * 0.95, 0, 26.0), 3.4, "MuttCloth", "thigh_" + side)

# Hips and belt.
box((0, 0, 25.5), (17.0, 11.0, 6.0), "MuttCloth", "hips")
box((0, 0, 28.0), (19.0, 12.5, 2.4), "MuttGun", "hips")
box((0, -6.4, 28.0), (3.6, 1.2, 3.0), "MuttMetal", "hips")      # buckle
for x in (-6.0, 6.0):
    box((x, -6.6, 27.0), (4.0, 2.4, 4.0), "MuttCloth", "hips")  # pouches

# Torso: fur underneath, olive armour vest (front plate, back plate,
# shoulder pads), so the vest is the dominant read.
ball((0, 0, 35.5), (17.0, 11.5, 15.0), "MuttFur", "chest")
box((0, -1.0, 34.4), (19.0, 11.0, 11.4), "MuttVest", "chest")
box((0, -6.4, 35.0), (15.0, 2.2, 9.6), "MuttVest", "chest")     # front plate
box((0, 5.6, 36.0), (15.0, 1.8, 10.0), "MuttVest", "chest")      # back plate
for x in (-9.5, 9.5):
    ball((x, 0.5, 39.6), (7.0, 9.0, 5.0), "MuttVest", "chest")    # shoulder pads
box((0, -7.7, 37.0), (9.0, 0.8, 2.0), "MuttGun", "chest")         # chest strap
box((0, 8.2, 36.0), (10.0, 4.0, 8.0), "MuttCloth", "chest")       # pack
box((0, 10.4, 33.0), (8.0, 1.0, 1.2), "MuttGun", "chest")         # pack strap
for x in (-5.5, 5.5):
    box((x, 7.0, 31.0), (3.0, 2.6, 3.4), "MuttCloth", "chest")    # kidney pouches
# Neck.
tube((0, 0, 40.0), (0, -0.5, 45.0), 3.4, "MuttFur", "chest")

# Head: round skull, long basset muzzle with jowls, dark nose and eyes,
# small olive helmet; long dark ears hang on both sides.
# The muzzle is long and level (a basset's), the fur-coloured bridge on
# top, the lighter muzzle under it; the nose is a small dark knob at the
# tip; the jowls hang 2-3 px below the jaw.
ball((0, 0.5, 47.0), (11.0, 11.0, 10.0), "MuttFur", "head")
box((0, -7.0, 46.6), (5.4, 11.0, 3.4), "MuttFur", "head", rot=(4, 0, 0))   # bridge
ball((0, -8.0, 44.2), (6.6, 12.0, 4.6), "MuttMuzzle", "head")              # muzzle
ball((0, -6.5, 42.2), (7.4, 8.0, 4.4), "MuttMuzzle", "head")             # jowls
ball((0, -14.0, 46.6), (2.0, 1.4, 1.1), "MuttDark", "head", seg=8, ring=6)  # nose
for x in (-2.6, 2.6):
    ball((x, -4.8, 48.2), (1.8, 1.2, 1.6), "MuttDark", "head", seg=6, ring=4)
    box((x, -4.6, 49.6), (3.0, 1.2, 1.0), "MuttFur", "head")        # droopy brow
ball((0, 1.5, 51.5), (12.4, 12.0, 8.0), "MuttVest", "head")         # helmet
box((0, 1.5, 50.4), (13.0, 12.6, 1.4), "MuttVest", "head")          # helmet rim
# Ears: long soft flaps hanging from under the rim beside the jaw, turned
# 35 deg toward the front so they show as flaps (not edges) from d0, top
# under the rim (z 48.8), tip below the jowls (z 30.4).
for side, x in (("R", -7.0), ("L", 7.0)):
    ball((x, 0.2, 39.6), (2.6, 6.4, 18.4), "MuttEar", "ear_" + side,
         rot=(-18, 0, -40 if x > 0 else 40))

# Arms: upper arm (vest-coloured sleeve), forearm and fur hand. Rest pose =
# port arms, the IK chains just reach their targets.
SHOULDER = {"R": (-11.5, 0.0, 40.0), "L": (11.5, 0.0, 40.0)}
ELBOW = {"R": (-13.0, -2.5, 31.5), "L": (14.0, -4.0, 30.5)}
HAND = {"R": (-5.5, -9.0, 28.0), "L": (5.5, -10.0, 35.5)}
for s in ("R", "L"):
    tube(SHOULDER[s], ELBOW[s], 2.7, "MuttFur", "uparm_" + s)
    tube(ELBOW[s], HAND[s], 2.4, "MuttFur", "forearm_" + s)
    tube(tuple(Vector(ELBOW[s]).lerp(Vector(HAND[s]), 0.55)),
         tuple(Vector(ELBOW[s]).lerp(Vector(HAND[s]), 0.85)), 2.8, "MuttGun", "forearm_" + s)
    ball(HAND[s], (4.6, 4.6, 4.6), "MuttFur", "forearm_" + s)

# Rifle: grip at the right hand, muzzle up past the left shoulder. Built
# along its own axis then placed: GUN_DIR is the barrel direction.
GRIP = Vector(HAND["R"])
GUN_DIR = (Vector(HAND["L"]) - GRIP).normalized()
GUN_UP = GUN_DIR.cross(Vector((0, 1, 0))).normalized()  # top of the rifle
if GUN_UP.z < 0:
    GUN_UP = -GUN_UP
GUN_SIDE = GUN_DIR.cross(GUN_UP)


def gun_point(a, u=0.0, s=0.0):
    """Point a px along the barrel from the grip, u px to the rifle's top."""
    return tuple(GRIP + GUN_DIR * a + GUN_UP * u + GUN_SIDE * s)


def gun_rot():
    m = Matrix((GUN_SIDE, GUN_DIR, GUN_UP)).transposed()  # columns x,y,z
    return m.to_euler()


GROT = tuple(math.degrees(a) for a in gun_rot())
box(gun_point(-6.5, -0.5), (2.6, 9.0, 4.6), "MuttGun", "gun", GROT)    # stock
box(gun_point(3.5, 0.6), (3.0, 12.0, 4.0), "MuttGun", "gun", GROT)     # receiver
box(gun_point(4.0, -3.4), (2.2, 3.0, 4.0), "MuttGun", "gun", GROT)     # magazine
box(gun_point(12.0, 0.6), (3.2, 6.0, 3.0), "MuttGun", "gun", GROT)     # handguard
box(gun_point(5.0, 3.2), (1.6, 6.0, 1.4), "MuttMetal", "gun", GROT)    # sight rail
tube(gun_point(14.0, 1.0), gun_point(21.0, 1.0), 0.9, "MuttMetal", "gun", seg=6)
tube(gun_point(20.0, 1.0), gun_point(22.0, 1.0), 1.3, "MuttGun", "gun", seg=6)
MUZZLE = Vector(gun_point(22.5, 1.0))
LED = Vector(gun_point(20.5, -1.2))
box(tuple(LED), (2.0, 1.6, 1.6), "MuttGun", "gun", GROT)                # LED housing
ball(tuple(LED + GUN_UP * -0.4 - GUN_DIR * 0.0), (2.6, 2.6, 2.6), "MuttLED", "led")
# Muzzle flash: a 4-point star around the barrel axis plus a cone forward.
tube(tuple(MUZZLE), tuple(MUZZLE + GUN_DIR * 3.5), 2.4, "MuttFlash", "flash", seg=6, r2=0.4)
for k in range(4):
    a = math.radians(45 + 90 * k)
    off = GUN_UP * math.cos(a) + GUN_SIDE * math.sin(a)
    tube(tuple(MUZZLE + GUN_DIR * 0.5), tuple(MUZZLE + GUN_DIR * 1.0 + off * 4.0),
         1.1, "MuttFlash", "flash", seg=4, r2=0.2)

mesh = bpy.data.meshes.new("MuttBody")
mesh.from_pydata(verts, [], faces)
for m in ORDER:
    mesh.materials.append(mats[m])
for poly, mi, sm in zip(mesh.polygons, face_mat, face_smooth):
    poly.material_index = mi
    poly.use_smooth = sm
mesh.validate()
mesh.update()
body = bpy.data.objects.new("MuttBody", mesh)
bpy.context.scene.collection.objects.link(body)

# ---------------------------------------------------------------- armature
arm_data = bpy.data.armatures.new("MuttRig")
rig = bpy.data.objects.new("Mutt", arm_data)
bpy.context.scene.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode="EDIT")
eb = arm_data.edit_bones

# Hand-posed bones: (name, parent, head px). All point +Z, roll 0.
UP_BONES = [
    ("root", None, (0, 0, 0)),
    ("hips", "root", (0, 0, 25.0)),
    ("thigh_R", "hips", (-4.5, 0, 25.0)),
    ("shin_R", "thigh_R", (-4.5, 0, 13.5)),
    ("thigh_L", "hips", (4.5, 0, 25.0)),
    ("shin_L", "thigh_L", (4.5, 0, 13.5)),
    ("chest", "hips", (0, 0, 29.0)),
    ("head", "chest", (0, 0, 43.0)),
    ("ear_R", "head", (-7.0, 1.2, 48.0)),
    ("ear_L", "head", (7.0, 1.2, 48.0)),
    ("gun", "chest", tuple(GRIP)),
    ("led", "gun", tuple(LED)),
    ("flash", "gun", tuple(MUZZLE)),
    ("hand_R_ik", "gun", HAND["R"]),
    ("hand_L_ik", "gun", HAND["L"]),
]
for name, parent, head in UP_BONES:
    b = eb.new(name)
    b.head = V(*head)
    b.tail = V(head[0], head[1], head[2] + 4.0)
    b.roll = 0.0
    if parent:
        b.parent = eb[parent]
for s in ("R", "L"):
    u = eb.new("uparm_" + s)
    u.head, u.tail = V(*SHOULDER[s]), V(*ELBOW[s])
    u.parent = eb["chest"]
    f = eb.new("forearm_" + s)
    f.head, f.tail = V(*ELBOW[s]), V(*HAND[s])
    f.parent = u
    f.use_connect = True
bpy.ops.object.mode_set(mode="OBJECT")

for s in ("R", "L"):
    c = rig.pose.bones["forearm_" + s].constraints.new("IK")
    c.target = rig
    c.subtarget = "hand_%s_ik" % s
    c.chain_count = 2
    c.use_tail = True

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

POSED = [n for n, _, _ in UP_BONES]


# ------------------------------------------------------------------ poses
def to_local(pb, rot_deg=(0, 0, 0), loc=(0, 0, 0)):
    """World-axis rotation (Euler degrees XYZ, relative to the parent) and
    offset (px) -> the bone's local rest frame."""
    b = pb.bone.matrix_local.to_3x3()
    bi = b.inverted()
    rw = Euler(tuple(math.radians(a) for a in rot_deg)).to_matrix()
    return (bi @ rw @ b).to_quaternion(), bi @ V(*loc)


def quat_local(pb, qw):
    b = pb.bone.matrix_local.to_3x3()
    return (b.inverted() @ qw.to_matrix() @ b).to_quaternion()


# Wind-up/attack: the rifle swings from port arms to the shoulder, barrel
# level, pointing at the d0 camera (-Y). AIM is that rotation (world axes),
# AIM_LOC moves the grip from the hip up to the right shoulder pocket.
AIM = GUN_DIR.rotation_difference(Vector((0, -1, 0)))
# Keep the rifle's top up after the swing.
up_after = AIM @ GUN_UP
roll = math.atan2(up_after.x, up_after.z)
AIM = Quaternion(Vector((0, -1, 0)), roll) @ AIM
AIM_GRIP = Vector((-5.0, 2.0, 38.5))


def aim_pose(t, kick=0.0):
    """Gun rotation and offset for a raise fraction t (0 = port arms)."""
    q = Quaternion().slerp(AIM, t)
    if kick:
        q = Quaternion(Vector((1, 0, 0)), math.radians(-kick)) @ q
    grip = GRIP.lerp(AIM_GRIP, t)
    return q, grip - GRIP


def pose(spec):
    """spec: {bone: {"rot": (x,y,z) deg, "loc": (x,y,z) px, "q": Quaternion
    (world), "scale": s}} relative to the parent; unlisted bones rest."""
    for name in POSED:
        pb = rig.pose.bones[name]
        pb.rotation_mode = "QUATERNION"
        s = spec.get(name, {})
        q, loc = to_local(pb, s.get("rot", (0, 0, 0)), s.get("loc", (0, 0, 0)))
        if "q" in s:
            q = quat_local(pb, s["q"])
        if "locv" in s:
            loc = pb.bone.matrix_local.to_3x3().inverted() @ (s["locv"] * PX)
        pb.rotation_quaternion = q
        pb.location = loc
        sc = s.get("scale", 1.0)
        if name in ("led", "flash") and "scale" not in s:
            sc = HIDE
        pb.scale = (sc, sc, sc)


def gun(t, kick=0.0, back=0.0):
    q, off = aim_pose(t, kick)
    return {"q": q, "locv": off + Vector((0, back, 0))}


def walk(phase):
    """Contact (phase 0/2) and passing (1/3) poses."""
    sw = {0: 1, 1: 0, 2: -1, 3: 0}[phase]
    lift = {0: 0, 1: 1, 2: 0, 3: 1}[phase]
    sp = {}
    sp["root"] = {"loc": (0, 0, lift)}
    # sw = 1: right leg forward (-X rotation swings a leg toward -Y).
    sp["thigh_R"] = {"rot": (-24 * sw, 0, 0)}
    sp["thigh_L"] = {"rot": (24 * sw, 0, 0)}
    if phase in (1, 3):
        back = "R" if phase == 1 else "L"
        sp["thigh_" + back] = {"rot": (-10, 0, 0)}
        sp["shin_" + back] = {"rot": (40, 0, 0)}
    else:
        sp["shin_R" if sw < 0 else "shin_L"] = {"rot": (18, 0, 0)}
    sp["hips"] = {"rot": (0, 0, 5 * sw)}
    sp["chest"] = {"rot": (4, 0, -8 * sw)}
    sp["head"] = {"rot": (0, 0, 4 * sw)}
    sp["ear_R"] = {"rot": (0, 8 * sw, 0)}
    sp["ear_L"] = {"rot": (0, 8 * sw, 0)}
    return sp


POSES = {
    "idle": [
        {"head": {"rot": (6, 0, 0)}},
        {"chest": {"loc": (0, 0, 1)}, "head": {"rot": (2, 0, 0)},
         "ear_R": {"rot": (0, -6, 0)}, "ear_L": {"rot": (0, 6, 0)}},
    ],
    "walk": [walk(0), walk(1), walk(2), walk(3)],
    "windup": [
        {"gun": gun(0.55), "chest": {"rot": (4, 0, 6)}, "head": {"rot": (4, 0, 0)}},
        {"gun": gun(1.0), "chest": {"rot": (6, 0, 10)}, "head": {"rot": (8, 0, -6)},
         "led": {"scale": 1.0}, "ear_R": {"rot": (0, -6, 0)}},
    ],
    "attack": [
        {"gun": gun(1.0, kick=4, back=1.5), "chest": {"rot": (2, 0, 10)},
         "head": {"rot": (6, 0, -6)}, "flash": {"scale": 1.0}, "led": {"scale": 1.0},
         "ear_R": {"rot": (0, -12, 0)}, "ear_L": {"rot": (0, 12, 0)}},
        {"gun": gun(0.9, kick=14, back=2.5), "chest": {"rot": (-4, 0, 8)},
         "head": {"rot": (-4, 0, -4)}},
    ],
    "pain": [
        {"chest": {"rot": (-14, 0, -8)}, "head": {"rot": (-22, 10, 0)},
         "hips": {"loc": (0, 2, 0)}, "gun": gun(0.0, kick=-12),
         "ear_R": {"rot": (0, -30, 0)}, "ear_L": {"rot": (0, 34, 0)},
         "thigh_R": {"rot": (-10, 0, 0)}, "shin_R": {"rot": (14, 0, 0)}},
    ],
    "death": [
        # f0: hit, thrown back, rifle flung up.
        {"chest": {"rot": (-20, 0, 6)}, "head": {"rot": (-30, 0, 0)},
         "hips": {"loc": (0, 3, 0)}, "gun": {"rot": (-30, 0, 0), "locv": Vector((0, 0, 3))},
         "ear_R": {"rot": (0, -35, 0)}, "ear_L": {"rot": (0, 35, 0)}},
        # f1: knees buckle.
        {"root": {"loc": (0, 6, -5)}, "thigh_R": {"rot": (-38, 0, 0)},
         "thigh_L": {"rot": (-30, 0, 0)}, "shin_R": {"rot": (70, 0, 0)},
         "shin_L": {"rot": (60, 0, 0)}, "chest": {"rot": (14, 0, 0)},
         "head": {"rot": (24, 0, 0)}, "gun": {"rot": (40, 0, 0)},
         "ear_R": {"rot": (-34, -6, 0)}, "ear_L": {"rot": (-34, 6, 0)}},
        # f2: on the knees, slumping, rifle sliding down.
        {"root": {"loc": (0, 7, -11)}, "thigh_R": {"rot": (-14, 0, 0)},
         "thigh_L": {"rot": (-8, 0, 0)}, "shin_R": {"rot": (95, 0, 0)},
         "shin_L": {"rot": (92, 0, 0)}, "chest": {"rot": (26, 0, 8)},
         "head": {"rot": (34, 0, 10)}, "gun": {"rot": (70, 0, 0), "locv": Vector((0, 4, -4))},
         "ear_R": {"rot": (-54, 0, 0)}, "ear_L": {"rot": (-54, 0, 0)}},
        # f3: pitching forward off the knees (thighs stay upright, the body
        # tilts 50 deg); the ears still hang down from the head.
        {"root": {"rot": (50, 0, 0), "loc": (0, 24, -3)}, "thigh_R": {"rot": (-48, 0, 0)},
         "thigh_L": {"rot": (-42, 0, 0)}, "shin_R": {"rot": (95, 0, 0)},
         "shin_L": {"rot": (92, 0, 0)}, "chest": {"rot": (10, 0, 6)},
         "head": {"rot": (14, 0, 16)}, "gun": {"rot": (30, 0, 0), "locv": Vector((0, 2, -4))},
         "ear_R": {"rot": (-66, 0, 0)}, "ear_L": {"rot": (-66, 0, 0)}},
        # f4: corpse face down, head turned to its left so the snout shows
        # in profile, legs folded out to the sides (frog) so the body is
        # short enough for every view of the 48 px canvas; ears lie flat on
        # the floor beside the head; the rifle lies beside the body.
        {"root": {"rot": (90, 0, 0), "loc": (0, 33, 8)}, "thigh_R": {"rot": (0, 70, 0)},
         "thigh_L": {"rot": (0, -70, 0)},
         # shins fold back in the floor plane, twisted 90 deg about the
         # leg so the boots lie flat on their sides instead of standing up.
         "shin_R": {"q": Quaternion((0, 1, 0), math.radians(-125))
                         @ Quaternion((0, 0, 1), math.radians(90))},
         "shin_L": {"q": Quaternion((0, 1, 0), math.radians(125))
                         @ Quaternion((0, 0, 1), math.radians(-90))}, "chest": {"rot": (0, 0, 8)},
         "head": {"rot": (-6, 0, 72)}, "gun": {"rot": (0, 0, 20), "locv": Vector((9, 4, -6))},
         "ear_R": {"rot": (0, 0, 0)}, "ear_L": {"rot": (-50, 0, 0)}},
    ],
}

ACTION_NAME = {"idle": "MuttIdle", "walk": "MuttWalk", "windup": "MuttWindup",
               "attack": "MuttAttack", "pain": "MuttPain", "death": "MuttDeath"}

scene = bpy.context.scene
rig.animation_data_create()
for anim, frames in POSES.items():
    rig.animation_data.action = None
    for i, spec in enumerate(frames):
        f = i + 1
        pose(spec)
        for name in POSED:
            pb = rig.pose.bones[name]
            for path in ("location", "rotation_quaternion", "scale"):
                pb.keyframe_insert(path, frame=f)
    act = rig.animation_data.action
    act.name = ACTION_NAME[anim]
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
scene.frame_start, scene.frame_end = 1, 5

blend = os.path.join(out_dir, "mutt.blend")
bpy.ops.wm.save_as_mainfile(filepath=blend)
print("mutt_model: wrote %s (%d verts, %d faces)" % (blend, len(verts), len(faces)))
