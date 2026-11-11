"""Shepherd Shieldbearer model and animations (docs/design/enemies.md,
Shepherd Shieldbearer).

    blender -b --factory-startup --python assets/src/sprites/shep/model.py -- <out_dir>

writes <out_dir>/shep.blend and copies sprite.json next to it
(tools/build.d/60-sprites.sh runs this, then renders with
tools/blender_render.py).

Look: an upright black-and-tan German shepherd (black saddle, mask and tall
pointed ears in the dark end of `brown`; tan face, chest, forearms and paws
in `beige`) in navy riot gear (`blue`: vest, pants, shin guards, helmet),
holding a tall curved riot shield (light `blue` face shaded across its
curve and bevels, dark vision slot, paw emblem and emitter rib, `fb_blue`
rim) in front with the left arm, its top below the muzzle so the
black-and-tan face reads, and a shock baton in the right hand. The
silhouette is the art-bible's: upright, two tall ears, a tall rectangle in
front, plus a bushy tail behind in side views.

Wind-up (the telegraph, enemies.md): the shield swings aside like a door
hinged at its outer edge, exposing the front, and the baton goes up over
the right shoulder; on windup f1 the baton tip crackles (`fb_white`).
Attack: the baton chops down with the crackle (f0), then recovers with the
shield coming back (f1). Death: the shield drops and powers off (rim
hidden from f2) and lands propped on its edge beside the body, the dog
sinks to its knees and collapses forward (f4, the corpse).

Rig: one armature "Shep" (gets the actions) and one mesh skinned rigidly to
it: every part's vertices belong 100% to one bone (pixel art at 40x60 has no
room for soft deformation). Hand-posed bones point along +Z with roll 0 and
are posed in world axes relative to their parent (helper `pose`). The arms
are 2-bone IK chains whose targets ride on the baton and the shield, so the
hands stay on them; in the death frames where the shield is let go the
left arm's IK is switched off and the arm is posed by hand.

All lengths are in pixels (1 world unit = 64 px, art-bible.md); the model
faces -Y and stands on z = 0 (tools/blender_render.py convention). The
character's right side is -X (seen from the d0 camera it is on the left).
"""
import math
import os
import shutil
import sys

import bmesh
import bpy
from mathutils import Euler, Matrix, Quaternion, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
out_dir = sys.argv[sys.argv.index("--") + 1]
os.makedirs(out_dir, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)

PX = 1.0 / 64.0
HIDE = 0.001  # scale of a hidden part (spark, powered-off rim)


def V(x, y, z):
    return Vector((x * PX, y * PX, z * PX))


