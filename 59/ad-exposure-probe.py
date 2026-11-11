import bpy, math, sys
s=bpy.context.scene
for o in list(bpy.data.objects): bpy.data.objects.remove(o)
s.render.engine='CYCLES'; s.cycles.samples=16; s.cycles.device='CPU'
s.render.resolution_x=s.render.resolution_y=8
s.world=bpy.data.worlds.new('w'); s.world.color=(0,0,0)
s.world.use_nodes=False
bpy.ops.mesh.primitive_plane_add(size=10)
m=bpy.data.materials.new('m'); m.use_nodes=True
m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value=(0.5,0.5,0.5,1)
m.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value=1.0
bpy.context.object.data.materials.append(m)
bpy.ops.object.camera_add(location=(0,0,5)); s.camera=bpy.context.object
bpy.context.object.data.type='ORTHO'
for st,label in ((1.0,'1'),(math.pi,'pi')):
  for o in [o for o in s.objects if o.type=='LIGHT']: bpy.data.objects.remove(o)
  bpy.ops.object.light_add(type='SUN'); bpy.context.object.data.energy=st
  for vt in ('Standard','AgX'):
    s.view_settings.view_transform=vt
    s.render.image_settings.file_format='PNG'
    bpy.ops.render.render()
    r=bpy.data.images['Render Result']
    p='/tmp/adtest/o.png'; r.save_render(p)
    im=bpy.data.images.load(p);im.reload(); px=im.pixels[:]
    print('RESULT',vt,label,px[4*27:4*27+3])
