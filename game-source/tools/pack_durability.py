"""Resize original 32px CEL to 16px. Preserve geometry; fit native RGB5 ramps.
8bpp OBJ indices occupy otherwise transparent bank-zero entries 1..15.
"""
from pathlib import Path
from collections import Counter
from itertools import combinations
import hashlib,json,struct
from lz77 import compress,indexed_chunks,decompress
ROOT=Path(__file__).resolve().parents[1]
raw=(ROOT/'assets/durability/DurIcons.CEL').read_bytes()
pal=(ROOT/'assets/durability/town.pal').read_bytes()
assert struct.unpack_from('<I',raw)[0]==16
offsets=struct.unpack_from('<17I',raw,4)
frames=[]
for start,end in zip(offsets,offsets[1:]):
 data=raw[start:end];pixels=[];pos=0
 while pos<len(data):
  run=data[pos];pos+=1
  if run>=128:pixels.extend([None]*(256-run))
  else:pixels.extend(data[pos:pos+run]);pos+=run
 assert len(pixels)==1024
 # CEL is bottom-up; same nearest half-size sample as Pillow: (2n+1).
 reduced=[pixels[(31-(2*y+1))*32+2*x+1] for y in range(16) for x in range(16)]
 frames.append([None if p is None else tuple(c>>3 for c in pal[p*3:p*3+3]) for p in reduced])
def distance(a,b):return sum((x-y)**2 for x,y in zip(a,b))
def fit(hist,n):
 colors=sorted(hist)
 distances=[[distance(c,q) for q in colors] for c in colors]
 best=None
 for subset in combinations(range(len(colors)),n):
  score=sum(hist[c]*min(distances[i][j] for j in subset) for i,c in enumerate(colors))
  candidate=(score,tuple(colors[j] for j in subset))
  if best is None or candidate<best:best=candidate
 return best
hist=[Counter(c for f in frames[start:start+8] for c in f if c not in (None,(0,0,0))) for start in (0,8)]
options=[]
for n in range(5,10):
 a,b=fit(hist[0],n),fit(hist[1],14-n)
 options.append((a[0]+b[0],a[1]+b[1],n))
error,colors,red_shades=min(options);colors=list(colors)+[(0,0,0)]
chunks=[];mapped=[]
for frame in frames:
 indices=[0 if c is None else 1+min(range(15),key=lambda i:distance(c,colors[i])) for c in frame]
 tiled=[indices[y*16+x] for ty in range(2) for tx in range(2) for y in range(ty*8,ty*8+8) for x in range(tx*8,tx*8+8)]
 packed=bytes(tiled[i]|tiled[i+1]<<4 for i in range(0,256,2))
 compressed=compress(packed);assert decompress(compressed)==packed
 chunks.append(compressed);mapped.append(indices)
out=ROOT/'generated'
(out/'native_durability.lz').write_bytes(indexed_chunks(chunks))
rgb5=[r|(g<<5)|(b<<10) for r,g,b in colors]
(out/'native_durability.h').write_text('/* Generated from original CEL by tools/pack_durability.py. */\nextern const uint8_t asset_native_durability[];\nstatic const uint16_t durability_native_colors[15]={'+','.join(hex(c) for c in rgb5)+'};\n')
report={'original_size':[32,32],'size':[16,16],'frames':16,'resize':'nearest; (2x+1,2y+1)','palette':'14 native RGB5 shades + opaque black; transparency separate','red_shades':red_shades,'gold_shades':14-red_shades,'weighted_rgb5_error':error,'source_sha256':hashlib.sha256(raw).hexdigest(),'palette_sha256':hashlib.sha256(pal).hexdigest(),'packed_bytes':(out/'native_durability.lz').stat().st_size,'colors_rgb5':colors,'frames_indices':mapped}
(out/'native-durability.json').write_text(json.dumps(report,indent=2)+'\n')
print('Native atlas:',report['packed_bytes'],'bytes; geometry preserved.')
