import bpy, math
from mathutils import Euler, Vector
def d(x,z): return (Euler((math.radians(x),0,math.radians(z)))).to_matrix() @ Vector((0,0,-1))
print("camera X=90 Z=0 view dir", tuple(round(c,3) for c in d(90,0)))
print("key    X=45 Z=-45 travel", tuple(round(c,3) for c in d(45,-45)))
print("fill   X=90 Z=0  travel", tuple(round(c,3) for c in d(90,0)))
# rotated camera (yaw 135): all three rotate together
for yaw in (0,135):
  print("yaw",yaw,"cam",tuple(round(c,3) for c in d(90,yaw)),"fill",tuple(round(c,3) for c in d(90,yaw)),"key",tuple(round(c,3) for c in d(45,-45+yaw)))
