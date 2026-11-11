"""Generate the blender_render test fixture: an asymmetric arrow (.blend + sprite.json).

Test fixture only, never shipped. Run by tests/test_blender_render.py:

    blender -b --factory-startup --python tests/fixtures/make_arrow_blend.py -- <out_dir>

writes <out_dir>/arrow.blend and <out_dir>/sprite.json.

Model convention (tools/blender_render.py): the thing faces -Y (towards the
d0 camera), stands on z = 0, origin at (0, 0, 0). The arrow:
- head (gold) points -Y, at the d0 camera;
- tall tail fin (sand) at +Y, so side views are front/back asymmetric;
- one wing (red) on +X only, the arrow's LEFT side (it faces -Y), so the
  front and back views are mirror images of each other and d2 (camera moved
  90 deg counter-clockwise from the front) sees the wing face-on;
- a post (sand, like the shaft) down to the floor.
Three materials map to three ramps in sprite.json "ramps" (the colour
budget, art-bible.md).
Action "Bob": frame 1 at z = 0, frame 2 raised 4 px (4/64 unit).
"""
import json
import os
import sys

import bpy

out_dir = sys.argv[sys.argv.index("--") + 1]
os.makedirs(out_dir, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)

# sRGB-ish base colours (the renderer uses the Standard view transform).
MATS = {
    "sand": (0.80, 0.62, 0.30),
    "gold": (0.95, 0.75, 0.15),
    "red": (0.80, 0.12, 0.08),
}


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


mats = {}
for name, rgb in MATS.items():
    m = bpy.data.materials.new(name)
    if not m.node_tree:  # always present in Blender 5; use_nodes is deprecated
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
ORDER = ["sand", "gold", "red"]


def box(x0, x1, y0, y1, z0, z1, mat):
    b = len(verts)
    for z in (z0, z1):
        for (x, y) in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
            verts.append((x, y, z))
    quads = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
             (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    for q in quads:
        faces.append(tuple(b + i for i in q))
        face_mat.append(ORDER.index(mat))


def pyramid(base_y, x0, x1, z0, z1, apex, mat):
    b = len(verts)
    verts.extend([(x0, base_y, z0), (x1, base_y, z0), (x1, base_y, z1), (x0, base_y, z1), apex])
    for f in ((b, b + 3, b + 2, b + 1),):
        faces.append(f)
        face_mat.append(ORDER.index(mat))
    for i in range(4):
        faces.append((b + i, b + (i + 1) % 4, b + 4))
        face_mat.append(ORDER.index(mat))


box(-0.10, 0.10, -0.20, 0.35, 0.28, 0.44, "sand")          # shaft
pyramid(-0.20, -0.20, 0.20, 0.18, 0.54, (0.0, -0.48, 0.36), "gold")  # head -> -Y
box(-0.03, 0.03, 0.18, 0.35, 0.44, 0.80, "sand")           # tail fin (+Y, tall)
box(0.10, 0.34, -0.06, 0.24, 0.31, 0.41, "red")            # wing, +X only
box(-0.05, 0.05, -0.05, 0.05, 0.00, 0.28, "sand")          # post to the floor

mesh = bpy.data.meshes.new("Arrow")
mesh.from_pydata(verts, [], faces)
for m in ORDER:
    mesh.materials.append(mats[m])
for poly, mi in zip(mesh.polygons, face_mat):
    poly.material_index = mi
    poly.use_smooth = False
mesh.validate()
mesh.update()
obj = bpy.data.objects.new("Arrow", mesh)
bpy.context.scene.collection.objects.link(obj)

scene = bpy.context.scene
scene.frame_start, scene.frame_end = 1, 2
obj.location = (0.0, 0.0, 0.0)
obj.keyframe_insert("location", frame=1)
obj.location = (0.0, 0.0, 4.0 / 64.0)
obj.keyframe_insert("location", frame=2)
act = obj.animation_data.action
act.name = "Bob"
# Constant interpolation: frames are poses, not tweens.
try:
    curves = act.fcurves
except AttributeError:  # layered actions (Blender 4.4+)
    curves = [fc for layer in act.layers for strip in layer.strips
              for bag in strip.channelbags for fc in bag.fcurves]
for fc in curves:
    for kp in fc.keyframe_points:
        kp.interpolation = "CONSTANT"
obj.animation_data.action = None  # blender_render assigns it per sprite.json
act.use_fake_user = True

blend = os.path.join(out_dir, "arrow.blend")
bpy.ops.wm.save_as_mainfile(filepath=blend)
with open(os.path.join(out_dir, "sprite.json"), "w") as f:
    json.dump({"object": "Arrow",
               "canvas": [64, 64],
               "ramps": {"sand": "sand", "gold": "gold", "red": "red"},
               "actions": {"Bob": {"anim": "bob", "frames": [1, 2]}}}, f, indent=2)
print("fixture: wrote %s" % blend)
