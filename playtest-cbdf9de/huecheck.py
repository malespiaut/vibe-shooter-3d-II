import sys, math
sys.path.insert(0, "/work/tools")
import bast
pal = open("/work/assets/gen/palette.bin","rb").read()
cm = open("/work/assets/gen/colormap.bin","rb").read()
def lin(c):
    c/=255; return c/12.92 if c<=0.04045 else ((c+0.055)/1.055)**2.4
def oklab(rgb):
    r,g,b=(lin(x) for x in rgb)
    l=0.4122214708*r+0.5363325363*g+0.0514459929*b
    m=0.2119034982*r+0.6806995451*g+0.1073969566*b
    s=0.0883024619*r+0.2817188376*g+0.6299787005*b
    l,m,s=(x**(1/3) for x in (l,m,s))
    L=0.2104542553*l+0.7936177850*m-0.0040720468*s
    a=1.9779984951*l-2.4285922050*m+0.4505937099*s
    bb=0.0259040371*l+0.7827717662*m-0.8086757660*s
    return L,a,bb
col=[tuple(pal[i*3:i*3+3]) for i in range(256)]
bad=[]
for i in range(256):
    L0,a0,b0=oklab(col[i]); C0=math.hypot(a0,b0)
    if C0<0.08: continue
    h0=math.degrees(math.atan2(b0,a0))
    worst=(0,0,0)
    for r in range(1,24):
        j=cm[r*256+i]; L,a,b=oklab(col[j]); C=math.hypot(a,b)
        if C<0.04: continue
        d=abs((math.degrees(math.atan2(b,a))-h0+180)%360-180)
        if d>worst[0]: worst=(d,r,j)
    if worst[0]>45: bad.append((i,col[i],worst))
for i,c,(d,r,j) in bad: print(f"idx {i} {c} row {r} -> idx {j} {col[j]} hue shift {d:.0f} deg")
print(len(bad),"entries with >45 deg hue shift in rows 1-23")
