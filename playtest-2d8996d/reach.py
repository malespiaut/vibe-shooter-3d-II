import sys, re
from collections import deque
txt=open(sys.argv[1]).read().splitlines()
leg={}; rows=[]; ents=[]; i=0
while i<len(txt):
    s=txt[i]; i+=1
    if s.startswith('legend '):
        t=s.split(); d={'ch':t[1]}
        for kv in t[2:]:
            if kv.startswith('#'): break
            k,_,v=kv.partition('='); d[k]=v
        leg[t[1]]=d
    elif s.strip()=='layout':
        while txt[i].strip()!='end': rows.append(txt[i].rstrip()); i+=1
    elif s.startswith('entity '):
        t=s.split('#')[0].split(); ents.append((t[1],int(t[2]),int(t[3]),' '.join(t[4:])))
H=len(rows); W=len(rows[0])
def cell(x,y):
    if not(0<=x<W and 0<=y<H): return None
    return leg.get(rows[H-1-y][x],{})
def flags(c): return c.get('flags','').split('|')
exits=set()
for y in range(H):
    for x in range(W):
        c=cell(x,y)
        if 'EXIT' in flags(c):
            for dx,dy in((1,0),(-1,0),(0,1),(0,-1)): exits.add((x+dx,y+dy))
keys={}
for t,x,y,rest in ents:
    m=re.match(r'(GREEN|BLUE|VIOLET)_ANKH',t)
    if m: keys[(x,y)]=m.group(1).lower()
start=[(x,y) for t,x,y,_ in ents if t=='PLAYER_START'][0]
def passable(fx,fy,x,y,have,openswitch):
    c=cell(x,y)
    if c is None: return False
    f=flags(c)
    if 'BLOCK_ALL' in f: return False
    if 'DOOR' in f:
        k=c.get('key')
        if k and k not in have: return False
        if c.get('mode')=='switch':
            return ('START_OPEN' in f) != openswitch.get(c.get('tag'),False) if 'START_OPEN' in f else openswitch.get(c.get('tag'),False)
        if 'ONEWAY' in f:
            side=c['side']; need={'west':(x-1,y),'east':(x+1,y),'north':(x,y+1),'south':(x,y-1)}[side]
            return (fx,fy)==need or (fx,fy)==(x,y)
        return True
    if c.get('wall'): return False
    return True
def bfs(src,have,ow):
    seen={src}; q=deque([src])
    while q:
        x,y=q.popleft()
        for dx,dy in((1,0),(-1,0),(0,1),(0,-1)):
            n=(x+dx,y+dy)
            if n in seen: continue
            if passable(x,y,n[0],n[1],have,ow): seen.add(n); q.append(n)
    return seen
def closure(src,have,skip=None):
    have=set(have)
    while True:
        r=bfs(src,have,{})
        new={keys[p] for p in r if p in keys}-have-({skip} if skip else set())
        if not new: return r,have
        have|=new
r,have=closure(start,set())
print(sys.argv[1],'keys on map',sorted(set(keys.values())),'reachable keys',sorted(have),'exit reachable (switch doors closed, pushwalls solid):',bool(r&exits))
for k in sorted(set(keys.values())):
    r2,h2=closure(start,set(),skip=k)
    print('  without',k,': exit reachable =',bool(r2&exits))
# softlock: from every reachable cell, can we get back to exit with all keys? (with keys collected from there)
bad=[p for p in r if not (closure(p,have)[0]&exits)]
print('  cells (all keys held) from which exit unreachable:',len(bad),sorted(bad)[:10])
