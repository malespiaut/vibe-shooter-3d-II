import sys, random, os
sys.path.insert(0,"/work/tools"); import bast
src=open("/work/maps/m1test.vmap","rb").read()
snd=open(sorted(os.listdir("/work/assets/gen/sfx"))[0] and "/work/assets/gen/sfx/"+sorted(os.listdir("/work/assets/gen/sfx"))[0],"rb").read()
rnd=random.Random(1); ok=err=0
for kind,data,loader in (("vmap",src,bast.Map.load),("snd",snd,bast.Snd.load)):
  for n in range(400):
    d=bytearray(data)
    if n<len(d) and n%2: d=d[:rnd.randrange(len(d))]
    else:
      for _ in range(rnd.randrange(1,8)): d[rnd.randrange(len(d))]=rnd.randrange(256)
    p=f"/tmp/fz/t.{kind}"; open(p,"wb").write(d)
    try: loader(p); ok+=1
    except bast.BastError: err+=1
print("loaded",ok,"rejected",err)
