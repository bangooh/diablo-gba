"""Restore retail L2S.CEL arches 5/6 on open door pieces 13/17.

Devilution ObjL2Special assigns these overlays; scrollrt draws them after the
ordinary piece. Bake their exact half-size pixels into this renderer's atlas.
No new runtime allocation or collision/door-state change is required.
Run with the user's DIABDAT.MPQ. Existing terrain establishes its palette LUT.
"""
import argparse,collections,hashlib,json,re,struct
from pathlib import Path
from PIL import Image
import mpyq
from extract_assets import read_mpq,pieces
p=argparse.ArgumentParser();p.add_argument('mpq');a=p.parse_args()
root=Path(__file__).resolve().parents[1];gen=root/'generated'
mpq=mpyq.MPQArchive(a.mpq,listfile=False)
native=pieces(read_mpq(mpq,r'levels\l2data\l2.cel'),read_mpq(mpq,r'levels\l2data\l2.min'))
native=[im.resize((32,80),Image.Resampling.NEAREST).tobytes() for im in native]
header=(gen/'world_l2.inc').read_text();pattern=r'(l2_tiles\[\]\s*=\{)([^}]+)(\})'
ids=list(map(int,re.search(pattern,header)[2].split(',')))
raw=(gen/'world_l2.rle').read_bytes();tiles=[raw[i:i+64] for i in range(0,len(raw),64)]
pairs=collections.defaultdict(set)
for n,src in enumerate(native):
 if n in (12,16):continue
 for y in range(80):
  for x in range(32):pairs[src[y*32+x]].add(tiles[ids[n*40+y//8*4+x//8]>>2][y%8*8+x%8])
assert all(len(v)==1 for v in pairs.values()),'terrain palette/geometry differs'
lut={k:next(iter(v)) for k,v in pairs.items()};assert lut[0]==0
cel=read_mpq(mpq,r'levels\l2data\l2s.cel');count=struct.unpack_from('<I',cel)[0]
offset=struct.unpack_from('<'+str(count+1)+'I',cel,4)
lookup={v:i for i,v in enumerate(tiles)};changed=[]
for piece,frame in [(12,5),(16,6)]:
 data=cel[offset[frame-1]:offset[frame]];pos=struct.unpack_from('<H',data)[0];pixels=[]
 while pos<len(data):
  run=data[pos];pos+=1
  if run>=128:pixels.extend([0]*(256-run))
  else:pixels.extend(data[pos:pos+run]);pos+=run
 assert len(pixels)==64*160
 im=Image.frombytes('P',(64,160),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM).resize((32,80),Image.Resampling.NEAREST)
 overlay=im.tobytes();assert all(v in lut for v in overlay)
 merged=bytes(lut[o or b] for o,b in zip(overlay,native[piece]))
 for ty in range(10):
  for tx in range(4):
   tile=b''.join(merged[y*32+tx*8:y*32+tx*8+8] for y in range(ty*8,ty*8+8))
   kind=0 if not any(tile) else 1 if all(tile) else 2
   if tile not in lookup:lookup[tile]=len(tiles);tiles.append(tile)
   ids[piece*40+ty*4+tx]=lookup[tile]*4+kind
 changed.append({'piece':piece+1,'arch_frame':frame,'overlay_pixels':sum(v!=0 for v in overlay)})
header=re.sub(pattern,lambda m:m[1]+','.join(map(str,ids))+m[3],header)
(gen/'world_l2.inc').write_text(header);(gen/'world_l2.rle').write_bytes(b''.join(tiles))
report={'source':'levels/l2data/l2s.cel','sha256':hashlib.sha256(cel).hexdigest(),'pieces':changed,'added_tile_bytes':len(tiles)*64-len(raw),'palette_mapping':'Exact existing terrain mapping; zero conflicts.'}
(gen/'l2_arch_restoration.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