# --------------------------------------------------------------- materials
# Base colours are sRGB; the ramp each maps to is in sprite.json.
MATS = {
    "ShepSaddle": (0.30, 0.19, 0.10),    # brown dark end: black saddle, ears, mask
    "ShepNose": (0.20, 0.12, 0.06),      # brown: nose, eyes
    "ShepTan": (0.70, 0.62, 0.50),       # beige: tan fur
    "ShepTanLight": (0.86, 0.80, 0.68),  # beige: muzzle, brow spots, chest
    "ShepArmor": (0.27, 0.40, 0.64),     # blue: vest, pants, helmet
    "ShepArmorDark": (0.16, 0.25, 0.42), # blue: boots, pads, guards, baton
    "ShepShield": (0.50, 0.70, 0.94),    # blue: energy shield face
    "ShepRim": (0.25, 0.50, 1.0),        # fb_blue: shield rim
    "ShepSpark": (0.94, 0.94, 1.0),      # fb_white: baton crackle
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


def tube(a, b, radius, mat, bone, seg=8, r2=None, smooth=True):
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
    add_bm(bm, mat, bone, smooth)


def ear(base, tip, width, depth, mat, bone):
    """A tall pointed ear: a flat pyramid from a base (width x depth px) at
    `base` to `tip`, the opening facing -Y."""
    bm = bmesh.new()
    b = Vector(base)
    w, d = width / 2, depth / 2
    vs = [bm.verts.new(tuple((b + Vector(o)) * PX)) for o in
          ((-w, -d, 0), (w, -d, 0), (w, d, 0), (-w, d, 0))]
    t = bm.verts.new(tuple(Vector(tip) * PX))
    bm.faces.new(vs[::-1])
    for i in range(4):
        bm.faces.new((vs[i], vs[(i + 1) % 4], t))
    bm.verts.index_update()
    add_bm(bm, mat, bone, False)


# Legs: navy riot pants, dark knee pads, shin guards and boots. Hip z 26,
# knee 13.
for side, x in (("R", -4.8), ("L", 4.8)):
    box((x, -1.2, 2.0), (6.6, 10.0, 4.0), "ShepArmorDark", "shin_" + side)
    tube((x, 0, 3.5), (x, 0, 14.0), 3.0, "ShepArmor", "shin_" + side)
    box((x, -2.6, 8.5), (5.0, 1.6, 9.0), "ShepArmorDark", "shin_" + side)   # guard
    ball((x, -1.6, 13.5), (6.4, 5.6, 5.6), "ShepArmorDark", "shin_" + side)  # knee pad
    tube((x, 0, 13.0), (x * 0.95, 0, 27.0), 3.6, "ShepArmor", "thigh_" + side)

# Hips and belt.
box((0, 0, 26.0), (17.5, 11.0, 6.0), "ShepArmor", "hips")
box((0, 0, 28.6), (19.0, 12.0, 2.2), "ShepArmorDark", "hips")
# Tail: bushy, black on top with a tan underside, hanging down behind.
tube((0, 5.5, 27.0), (0, 10.5, 13.0), 2.8, "ShepSaddle", "tail", r2=2.2)
ball((0, 11.0, 12.0), (4.2, 4.2, 5.0), "ShepTan", "tail")

# Torso: black saddle on the back, tan chest, navy vest over it.
ball((0, 0.5, 35.5), (17.0, 12.0, 15.0), "ShepSaddle", "chest")
box((0, -0.8, 34.0), (18.6, 10.6, 10.6), "ShepArmor", "chest")
box((0, -6.0, 34.6), (14.0, 1.8, 8.6), "ShepArmorDark", "chest")    # front plate
for x in (-9.6, 9.6):
    ball((x, 0.4, 39.8), (7.4, 9.0, 5.2), "ShepArmorDark", "chest")  # shoulder pads
ball((0, -3.0, 41.2), (9.0, 6.0, 4.0), "ShepTanLight", "chest")      # tan bib
# Neck.
tube((0, 0.5, 40.0), (0, -0.5, 46.0), 3.8, "ShepTan", "chest")
tube((0, 2.0, 40.0), (0, 1.6, 46.0), 3.0, "ShepSaddle", "chest")

# Head: tan cheeks, black skull and mask on the muzzle bridge, long tan
# muzzle, dark nose and eyes, tan brow spots, small navy riot helmet, two
# tall black pointed ears through the helmet.
ball((0, 0.5, 48.4), (10.4, 10.0, 9.0), "ShepSaddle", "head")
ball((0, -3.0, 46.6), (9.0, 7.0, 6.0), "ShepTan", "head")
ball((0, -8.2, 45.8), (5.0, 10.0, 4.2), "ShepTanLight", "head")       # muzzle
box((0, -7.4, 47.6), (3.4, 8.6, 1.8), "ShepSaddle", "head", rot=(-8, 0, 0))  # mask
ball((0, -12.8, 46.8), (2.4, 1.8, 1.8), "ShepNose", "head", seg=8, ring=6)
for x in (-2.5, 2.5):
    ball((x, -4.4, 49.6), (1.8, 1.2, 1.6), "ShepNose", "head", seg=6, ring=4)
    ball((x, -4.6, 51.2), (1.8, 1.0, 1.0), "ShepTanLight", "head", seg=6, ring=4)
ball((0, 1.4, 52.0), (11.6, 11.0, 6.4), "ShepArmor", "head")          # helmet
box((0, 1.4, 51.0), (12.0, 11.6, 1.2), "ShepArmorDark", "head")       # helmet rim
for side, x in (("R", -3.8), ("L", 3.8)):
    ear((x, 0.8, 52.5), (x * 1.45, 1.4, 60.6), 5.4, 2.6, "ShepSaddle", "ear_" + side)

# Arms: navy sleeve (upper arm), tan forearm and paw. Rest pose: the IK
# chains just reach their targets.
SHOULDER = {"R": (-11.6, 0.0, 40.0), "L": (11.6, 0.0, 40.0)}
ELBOW = {"R": (-15.0, 0.5, 31.5), "L": (15.0, -2.0, 31.0)}
HAND = {"R": (-15.5, -6.0, 26.0), "L": (8.0, -7.6, 31.0)}
for s in ("R", "L"):
    tube(SHOULDER[s], ELBOW[s], 2.8, "ShepArmor", "uparm_" + s)
    tube(ELBOW[s], HAND[s], 2.4, "ShepTan", "forearm_" + s)
    ball(HAND[s], (4.6, 4.6, 4.6), "ShepTan", "forearm_" + s)

# Shock baton: grip at the right hand, pointing up and forward.
GRIP = Vector(HAND["R"])
BATON_DIR = Vector((-0.22, -0.40, 0.89)).normalized()
TIP = GRIP + BATON_DIR * 16.0
tube(tuple(GRIP - BATON_DIR * 3.0), tuple(TIP), 1.3, "ShepArmorDark", "baton", seg=6)
tube(tuple(GRIP + BATON_DIR * 2.5), tuple(GRIP + BATON_DIR * 3.5), 2.2,
     "ShepArmorDark", "baton", seg=6)                                   # guard
# Crackle: a few fullbright prongs around the tip.
for k in range(5):
    a = math.radians(72 * k + 10)
    side = BATON_DIR.orthogonal().normalized()
    up = BATON_DIR.cross(side)
    off = side * math.cos(a) + up * math.sin(a)
    tube(tuple(TIP - BATON_DIR * 1.5), tuple(TIP + BATON_DIR * 1.5 + off * 4.0),
         0.9, "ShepSpark", "spark", seg=4, r2=0.2)
ball(tuple(TIP), (3.4, 3.4, 3.4), "ShepSpark", "spark", seg=8, ring=6)

# Energy shield: a tall riot plate in front, 24 x 38 px, curved around a
# vertical axis (radius 24 px, bulging toward the viewer) and bevelled back
# at the top and bottom, 1.8 px thick, so the key light shades it from the
# upper-left strip to the lower-right one. Its top (z 40) sits below the
# muzzle so the black mask and tan face read over it. Modelled dog-tech
# detail: a dark vision slot, a raised paw emblem and an emitter rib; a
# fullbright rim follows the curved edge; a dark strut across the back
# where the left paw holds it.
SH_C = Vector((6.0, -10.8, 21.0))
SH_W, SH_H, SH_R, SH_T = 24.0, 38.0, 24.0, 1.8
SH_NU = 7                                                  # 6 vertical strips
SH_Z = ((-SH_H / 2, 2.0), (-SH_H / 2 + 4.0, 0.0),          # (z, set back y)
        (SH_H / 2 - 5.0, 0.0), (SH_H / 2, 3.0))


def shield_pt(u, z, back=0.0, lift=0.0):
    """Point on the shield's front surface (px, world, rest): u along the
    arc from its centre (-W/2..W/2), z from its centre; `lift` moves it out
    of the surface toward the viewer, `back` is the set-back of a bevel."""
    a = u / SH_R
    return SH_C + Vector((SH_R * math.sin(a) - lift * math.sin(a),
                          SH_R * (1 - math.cos(a)) + back - lift * math.cos(a), z))


def shield_plate():
    bm = bmesh.new()
    us = [-SH_W / 2 + SH_W * i / (SH_NU - 1) for i in range(SH_NU)]
    fr = [[bm.verts.new(tuple(shield_pt(u, z, b) * PX)) for z, b in SH_Z] for u in us]
    bk = [[bm.verts.new(tuple((shield_pt(u, z, b) + Vector((0, SH_T, 0))) * PX))
           for z, b in SH_Z] for u in us]
    nz = len(SH_Z)
    for i in range(SH_NU - 1):
        for j in range(nz - 1):
            bm.faces.new((fr[i][j], fr[i + 1][j], fr[i + 1][j + 1], fr[i][j + 1]))
            bm.faces.new((bk[i][j], bk[i][j + 1], bk[i + 1][j + 1], bk[i + 1][j]))
    ring = ([(i, 0) for i in range(SH_NU)] + [(SH_NU - 1, j) for j in range(1, nz)]
            + [(i, nz - 1) for i in range(SH_NU - 2, -1, -1)]
            + [(0, j) for j in range(nz - 2, 0, -1)])
    for k in range(len(ring)):
        (i0, j0), (i1, j1) = ring[k], ring[(k + 1) % len(ring)]
        bm.faces.new((fr[i0][j0], fr[i1][j1], bk[i1][j1], bk[i0][j0]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.verts.index_update()
    add_bm(bm, "ShepShield", "shield", False)
    # Rim along the front edge of the plate.
    pts = [shield_pt(us[i], SH_Z[j][0], SH_Z[j][1], 0.3) for i, j in ring]
    for k in range(len(pts)):
        tube(tuple(pts[k]), tuple(pts[(k + 1) % len(pts)]), 0.8, "ShepRim", "rim",
             seg=4, smooth=False)
        ball(tuple(pts[k]), (1.8, 1.8, 1.8), "ShepRim", "rim", seg=6, ring=4)


shield_plate()
# Vision slot near the top.
box(tuple(shield_pt(0, SH_H / 2 - 9.0, 0, 0.3)), (10.0, 1.6, 2.0), "ShepArmorDark", "shield")
# Paw emblem: a main pad and four toes, raised off the face.
ball(tuple(shield_pt(0, -3.0, 0, 0.2)), (7.0, 1.8, 5.6), "ShepArmorDark", "shield", seg=10, ring=6)
for u, z in ((-4.4, 1.6), (-1.6, 3.4), (1.6, 3.4), (4.4, 1.6)):
    ball(tuple(shield_pt(u, z, 0, 0.2)), (2.6, 1.6, 3.0), "ShepArmorDark", "shield", seg=6, ring=4)
# Emitter rib across the lower face, following the curve.
rib = [shield_pt(-SH_W / 2 + 2.0 + 20.0 * k / 6, -11.0, 0, 0.3) for k in range(7)]
for a, b in zip(rib, rib[1:]):
    tube(tuple(a), tuple(b), 0.8, "ShepArmor", "shield", seg=4, smooth=False)
box(tuple(SH_C + Vector((2.0, 2.4, 4.0))), (2.0, 2.0, 22.0), "ShepArmorDark", "shield")  # strut

mesh = bpy.data.meshes.new("ShepBody")
mesh.from_pydata(verts, [], faces)
for m in ORDER:
    mesh.materials.append(mats[m])
for poly, mi, sm in zip(mesh.polygons, face_mat, face_smooth):
    poly.material_index = mi
    poly.use_smooth = sm
mesh.validate()
mesh.update()
body = bpy.data.objects.new("ShepBody", mesh)
bpy.context.scene.collection.objects.link(body)

# ---------------------------------------------------------------- armature
arm_data = bpy.data.armatures.new("ShepRig")
rig = bpy.data.objects.new("Shep", arm_data)
bpy.context.scene.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode="EDIT")
eb = arm_data.edit_bones

# Hand-posed bones: (name, parent, head px). All point +Z, roll 0. The
# shield is a top-level bone (not under the chest or root) so it stays
# steady when the torso sways and can be dropped on the floor on its own.
UP_BONES = [
    ("root", None, (0, 0, 0)),
    ("hips", "root", (0, 0, 26.0)),
    ("thigh_R", "hips", (-4.8, 0, 26.0)),
    ("shin_R", "thigh_R", (-4.8, 0, 13.5)),
    ("thigh_L", "hips", (4.8, 0, 26.0)),
    ("shin_L", "thigh_L", (4.8, 0, 13.5)),
    ("tail", "hips", (0, 5.5, 27.0)),
    ("chest", "hips", (0, 0, 29.0)),
    ("head", "chest", (0, 0, 44.0)),
    ("ear_R", "head", (-3.8, 0.8, 52.5)),
    ("ear_L", "head", (3.8, 0.8, 52.5)),
    ("baton", "chest", tuple(GRIP)),
    ("spark", "baton", tuple(TIP)),
    ("hand_R_ik", "baton", HAND["R"]),
    ("shield", None, tuple(SH_C)),
    ("rim", "shield", tuple(SH_C)),
    ("hand_L_ik", "shield", HAND["L"]),
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
    c.name = "IK"
    c.target = rig
    c.subtarget = "hand_%s_ik" % s
    c.chain_count = 2
    c.use_tail = True
    c.pole_target = None

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

POSED = [n for n, _, _ in UP_BONES] + ["uparm_L", "forearm_L"]


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


def pose(spec):
    """spec: {bone: {"rot": (x,y,z) deg, "loc": (x,y,z) px, "q": Quaternion
    (world), "scale": s}} relative to the parent; unlisted bones rest.
    spec["ik_L"] = 0 switches the left arm's IK off (shield let go)."""
    for name in POSED:
        pb = rig.pose.bones[name]
        pb.rotation_mode = "QUATERNION"
        s = spec.get(name, {})
        q, loc = to_local(pb, s.get("rot", (0, 0, 0)), s.get("loc", (0, 0, 0)))
        if "q" in s:
            q = quat_local(pb, s["q"])
        pb.rotation_quaternion = q
        pb.location = loc
        sc = s.get("scale", 1.0)
        if name == "spark" and "scale" not in s:
            sc = HIDE
        pb.scale = (sc, sc, sc)
    rig.pose.bones["forearm_L"].constraints["IK"].influence = spec.get("ik_L", 1.0)


def shield_aside(t, extra=None):
    """The shield swung aside like a door hinged at its outer (+X) edge by
    a fraction t (1 = fully open, its face turned toward +X, the front of
    the dog exposed)."""
    # Turned 80 deg about its centre (counter-clockwise from above) and
    # carried out to the dog's left side: fully open, it stands beside the
    # body edge-on to the d0 view, centred at x 19, y -2.
    s = {"rot": (0, 0, 80.0 * t), "loc": (13.0 * t, 8.8 * t, 0)}
    if extra:
        s.update(extra)
    return s


def baton(hand, direction):
    """Baton with the grip moved to `hand` (px, world, rest-relative to the
    chest) and pointing along `direction` (world)."""
    q = BATON_DIR.rotation_difference(Vector(direction).normalized())
    return {"q": q, "loc": tuple(Vector(hand) - GRIP)}


def walk(phase):
    """Contact (phase 0/2) and passing (1/3) poses."""
    sw = {0: 1, 1: 0, 2: -1, 3: 0}[phase]
    lift = {0: 0, 1: 1, 2: 0, 3: 1}[phase]
    sp = {}
    sp["root"] = {"loc": (0, 0, lift)}
    sp["thigh_R"] = {"rot": (-22 * sw, 0, 0)}
    sp["thigh_L"] = {"rot": (22 * sw, 0, 0)}
    if phase in (1, 3):
        back = "R" if phase == 1 else "L"
        sp["thigh_" + back] = {"rot": (-10, 0, 0)}
        sp["shin_" + back] = {"rot": (40, 0, 0)}
    else:
        sp["shin_R" if sw < 0 else "shin_L"] = {"rot": (18, 0, 0)}
    sp["hips"] = {"rot": (0, 0, 4 * sw)}
    sp["chest"] = {"rot": (4, 0, -6 * sw)}
    sp["head"] = {"rot": (0, 0, 3 * sw)}
    sp["tail"] = {"rot": (0, 10 * sw, 0)}
    sp["shield"] = {"loc": (0, 0, lift), "rot": (0, 2 * sw, 0)}
    return sp


# The dropped shield: lying on its long edge beside the body, face up and
# propped 35 deg off the floor (so it shows face from every eye-height view
# instead of a 1 px floor line), yawed 22.5 deg so no rendered direction
# sees it exactly edge-on. Centred near the origin so its 38 px length fits
# the 56 px canvas in every view.
DROP_TILT, DROP_YAW, DROP_X, DROP_Y = 35.0, 22.5, 3.0, -4.0
FLAT = {"q": (Quaternion((0, 0, 1), math.radians(DROP_YAW))
              @ Quaternion((1, 0, 0), math.radians(DROP_TILT - 90.0))
              @ Quaternion((0, 1, 0), math.radians(90.0))),
        "loc": (DROP_X - SH_C.x, DROP_Y - SH_C.y,
                SH_W / 2 * math.sin(math.radians(DROP_TILT)) + 1.6 - SH_C.z)}

POSES = {
    "idle": [
        {"head": {"rot": (4, 0, 0)}},
        {"chest": {"loc": (0, 0, 1)}, "head": {"rot": (0, 0, 0)},
         "tail": {"rot": (0, 8, 0)}},
    ],
    "walk": [walk(0), walk(1), walk(2), walk(3)],
    "windup": [
        {"shield": shield_aside(0.55), "chest": {"rot": (0, 0, 8)},
         "baton": baton((-15.0, -2.0, 36.0), (-0.1, 0.25, 1.0))},
        {"shield": shield_aside(1.0), "chest": {"rot": (-4, 0, 12)},
         "head": {"rot": (4, 0, -6)},
         "baton": baton((-14.0, 1.0, 42.0), (-0.15, 0.7, 0.7)),
         "spark": {"scale": 1.0}, "ear_R": {"rot": (0, -6, 0)}},
    ],
    "attack": [
        {"shield": shield_aside(1.0), "chest": {"rot": (14, 0, -8)},
         "head": {"rot": (6, 0, -4)},
         "baton": baton((-10.0, -9.0, 31.0), (0.3, -0.6, -0.75)),
         "spark": {"scale": 1.0}},
        {"shield": shield_aside(0.5), "chest": {"rot": (8, 0, -4)},
         "baton": baton((-13.0, -7.0, 27.0), (0.0, -0.6, -0.8))},
    ],
    "pain": [
        {"chest": {"rot": (-14, 0, -8)}, "head": {"rot": (-20, 10, 0)},
         "hips": {"loc": (0, 2, 0)},
         "shield": {"rot": (-8, 6, 10), "loc": (1, 3, 0)},
         "ear_R": {"rot": (0, -30, 0)}, "ear_L": {"rot": (0, 30, 0)},
         "tail": {"rot": (-20, 0, 0)},
         "thigh_R": {"rot": (-10, 0, 0)}, "shin_R": {"rot": (14, 0, 0)}},
    ],
    "death": [
        # f0: hit, thrown back, shield sagging away to the side.
        {"chest": {"rot": (-20, 0, 6)}, "head": {"rot": (-28, 0, 0)},
         "hips": {"loc": (0, 3, 0)},
         "shield": {"rot": (0, 18, 20), "loc": (3, 3, -2)},
         "ear_R": {"rot": (0, -25, 0)}, "ear_L": {"rot": (0, 25, 0)},
         "tail": {"rot": (-25, 0, 0)}},
        # f1: knees buckle, the shield falls out of the paw.
        {"root": {"loc": (0, 8, -5)}, "thigh_R": {"rot": (-38, 0, 0)},
         "thigh_L": {"rot": (-30, 0, 0)}, "shin_R": {"rot": (70, 0, 0)},
         "shin_L": {"rot": (60, 0, 0)}, "chest": {"rot": (14, 0, 0)},
         "head": {"rot": (22, 0, 0)}, "ik_L": 0.0,
         "uparm_L": {"rot": (0, -20, 0)},
         "shield": {"rot": (30, 0, 10), "loc": (0, 4, -4)},
         "baton": {"rot": (60, 0, 0)}},
        # f2: on the knees, slumping; the shield on the floor, powered off.
        {"root": {"loc": (0, 11, -11)}, "thigh_R": {"rot": (-14, 0, 0)},
         "thigh_L": {"rot": (-8, 0, 0)}, "shin_R": {"rot": (95, 0, 0)},
         "shin_L": {"rot": (92, 0, 0)}, "chest": {"rot": (26, 0, 8)},
         "head": {"rot": (32, 0, 10)}, "ik_L": 0.0,
         "uparm_L": {"rot": (20, -10, 0)},
         "shield": FLAT, "tail": {"rot": (-10, 0, 0)},
         "rim": {"scale": HIDE}, "baton": {"rot": (90, 0, 0)}},
        # f3: pitching forward off the knees.
        {"root": {"rot": (45, 0, 0), "loc": (0, 24, -6)}, "tail": {"rot": (-45, 0, 0)},
         "thigh_R": {"rot": (-50, 0, 0)}, "thigh_L": {"rot": (-44, 0, 0)},
         "shin_R": {"rot": (95, 0, 0)}, "shin_L": {"rot": (92, 0, 0)},
         "chest": {"rot": (12, 0, 6)}, "head": {"rot": (12, 0, 14)}, "ik_L": 0.0,
         "uparm_L": {"rot": (40, 0, 0)},
         "shield": FLAT,
         "rim": {"scale": HIDE}, "baton": {"rot": (90, 0, 0)}},
        # f4: corpse: collapsed forward onto the knees and chest beside the
        # propped shield, head turned to its left.
        {"root": {"rot": (80, 0, 0), "loc": (0, 36, 3)},
         "thigh_R": {"rot": (-84, 0, 0)}, "thigh_L": {"rot": (-80, 0, 0)},
         "shin_R": {"rot": (150, 0, 0)}, "shin_L": {"rot": (148, 0, 0)},
         "chest": {"rot": (6, 0, 6)}, "head": {"rot": (-10, 0, 70)}, "ik_L": 0.0,
         "uparm_L": {"rot": (60, 0, 0)}, "tail": {"rot": (-60, 0, 0)},
         "shield": FLAT,
         "rim": {"scale": HIDE}, "baton": {"rot": (90, 0, 0)}},
    ],
}

ACTION_NAME = {"idle": "ShepIdle", "walk": "ShepWalk", "windup": "ShepWindup",
               "attack": "ShepAttack", "pain": "ShepPain", "death": "ShepDeath"}

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
        rig.pose.bones["forearm_L"].constraints["IK"].keyframe_insert("influence", frame=f)
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

blend = os.path.join(out_dir, "shep.blend")
bpy.ops.wm.save_as_mainfile(filepath=blend)
shutil.copy(os.path.join(HERE, "sprite.json"), os.path.join(out_dir, "sprite.json"))
print("shep model: wrote %s (%d verts, %d faces)" % (blend, len(verts), len(faces)))
