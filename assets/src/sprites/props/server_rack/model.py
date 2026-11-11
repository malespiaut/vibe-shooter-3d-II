"""Server rack model and LED blink (docs/design/items.md Decorations, issue #128).

Generates the .blend rendered by tools/blender_render.py; run by
tools/build.d/48-props.sh:

    blender -b --factory-startup --python assets/src/sprites/props/server_rack/model.py -- <out_dir>

writes <out_dir>/server_rack.blend and copies sprite.json next to it.

The look: a dog-tech 19-inch rack, 0.72 u wide and 0.98 u tall (46x63 px plus
the outline column/row on a 48x64 canvas). Steel side posts, top cap and
plinth frame six gray server units. Each unit has a lit top chamfer, a dark
vent grille, a steel pull handle and a strip of LEDs: one steady fb_green
power LED and three activity LEDs (fb_green / fb_cyan) in two sets that
alternate between the two frames. Behind every LED sits a dark gray socket,
so an LED that is off reads as an unlit lamp, not a hole.

Model convention (blender_render.py): faces -Y, stands on z = 0, origin at
(0, 0, 0). Flat shaded boxes and prisms.

Rig: armature "RackRig" with bones root, led_a and led_b, all pointing up
(+Z, roll 0: bone-local Z = world -Y). Every vertex belongs to one bone. The
action RackIdle (constant interpolation) pushes the off set 0.1 u back into
the faceplate: frame 1 = set A lit, frame 2 = set B lit.
"""
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
# renders at this colour, art-bible.md Pre-render exposure).
MATS = {
    "RackSteel": (0.74, 0.80, 0.88),   # posts, cap, plinth, handles
    "RackUnit": (0.66, 0.66, 0.66),    # server faceplates
    "RackVent": (0.30, 0.30, 0.30),    # vent grille, recessed
    "RackSocket": (0.22, 0.22, 0.22),  # LED sockets
    "LedGreen": (0.25, 1.00, 0.25),    # fb_green
    "LedCyan": (0.25, 1.00, 0.94),     # fb_cyan
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

verts, faces, face_mat, vgroup = [], [], [], []
U = 1.0 / 64.0  # one pixel


def add(vs, fs, mat, bone):
    b = len(verts)
    verts.extend(vs)
    vgroup.extend([bone] * len(vs))
    for f in fs:
        faces.append(tuple(b + i for i in f))
        face_mat.append(ORDER.index(mat))


def box(x0, x1, y0, y1, z0, z1, mat, bone="root"):
    vs = [(x, y, z) for z in (z0, z1) for (x, y) in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]
    fs = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    add(vs, fs, mat, bone)


def chamfer(x0, x1, yf, yb, z0, z1, mat, bone="root"):
    """Sloped strip from (yf, z0) at the front up to (yb, z1) at the back:
    faces up and toward the viewer, so the top-left key lights it brighter
    than the vertical faceplate below."""
    vs = [(x0, yf, z0), (x1, yf, z0), (x1, yb, z1), (x0, yb, z1),
          (x0, yb, z0), (x1, yb, z0)]
    fs = [(0, 1, 2, 3), (0, 4, 5, 1), (0, 3, 4), (1, 5, 2), (4, 3, 2, 5)]
    add(vs, fs, mat, bone)


HW = 23 * U          # half width: 46 px of art
TOP = 63 * U         # 63 px tall, bottom outline row on the canvas
FRONT = -0.25
POST = 5 * U         # side post width

# frame: side posts, top cap, plinth, back panel
for sx in (-1, 1):
    x0, x1 = (-HW, -HW + POST) if sx < 0 else (HW - POST, HW)
    box(x0, x1, FRONT - 2 * U, 0.25, 4 * U, TOP - 4 * U, "RackSteel")
    # rack-mount holes: small sockets down the post front
    for k in range(9):
        z = 9 * U + k * 6 * U
        xc = (x0 + x1) / 2
        box(xc - U / 2, xc + U / 2, FRONT - 2.5 * U, FRONT - 1.5 * U, z, z + U, "RackSocket")
box(-HW, HW, FRONT - 3 * U, 0.25, TOP - 4 * U, TOP, "RackSteel")           # top cap
chamfer(-HW, HW, FRONT - 3 * U, FRONT - 1 * U, TOP - 1 * U, TOP + 0.5 * U, "RackSteel")
box(-HW + U, HW - U, FRONT - 3 * U, 0.22, 0.0, 4 * U, "RackSteel")         # plinth
box(-HW + POST, HW - POST, 0.20, 0.25, 4 * U, TOP - 4 * U, "RackVent")    # back panel

# six server units between z = 4 px and z = 59 px (55 px): 9 px each, 1 px gaps
IX0, IX1 = -HW + POST, HW - POST
leds = []  # (x, z, bone, mat)
for u in range(6):
    z0 = 5 * U + u * 9 * U
    z1 = z0 + 8 * U
    box(IX0, IX1, FRONT + U, 0.18, z0, z1 - 2 * U, "RackUnit")
    chamfer(IX0, IX1, FRONT + U, FRONT + 3 * U, z1 - 2 * U, z1, "RackUnit")
    # vent grille: two dark slot inserts on the left half, flush with the faceplate
    for k in range(2):
        zz = z0 + (1.0 + 3.0 * k) * U
        box(IX0 + 3 * U, IX0 + 17 * U, FRONT + 0.4 * U, FRONT + 2 * U, zz, zz + 0.9 * U, "RackVent")
    # pull handle, steel, standing proud of the faceplate
    hx = IX0 + 19 * U
    box(hx, hx + 1.2 * U, FRONT - 1.5 * U, FRONT + U, z0 + U, z1 - 2.5 * U, "RackSteel")
    # LED strip on the right
    lx = IX1 - 13 * U
    for i in range(4):
        x = lx + i * 3 * U
        z = z0 + 3 * U
        box(x - 0.2 * U, x + 2.2 * U, FRONT + 0.5 * U, FRONT + 2 * U, z - 0.2 * U, z + 2.2 * U,
            "RackSocket")
        if i == 0:
            leds.append((x, z, "root", "LedGreen"))
        else:
            bone = "led_a" if (u + i) % 2 == 0 else "led_b"
            mat = "LedCyan" if (u * 3 + i) % 3 else "LedGreen"
            leds.append((x, z, bone, mat))
for x, z, bone, mat in leds:
    box(x, x + 2 * U, FRONT - U, FRONT + U, z, z + 2 * U, mat, bone)

mesh = bpy.data.meshes.new("ServerRack")
mesh.from_pydata(verts, [], faces)
for m in ORDER:
    mesh.materials.append(mats[m])
for poly, mi in zip(mesh.polygons, face_mat):
    poly.material_index = mi
    poly.use_smooth = False
mesh.validate()
mesh.update()
body = bpy.data.objects.new("ServerRack", mesh)
scene = bpy.context.scene
scene.collection.objects.link(body)

BONES = ("root", "led_a", "led_b")
arm = bpy.data.armatures.new("RackRig")
rig = bpy.data.objects.new("RackRig", arm)
scene.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
rig.select_set(True)
bpy.ops.object.mode_set(mode="EDIT")
for name in BONES:
    eb = arm.edit_bones.new(name)
    eb.head = (0.0, 0.0, 0.0)
    eb.tail = (0.0, 0.0, 0.05)
    eb.roll = 0.0
    if name != "root":
        eb.parent = arm.edit_bones["root"]
bpy.ops.object.mode_set(mode="OBJECT")
for name in BONES:
    body.vertex_groups.new(name=name)
for i, g in enumerate(vgroup):
    body.vertex_groups[g].add([i], 1.0, "REPLACE")
body.parent = rig
mod = body.modifiers.new("Armature", "ARMATURE")
mod.object = rig

OFF = (0.0, 0.0, -0.1)  # bone-local Z = world -Y: 0.1 u back, behind the faceplate


def keyframe(frame, off_bone):
    for pb in rig.pose.bones:
        pb.location = OFF if pb.name == off_bone else (0.0, 0.0, 0.0)
        pb.keyframe_insert("location", frame=frame)


rig.animation_data_create()
act = bpy.data.actions.new("RackIdle")
rig.animation_data.action = act
if hasattr(rig.animation_data, "action_slot") and rig.animation_data.action_slot is None \
        and len(act.slots):
    rig.animation_data.action_slot = act.slots[0]
keyframe(1, "led_b")
keyframe(2, "led_a")
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
for pb in rig.pose.bones:
    pb.location = (0.0, 0.0, 0.0)

scene.frame_start, scene.frame_end = 1, 2
blend = os.path.join(out_dir, "server_rack.blend")
bpy.ops.wm.save_as_mainfile(filepath=blend)
shutil.copy(os.path.join(HERE, "sprite.json"), os.path.join(out_dir, "sprite.json"))
print("server_rack: wrote %s" % blend)
