"""Independent native-town contour raster, checking all 64 tile alignments."""
from pathlib import Path
import json
root=Path(__file__).resolve().parents[1]
raw=Path('/workspace/scratch/eda30d53efcc/work/stage58/minimap-census/town.bin').read_bytes()
assert len(raw)==96*96
def walk(x,y):return 0<=x<96 and 0<=y<96 and raw[y*96+x]!=0
def rnd(n,d):return -(int((-n+d//2)//d)) if n<0 else int((n+d//2)//d)
def point(x,y):return 256+rnd((x-y)*320,180),48+rnd((x+y-1)*320,362)
lines=[]
for axis in range(2):
 for row in range(97):
  start=None
  for col in range(97):
   x,y=(row,col) if axis else (col,row)
   visible=col<96 and walk(x,y)!=walk(x-int(bool(axis)),y-int(not axis))
   if visible and start is None:start=col
   if not visible and start is not None:
    a,b=((row,start),(row,col)) if axis else ((start,row),(col,row))
    lines.append((point(*a),point(*b)));start=None
pixels=set()
for (x,y),(ex,ey) in lines:
 dx,dy=abs(ex-x),-abs(ey-y);sx=1 if ex>=x else -1;sy=1 if ey>=y else -1;err=dx+dy
 while True:
  if 0<=x<512 and 0<=y<256:pixels.add((x,y))
  if x==ex and y==ey:break
  twice=2*err
  if twice>=dy:err+=dy;x+=sx
  if twice<=dx:err+=dx;y+=sy
rev=bytes(int(f'{b:08b}'[::-1],2) for b in range(256))
def count(dx,dy):
 tiles={}
 for x,y in pixels:
  x+=dx;y+=dy
  if not(0<=x<512 and 0<=y<256):continue
  k=(x//8,y//8);tiles[k]=tiles.get(k,0)|(1<<((y%8)*8+x%8))
 canonical={0}
 for v in tiles.values():
  b=v.to_bytes(8,'little');h=b.translate(rev)
  canonical.add(min(b,b[::-1],h,h[::-1]))
 return len(canonical)
rows=[{'dx':dx,'dy':dy,'tiles':count(dx,dy)} for dy in range(8) for dx in range(8)]
assert rows[0]['tiles']==258,rows[0]
report={'town_native_phase0':258,'min':min(rows,key=lambda x:x['tiles']),'max':max(rows,key=lambda x:x['tiles']),'phases':rows,'pixels':len(pixels),'lines':len(lines)}
out=root/'generated/stage58-validation/minimap-census';out.mkdir(exist_ok=True,parents=True)
(out/'town-phases.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
