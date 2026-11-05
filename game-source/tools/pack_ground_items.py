"""Original resting item CEL frames, following Devilution ItemCAnimTbl.
Rebuild from source_assets/ground_items; optional MPQ argument refreshes CELs.
Shared ground tiles feed dungeon BG decals; the pending town rewards use OBJ tiles868..879.
Bank15 indices1..5 remain map markers;6..15 are bright item colours.
"""
from pathlib import Path
import json, struct, sys, hashlib
from PIL import Image
from lz77 import compress, decompress
from pack_gba_assets import tile_bytes, rgb15
from item_roster import IDS, binding, art_bindings

root=Path(__file__).resolve().parents[1];src=root/'source_assets/ground_items';out=root/'generated'
src.mkdir(parents=True,exist_ok=True)
bindings=art_bindings()
mapping={'cursor_ids':[b['cursor'] for b in bindings],'ground_names':[b['ground'] for b in bindings],'source_revision':'9f01757243412bb154d02d9af65c804159e6e0ea'}
(src/'mapping.json').write_text(json.dumps(mapping,indent=2)+'\n');names=list(dict.fromkeys(mapping['ground_names']))
if len(sys.argv)>1:
 import mpyq
 from extract_assets import read_mpq
 a=mpyq.MPQArchive(sys.argv[1],listfile=False)
 for name in names:(src/(name+'.CEL')).write_bytes(read_mpq(a,'Items\\'+name+'.CEL'))
 (src/'l1_1.pal').write_bytes(read_mpq(a,'Levels\\L1Data\\L1_1.PAL'))
