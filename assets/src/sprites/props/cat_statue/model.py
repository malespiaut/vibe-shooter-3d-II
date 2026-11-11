"""Seated Bast cat statue (docs/design/items.md Decorations, art-bible.md).

Generates the .blend rendered by tools/blender_render.py; run by
tools/build.d/48-props.sh:

    blender -b --factory-startup --python assets/src/sprites/props/cat_statue/model.py -- <out_dir>

writes <out_dir>/cat_statue.blend and copies sprite.json next to it.

The look: a tall, upright seated cat in polished black basalt (`steel`
ramp: a cool dark stone that still reaches the upper half of its ramp on lit
faces), gold collar, gold scarab on the chest, gold eyes and a gold ring in
the left ear, the tail curled round the front paws, on a sandstone plinth
(`sand`). 0.5 x 1.0 units, 32x64 px; the ear tips set the height.

Model convention (blender_render.py): faces -Y, stands on z = 0, origin at
(0, 0, 0). Low-poly, flat shaded: every facet is a few pixels, which reads
as carved planes. Static: one action with a single key, rendered as idle f0.
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
# renders at exactly this colour, art-bible.md Pre-render exposure).
MATS = {
    "StatueBasalt": (0.50, 0.55, 0.62),  # dark polished stone; lit faces ~steel_09
    "StatueGold": (1.00, 0.86, 0.36),    # gold inlay, lit faces near the top
    "StatuePlinth": (0.93, 0.80, 0.52),  # sandstone block
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

verts, faces, face_mat = [], [], []


def add(vs, fs, mat):
    b = len(verts)
    verts.extend(vs)
    for f in fs:
        faces.append(tuple(b + i for i in f))
        face_mat.append(ORDER.index(mat))


def box(x0, x1, y0, y1, z0, z1, mat):
    vs = [(x, y, z) for z in (z0, z1) for (x, y) in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]
    fs = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    add(vs, fs, mat)


def ellipsoid(c, r, mat, seg=8, rings=5):
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
    fs = [(0, 1 + (j + 1) % seg, 1 + j) for j in range(seg)]
    for i in range(rings - 2):
        a0, a1 = 1 + i * seg, 1 + (i + 1) * seg
        for j in range(seg):
            k = (j + 1) % seg
            fs.append((a0 + j, a0 + k, a1 + k, a1 + j))
    last = 1 + (rings - 2) * seg
    fs.extend((last + j, last + (j + 1) % seg, top) for j in range(seg))
    add(vs, fs, mat)


def cylinder(x, y, r, z0, z1, mat, seg=6):
    vs = []
    for z in (z0, z1):
        for j in range(seg):
            a = 2 * math.pi * (j + 0.5) / seg
            vs.append((x + r * math.cos(a), y + r * math.sin(a), z))
    fs = [tuple(range(seg - 1, -1, -1)), tuple(range(seg, 2 * seg))]
    fs += [(j, (j + 1) % seg, seg + (j + 1) % seg, seg + j) for j in range(seg)]
    add(vs, fs, mat)


def prism(tri, y0, y1, mat):
    """Triangle `tri` [(x, z)] x3 extruded from y0 (front) to y1 (back)."""
    vs = [(x, y0, z) for x, z in tri] + [(x, y1, z) for x, z in tri]
    fs = [(0, 1, 2), (5, 4, 3), (0, 3, 4, 1), (1, 4, 5, 2), (2, 5, 3, 0)]
    add(vs, fs, mat)


U = 1.0 / 64.0  # one pixel
PLINTH_TOP = 0.16
# The cat is carved with its own basalt base slab, 2 px tall, on the plinth.
# The pointed low-poly bottoms of the haunch, paw and tail ellipsoids sink
# into it, so no transparent gap or dark comb shows between the body and the
# plinth (PR #160 review).
BASE_H = 2 * U
TOP = PLINTH_TOP + BASE_H - 1.0 * U  # cat body bottom, 1 px into the slab

# Plinth: a sandstone block with a projecting cornice and foot.
box(-0.215, 0.215, -0.17, 0.17, 0.0, 0.03, "StatuePlinth")
box(-0.19, 0.19, -0.15, 0.15, 0.03, 0.13, "StatuePlinth")
box(-0.215, 0.215, -0.17, 0.17, 0.13, PLINTH_TOP, "StatuePlinth")
box(-0.165, 0.165, -0.15, 0.16, PLINTH_TOP, PLINTH_TOP + BASE_H, "StatueBasalt")

# Haunches and body: a pear, wide at the seat, narrowing to the chest.
ellipsoid((0.0, 0.05, TOP + 0.13), (0.15, 0.12, 0.13), "StatueBasalt", seg=10)
ellipsoid((0.0, 0.00, TOP + 0.30), (0.10, 0.09, 0.17), "StatueBasalt", seg=10)
ellipsoid((0.0, -0.035, TOP + 0.43), (0.08, 0.075, 0.10), "StatueBasalt", seg=8)
# Front legs: straight columns, paws together at the front edge.
for s in (1, -1):
    cylinder(s * 0.045, -0.075, 0.028, TOP + 0.02, TOP + 0.36, "StatueBasalt")
    ellipsoid((s * 0.045, -0.095, TOP + 0.022), (0.032, 0.045, 0.022), "StatueBasalt", seg=6, rings=3)
# Neck and head, held high.
ellipsoid((0.0, -0.035, TOP + 0.53), (0.06, 0.06, 0.09), "StatueBasalt", seg=8)
ellipsoid((0.0, -0.05, TOP + 0.635), (0.078, 0.068, 0.068), "StatueBasalt", seg=10)
ellipsoid((0.0, -0.112, TOP + 0.612), (0.036, 0.03, 0.03), "StatueBasalt", seg=6, rings=4)
# Tall pointed ears.
for s in (1, -1):
    prism([(s * 0.018, TOP + 0.67), (s * 0.075, TOP + 0.65), (s * 0.068, TOP + 0.83)],
          -0.07, -0.03, "StatueBasalt")
# Tail: wraps from the right haunch round the front of the paws.
for k in range(13):
    a = math.radians(-5 - 15 * k)
    ellipsoid((0.15 * math.cos(a), 0.14 * math.sin(a) - 0.01, TOP + 0.025),
              (0.032, 0.032, 0.025), "StatueBasalt", seg=6, rings=3)

# Gold: collar band, scarab pendant, eyes, ear ring.
ellipsoid((0.0, -0.038, TOP + 0.475), (0.072, 0.07, 0.016), "StatueGold", seg=10, rings=3)
ellipsoid((0.0, -0.105, TOP + 0.43), (0.022, 0.012, 0.026), "StatueGold", seg=6, rings=3)
for s in (1, -1):
    box(s * 0.032 - 0.014, s * 0.032 + 0.014, -0.124, -0.112, TOP + 0.638, TOP + 0.652,
        "StatueGold")
ellipsoid((0.066, -0.06, TOP + 0.665), (0.012, 0.012, 0.016), "StatueGold", seg=6, rings=3)

mesh = bpy.data.meshes.new("CatStatue")
mesh.from_pydata(verts, [], faces)
for m in ORDER:
    mesh.materials.append(mats[m])
for poly, mi in zip(mesh.polygons, face_mat):
    poly.material_index = mi
    poly.use_smooth = False
mesh.validate()
mesh.update()
obj = bpy.data.objects.new("CatStatue", mesh)
scene = bpy.context.scene
scene.collection.objects.link(obj)

# One static key: blender_render.py renders every listed frame of an action.
obj.animation_data_create()
act = bpy.data.actions.new("CatStatueIdle")
obj.animation_data.action = act
if hasattr(obj.animation_data, "action_slot") and obj.animation_data.action_slot is None \
        and len(act.slots):
    obj.animation_data.action_slot = act.slots[0]
obj.keyframe_insert("location", frame=1)
act.use_fake_user = True
obj.animation_data.action = None

scene.frame_start, scene.frame_end = 1, 1
blend = os.path.join(out_dir, "cat_statue.blend")
bpy.ops.wm.save_as_mainfile(filepath=blend)
shutil.copy(os.path.join(HERE, "sprite.json"), os.path.join(out_dir, "sprite.json"))
print("cat_statue: wrote %s" % blend)
