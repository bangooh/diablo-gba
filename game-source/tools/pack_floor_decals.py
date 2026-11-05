"""Original final death frames and ground items as row-compressed BG decals.
Rows can be clipped directly from ROM without adding an EWRAM sprite cache.
"""
from pathlib import Path
import struct,sys,json,hashlib
from PIL import Image
from extract_assets import read_mpq,cl2_frames
from lz77 import decompress
r=Path(__file__).resolve().parents[1];out=r/'generated';src=r/'source_assets/corpses';src.mkdir(exist_ok=True)
if len(sys.argv)>1:
 import mpyq
 archive=mpyq.MPQArchive(sys.argv[1],listfile=False)
else:archive=None
def get(name,path):
 f=src/name
 if archive:f.write_bytes(read_mpq(archive,path))
 return f.read_bytes()
raw=get('l1.pal','Levels\\L1Data\\L1_1.PAL');rgb=[tuple(raw[i:i+3]) for i in range(0,768,3)]
bg=struct.unpack('<256H',decompress((out/'bg_palette.lz').read_bytes()));colors=[tuple(((c>>s)&31)*255//31 for s in (0,5,10)) for c in bg]
def nearest(c):return min(range(1,256),key=lambda j:sum((colors[j][k]-c[k])**2 for k in range(3)))
lut=[0]+[nearest(tuple(v*.80 for v in c)) for c in rgb[1:]];pictures=[];death_pictures=[];death_starts=[];death_counts=[];shapes=[];shape_palettes=[];death_shapes=[];death_palettes=[]
def death_group(groups,translation,width):
 death_starts.append(len(death_pictures));death_counts.append(len(groups[0]))
 for group in groups:
  assert len(group)==death_counts[-1]
  for im in group:
   source_im=im;im=im.point(translation);side=max(128,width,im.height);canvas=Image.new("P",(side,side));canvas.paste(im,((side-im.width)//2,side-im.height))
   death_pictures.append(canvas.resize((64,64),Image.Resampling.NEAREST).point(lut).tobytes())
   shape=Image.new("P",(side,side));shape.paste(source_im,((side-source_im.width)//2,side-source_im.height));death_shapes.append(shape.resize((64,64),Image.Resampling.NEAREST).tobytes());death_palettes.append(bytes(lut[v] for v in translation))
actors=[('Zombie','Zombie',None),('SkelAxe','SklAx','White.TRN'),('FalSpear','Phall','FallenT.TRN'),('FalSword','Fall','FallenT.TRN'),('Scav','Scav',None),('SkelSd','SklSr','White.TRN'),('FatC','FatC','..\\Monsters\\GENRL.TRN')]
for folder,stem,trn in actors:
 b=get(stem+'d.CL2',f'Monsters\\{folder}\\{stem}d.CL2')
 if trn:
  path='Monsters\\Monsters\\GENRL.TRN' if folder=='FatC' else f'Monsters\\{folder}\\{trn}'
  translation=list(get(folder+'.TRN',path))
 else:translation=list(range(256))
 groups=cl2_frames(b,128);death_group(groups,translation,128)
 for group in groups:
  im=group[-1].point(translation);side=max(128,im.width,im.height);canvas=Image.new('P',(side,side));canvas.paste(im,((side-im.width)//2,side-im.height));pictures.append(canvas.resize((64,64),Image.Resampling.NEAREST).point(lut).tobytes())
assert len(pictures)==56
variants=json.loads((r/'source_assets/monster_catalog/monsters.json').read_text())['monsters'][7:]
for d in variants:
 folder=r/'source_assets/monster_catalog'/d['key'];translation=list((folder/'translation.trn').read_bytes())
 groups=cl2_frames((folder/'d.cl2').read_bytes(),d.get('width',128));death_group(groups,translation,d.get('width',128))
 for group in groups:
  im=group[-1].point(translation);side=max(128,d.get('width',128),im.height);canvas=Image.new('P',(side,side));canvas.paste(im,((side-im.width)//2,side-im.height));pictures.append(canvas.resize((64,64),Image.Resampling.NEAREST).point(lut).tobytes())
# Four leaders have separate colors while retaining their base CL2 geometry.
# Their pack members deliberately use the existing base corpse kind. Retail
# applies a base TRN to the CL2 before applying the unique's draw translation.
from pack_unique_monsters import GROUPS,TRANSLATIONS,translation as compose_translation
ordinary=json.loads((r/'source_assets/monster_catalog/monsters.json').read_text())['monsters']
assert len(ordinary)==50
unique_records=[]
for kind,base_kind,key,name,trn in GROUPS:
 d=ordinary[base_kind];folder=r/'source_assets/monster_catalog'/d['key']
 translation=compose_translation((folder/'translation.trn').read_bytes(),(TRANSLATIONS/(trn+'.TRN')).read_bytes())
 # A translated black pixel remains opaque. Zero in the source is the only
 # transparent index; the BG decoder must not punch holes in Blackstorm.
 mapping=bytes([0]+[lut[v] if v else nearest((0,0,0)) for v in translation[1:]])
 groups=cl2_frames((folder/'d.cl2').read_bytes(),d['width'])
 death_starts.append(len(death_pictures));death_counts.append(len(groups[0]))
 for group in groups:
  assert len(group)==death_counts[-1]
  for im in group:
   side=max(128,d['width'],im.height);canvas=Image.new('P',(side,side));canvas.paste(im,((side-im.width)//2,side-im.height))
   shape=canvas.resize((64,64),Image.Resampling.NEAREST).tobytes();mapped=bytes(mapping[v] for v in shape)
   assert all(bool(a)==bool(b) for a,b in zip(shape,mapped))
   death_pictures.append(mapped);death_shapes.append(shape);death_palettes.append(mapping)
  pictures.append(mapped)
 unique_records.append({**d,'kind':kind,'key':key,'name':name})
corpse_frames=len(pictures)
for start,count in zip(death_starts,death_counts):
 for direction in range(8):
  i=start+(direction+1)*count-1;shapes.append(death_shapes[i]);shape_palettes.append(death_palettes[i])
shapes+=death_shapes;shape_palettes+=death_palettes
records=ordinary+unique_records
assert death_counts==[d["frames"][4] for d in records]
(out/"monster_death.h").write_text("#pragma once\n#define MONSTER_DEATH_TYPES "+str(len(records))+"\nstatic const uint16_t monster_death_base[]={"+",".join(str(corpse_frames+i) for i in death_starts)+"};\nstatic const uint8_t monster_death_frames[]={"+",".join(map(str,death_counts))+"};\nstatic const uint8_t monster_death_ticks[]={"+",".join(str(d["rates"][4]+1) for d in records)+"};\n")
pictures+=death_pictures
item_base=len(pictures)
import re
h=(out/'ground_items.h').read_text()
def values(name):return list(map(int,re.search(name+r'\[\d+\]=\{([^}]+)',h)[1].split(',')))
raw=decompress((out/'ground_items.lz').read_bytes());p=struct.unpack('<16H',decompress((out/'ground_item_palette.lz').read_bytes()));pal=[tuple(((c>>s)&31)*255//31 for s in (0,5,10)) for c in p]
for selected in range(2):
 mapping=[0]+[nearest(tuple(min(255,v+(255-v)*.35+16) for v in c) if selected else c) for c in pal[1:]]
 for tile,wide in zip(values('ground_item_tiles'),values('ground_item_wide')):
  w=32 if wide else 16;b=raw[(tile-832)*32:(tile-832)*32+w*8];canvas=bytearray(4096)
  for y in range(16):
   for x in range(w):
    n=((y//8)*(w//8)+x//8)*32+(y%8)*4+(x%8)//2;v=(b[n]>>((x&1)*4))&15
    canvas[(y+48)*64+(64-w)//2+x]=mapping[v]
  pictures.append(bytes(canvas))
from floor_codec import encode
shapes+=pictures[len(shapes):];shape_palettes+=[bytes(range(256))]*(len(pictures)-len(shape_palettes))
blob,codec_report=encode(shapes,shape_palettes)
from floor_palette_style import apply as style_floor, write_report as write_style_report
blob,style_report=style_floor(blob,bg,[corpse_frames+i for i in death_starts],death_counts)
write_style_report(out,style_report)
# Match the preview/decoded hash to the scoped post-encode palette layer.
for style in style_report['kinds']:
 kind=style['kind'];target=style['target_bg_index']
 affected=list(range(kind*8,kind*8+8))+list(range(corpse_frames+death_starts[kind],corpse_frames+death_starts[kind]+death_counts[kind]*8))
 for frame in affected:pictures[frame]=bytes(target if v==242 else v for v in pictures[frame])
frames=pictures
(out/'floor_decals.rle').write_bytes(blob)
(out/'floor_decals.h').write_text(f'#pragma once\nextern const uint8_t asset_floor_decals[];\nenum {{ FLOOR_ITEM_BASE={item_base}, FLOOR_BRIGHT_BASE={item_base+len(values('ground_item_tiles'))} }};\n')
preview=Image.new('P',(8*64,(corpse_frames//8)*64));preview.putpalette(bytes(v for c in colors for v in c))
for i,b in enumerate(pictures[:corpse_frames]):preview.paste(Image.frombytes('P',(64,64),b),((i%8)*64,(i//8)*64))
preview.save(src/'corpses.png');print(json.dumps({'pictures':len(frames),'row_compressed_bytes':len(blob),'corpse_directions':8,'corpse_kinds':corpse_frames//8}))

(out/"floor-decals-report.json").write_text(json.dumps({"frames":len(frames),"decoded_sha256":hashlib.sha256(b"".join(pictures)).hexdigest(),"encoded_bytes":len(blob),"format":"palette-row-rle-v4",**codec_report},indent=2)+"\n")
