"""Retain native L1 IDs but pack only raster rows used by the King's lair."""
from pathlib import Path
import struct,json
from PIL import Image
from extract_assets import tile_frame
r=Path(__file__).resolve().parents[1];src=r/'source_assets/worldgen';out=r/'generated'
macros=struct.unpack('<'+'H'*((src/'L1.TIL').stat().st_size//2),(src/'L1.TIL').read_bytes())
used_macros={13,22}
for name in ['SklKng1','SklKng2']:
 b=(r/'source_assets/quest_remaining'/(name+'.DUN')).read_bytes();w,h=struct.unpack_from('<HH',b)
 used_macros.update(v or 13 for v in struct.unpack_from('<'+'H'*(w*h),b,4))
used={0,392,394,407}
for n in used_macros:used.update(macros[(n-1)*4:n*4])
cel=(src/'L1.CEL').read_bytes();mini=(src/'L1.MIN').read_bytes();sol=(src/'L1.SOL').read_bytes();pal=(src/'L1_1.PAL').read_bytes()
rgb=[tuple(pal[i:i+3]) for i in range(0,768,3)]
lut=[0]+[min(range(1,256),key=lambda j:sum((rgb[j][k]-rgb[i][k]*.5)**2 for k in range(3))) for i in range(1,256)]
count=len(mini)//20;blob=bytearray();atlas={};tiles=[];flags=[];occludes=[0]*count;rows=[0]*count;piece_rows={}
for n in sorted(used):
 im=Image.new('P',(64,160))
 for j,v in enumerate(struct.unpack_from('<10H',mini,n*20)):
  t=tile_frame(cel,v);im.paste(t,((j&1)*32,(j//2)*32),Image.frombytes('L',t.size,bytes(255 if v else 0 for v in t.tobytes())))
 occludes[n]=int(bool(im.getbbox() and im.getbbox()[1]<128));rows[n]=len(tiles)//40
 data=im.resize((32,80),Image.Resampling.NEAREST).point(lut).tobytes()
 for y in range(10):
  for x in range(4):
   block=bytes(data[(y*8+j)*32+x*8+i] for j in range(8) for i in range(8))
   if block not in atlas:atlas[block]=len(blob)//64;blob.extend(block)
   tiles.append(atlas[block]);flags.append(0 if not any(block) else 1 if all(block) else 2)
compact_tiles=[];compact_flags=[]
for n in sorted(used):
 start=rows[n]*40;key=tuple(tiles[start:start+40])
 if key not in piece_rows:
  piece_rows[key]=len(compact_tiles)//40;compact_tiles.extend(key);compact_flags.extend(flags[start:start+40])
 rows[n]=piece_rows[key]
tiles=[(t<<2)|f for t,f in zip(compact_tiles,compact_flags)]
h=['extern const uint8_t asset_world_l1[];']
for ty,name,values in [('uint8_t','solid',[v&1 for v in sol[:count]]),('uint8_t','occludes',occludes),('uint16_t','macros',macros),('uint16_t','tiles',tiles),('uint16_t','render_rows',rows)]:
 h.append(f'static const {ty} l1_{name}[]={{'+','.join(map(str,values))+'};')
h.append(f'static const WorldArt l1_art={{asset_world_l1,l1_solid,l1_occludes,l1_macros,l1_tiles,{len(macros)//4},{count},80,l1_render_rows}};')
(out/'world_l1.inc').write_text('\n'.join(h)+'\n');(out/'world_l1.rle').write_bytes(blob)
report={'native_ids_retained':count,'rendered_ids':sorted(used),'render_rows':len(piece_rows),'native_rendered_count':len(used),'flags_encoding':'tile word low two bits','raster_bytes':len(blob),'table_bytes':len(sol[:count])*2+len(macros)*2+len(tiles)*2+len(rows)*2,'lossless_tile_dedup':True}
(r/'source_assets/quest_remaining/king-terrain-report.json').write_text(json.dumps(report,indent=2)+'\n');print({k:v for k,v in report.items() if k!='rendered_ids'})

p=src/'terrain-report.json'
if p.exists():
 records=json.loads(p.read_text())
 for row in records:
  if row['region']==1:row.update(ROM_bytes=len(blob),rendered_ids=len(used),compact_rows=len(piece_rows),packed_flags=True)
 p.write_text(json.dumps(records,indent=2)+'\n')