pal=(src/'l1_1.pal').read_bytes();original=[];bright=[];samples=[];frames=[];native_shapes=[]
for name in names:
 raw=(src/(name+'.CEL')).read_bytes();frame=11 if name.lower()=='rock' else struct.unpack_from('<I',raw)[0];frames.append(frame)
 lo,hi=struct.unpack_from('<II',raw,frame*4);data=raw[lo:hi];p=struct.unpack_from('<H',data)[0];pixels=bytearray()
 while p<len(data):
  run=data[p];p+=1
  if run>=128:pixels.extend(bytes(256-run))
  else:pixels.extend(data[p:p+run]);p+=run
 assert len(pixels)%96==0
 im=Image.frombytes('P',(96,len(pixels)//96),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
 im.putpalette(pal);im=im.crop(im.getbbox());scale=min(.5,32/im.width,15/im.height)
 im=im.resize((max(1,round(im.width*scale)),max(1,round(im.height*scale))),Image.Resampling.NEAREST)
 native=Image.new('P',(64,64));native.paste(im,((64-im.width)//2,63-im.height));native_shapes.append(native.tobytes())
 mask=im.point([0]+[255]*255).convert('L');rgb=im.convert('RGB');lit=rgb
 colors=[c for c,m in zip(lit.getdata(),mask.getdata()) if m];samples.extend(colors*max(1,600//len(colors)))
 original.append((rgb,mask));bright.append((lit,mask))
sample=Image.new('RGB',(len(samples),1));sample.putdata(samples)
q=sample.quantize(colors=10,method=Image.Quantize.MEDIANCUT);p=q.getpalette()[:30];colors=[tuple(p[i:i+3]) for i in range(0,30,3)]
palette=[(0,0,0)]*6+colors;blobs=[];stats=[];offsets=[];wide=[];tile=832;preview=Image.new('RGB',(len(names)*32,64),(22,22,26))
for i,((lit,mask),(orig,_)) in enumerate(zip(bright,original)):
 indices=[]
 for c,m in zip(lit.getdata(),mask.getdata()):
  indices.append(6+min(range(10),key=lambda j:sum((c[k]-colors[j][k])**2 for k in range(3))) if m else 0)
 image=Image.frombytes('P',lit.size,bytes(indices));image.putpalette(bytes(v for c in palette for v in c)+bytes(720))
 width=32 if image.width>16 else 16;count=width//4
 if tile<884 and tile+count>880:blobs.append(bytes((884-tile)*32));tile=884
 offsets.append(tile);wide.append(int(width==32));tile+=count
 canvas=Image.new('P',(width,16));canvas.paste(image,((width-image.width)//2,15-image.height));blobs.append(tile_bytes(canvas,4))
 preview.paste(orig,(i*32+(32-orig.width)//2,16-orig.height//2),mask)
 preview.paste(image.convert('RGB'),(i*32+(32-image.width)//2,48-image.height//2),mask)
 before=[sum(c)/3 for c,m in zip(orig.getdata(),mask.getdata()) if m];after=[sum(palette[v])/3 for v in indices if v]
 stats.append({'name':names[i],'rest_frame':frames[i],'size':list(lit.size),'mean_original':round(sum(before)/len(before),1),'mean_bright':round(sum(after)/len(after),1)})
preview.save(src/'ground-comparison.png')
raw=b''.join(blobs);(out/'ground_items.lz').write_bytes(compress(raw));assert decompress((out/'ground_items.lz').read_bytes())==raw
(out/'ground_item_palette.lz').write_bytes(compress(rgb15(palette)))
indices=[names.index(n) for n in mapping['ground_names']]
assert all(t+c<=880 or t>=884 for t,c in zip(offsets,[8 if w else 4 for w in wide]))
(out/'ground_items.h').write_text('#pragma once\n#include <stdint.h>\nextern const uint8_t asset_ground_items[];\nextern const uint8_t asset_ground_item_palette[];\n#define GROUND_ITEM_BYTES '+str(len(raw))+'\nstatic const uint8_t ground_item_image[]={'+','.join(map(str,indices))+'};\nstatic const uint16_t ground_item_tiles['+str(len(names))+']={'+','.join(map(str,offsets))+'};\nstatic const uint8_t ground_item_wide['+str(len(names))+']={'+','.join(map(str,wide))+'};\n/* Packed tiles feed floor_decals.rle and the pending town quest reward OBJ. */\n')
report={'source_revision':mapping['source_revision'],'supported_kinds':len(IDS),'shared_images':len(names),'tile_range':[832,832+len(raw)//32-1],'reserved_tiles':[880,881,882,883],'obj_bytes':len(raw),'palette_bank':15,'palette_indices':[6,15],'runtime':'BG row decals plus two pending town quest reward OBJs','per_frame_decompression':False,'assets':stats,'source_sha256':{n:hashlib.sha256((src/(n+'.CEL')).read_bytes()).hexdigest() for n in names}}
# Dungeon items map directly from native 256-color art to static BG slots.
# They no longer pass through the ten-color town-OBJ palette or brightening.
from floor_codec import encode
bg=struct.unpack('<256H',decompress((out/'bg_palette.lz').read_bytes()))
bg_rgb=[tuple(((v>>s)&31)*255//31 for s in (0,5,10)) for v in bg]
slots=list(range(1,256)) if '--legacy-palette' in sys.argv else [v for v in json.loads((out/'background-palette-review/audit.json').read_text())['palette']['static_slots'] if v]
native_rgb=[tuple(pal[i:i+3]) for i in range(0,768,3)]
maps=[]
for selected in range(2):
 lut=[0]
 for rgb in native_rgb[1:]:
  c=tuple(min(255,v+(255-v)*.35+16) for v in rgb) if selected else rgb
  lut.append(min(slots,key=lambda j:sum((bg_rgb[j][k]-c[k])**2 for k in range(3))))
 maps.extend([bytes(lut)]*len(names))
blob,_=encode(native_shapes*2,maps);(out/'ground_items66.rle').write_bytes(blob)
report.update(brightness_gain=1.0,brightness_offset=0,bg_native_palette=True,bg_bytes=len(blob),bg_frames=len(names)*2)
(out/'ground-items-report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
