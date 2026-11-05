"""Native L5 floor-family variants, streamed from ROM; native chest size art.
No growth of the already full resident terrain cache. MPQ needed only to repack.
"""
from pathlib import Path
import sys,re,struct,json,shutil
import mpyq
from PIL import Image
from extract_assets import read_mpq,pieces
from lz77 import indexed
r=Path(__file__).resolve().parents[1];src=r/'source_assets/cathedral_details';src.mkdir(exist_ok=True);out=r/'generated'
a=mpyq.MPQArchive(sys.argv[1],listfile=False)
if len(sys.argv)>2:shutil.copyfile(Path(sys.argv[2])/'Source/drlg_l1.cpp',src/'drlg_l1.cpp')
raw=read_mpq(a,'Levels\\L1Data\\L1_1.PAL');pal=[tuple(raw[i:i+3]) for i in range(0,768,3)];til=read_mpq(a,'Levels\\L1Data\\L1.TIL');sol=read_mpq(a,'Levels\\L1Data\\L1.SOL');ims=pieces(read_mpq(a,'Levels\\L1Data\\L1.CEL'),read_mpq(a,'Levels\\L1Data\\L1.MIN'))
source=(src/'drlg_l1.cpp').read_text();btypes=[int(v) for v in re.findall(r'\d+',re.search(r'const BYTE L5BTYPES\[206\] = \{(.*?)\};',source,re.S)[1])]
assert len(btypes)==206
lut=[0]+[min(range(1,256),key=lambda j:sum((pal[j][k]-pal[i][k]*.50)**2 for k in range(3))) for i in range(1,256)]
ids=[];tiles=[];flags=[];atlas={};blob=bytearray();sheet=[]
basepieces=struct.unpack_from('<4H',til,(13-1)*8);base_mask=[sol[p]&1 for p in basepieces]
for mid,family in enumerate(btypes):
 if family!=13 or mid==13:continue
 ps=struct.unpack_from('<4H',til,(mid-1)*8)
 if [sol[p]&1 for p in ps]!=base_mask:continue
 if any(ims[p].getbbox() and ims[p].getbbox()[1]<128 for p in ps):continue
 ids.append(mid)
 for p in ps:
  im=ims[p];im.putpalette(raw);im.save(src/f'piece-{p+1}.png');im=im.resize((32,80),Image.Resampling.NEAREST).point(lut);data=im.tobytes();sheet.append(im)
  for y in range(10):
   for x in range(4):
    tile=bytes(data[(y*8+j)*32+x*8+i] for j in range(8) for i in range(8))
    if tile not in atlas:atlas[tile]=len(blob)//64;blob.extend(tile)
    tiles.append(atlas[tile]);flags.append(0 if not any(tile) else 1 if all(tile) else 2)
dart_count=int(re.search(r'DART_COUNT=(\d+)',(out/'dungeon_art.h').read_text())[1])
assert ids and dart_count+len(ids)*4<65535
(out/'floor_variants.rle').write_bytes(blob)
h=['#pragma once','extern const uint8_t asset_floor_variants[];',f'#define FLOOR_VARIANT_COUNT {len(ids)}','static const uint8_t floor_variant_ids[]={'+','.join(map(str,ids))+'};','static const uint16_t floor_variant_tiles[]={'+','.join(map(str,tiles))+'};','static const uint8_t floor_variant_flags[]={'+','.join(map(str,flags))+'};']
(out/'floor_variants.h').write_text('\n'.join(h)+'\n')
# Preserve the original container + Butcher object frame IDs.
prop=bytearray((out/'prop_frames.bin').read_bytes()[:37*4096]);assert len(prop)==37*4096
header=['#pragma once']
for name in ['Chest2','Chest3']:
 data=read_mpq(a,'Objects\\'+name+'.CEL');(src/(name+'.CEL')).write_bytes(data);count=struct.unpack_from('<I',data)[0];assert count==6;off=struct.unpack_from('<7I',data,4);header.append(f'#define PROP_{name.upper()}_BASE {len(prop)//4096}')
 for lo,hi in zip(off,off[1:]):
  f=data[lo:hi];p=struct.unpack_from('<H',f)[0];pixels=bytearray()
  while p<len(f):
   c=f[p];p+=1
   if c>=128:pixels.extend(bytes(256-c))
   else:pixels.extend(f[p:p+c]);p+=c
  assert len(pixels)%96==0
  im=Image.frombytes('P',(96,len(pixels)//96),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM);im=im.resize((48,im.height//2),Image.Resampling.NEAREST);assert im.height<=64
  canvas=Image.new('P',(64,64));canvas.paste(im,(8,64-im.height));prop.extend(canvas.tobytes())
(out/'cathedral_props.h').write_text('\n'.join(header)+'\n');(out/'prop_frames.bin').write_bytes(prop);(out/'prop_frames.lz').write_bytes(indexed(bytes(prop),4096));p=out/'packed_assets.h';p.write_text(re.sub(r'#define RAW_PROP_FRAMES \d+',f'#define RAW_PROP_FRAMES {len(prop)}',p.read_text()))
report={'floor_macros':ids,'rule':'L5BTYPES[id]==13; exact base SOL footprint; no overhang; never stairs/walls','rom_tile_bytes':len(blob),'resident_RAM_growth':0,'object_frames':len(prop)//4096};(src/'report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
