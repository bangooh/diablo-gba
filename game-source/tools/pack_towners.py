from pathlib import Path
from PIL import Image
import sys,struct,json
from extract_assets import read_mpq
from pack_gba_assets import obj_palette,rgb15,tile_bytes
import mpyq
specs=[('GRISWOLD',62,63,r'Towners\Smith\SmithN.CEL'),('OGDEN',55,62,r'Towners\TwnF\TwnFN.CEL'),('WOUNDED TOWNSMAN',24,32,r'Towners\Butch\Deadguy.CEL'),('ADRIA',80,20,r'Towners\TownWmn1\Witch.CEL'),('GILLIAN',43,66,r'Towners\TownWmn1\WmnN.CEL'),('WIRT',11,53,r'Towners\TownBoy\PegKid1.CEL'),('PEPIN',55,79,r'Towners\Healer\Healer.CEL'),('CAIN',62,71,r'Towners\Strytell\Strytell.CEL'),('FARNHAM',71,84,r'Towners\Drunk\TwnDrunk.CEL')]
a=mpyq.MPQArchive(sys.argv[1],listfile=False);out=Path(sys.argv[2]);raw=read_mpq(a,r'levels\towndata\town.pal');pal=[tuple(raw[i:i+3]) for i in range(0,768,3)]
header=['#ifndef TOWNERS_H','#define TOWNERS_H','#define NPC_COUNT 9'];counts=[];collision=bytearray((out/'town_collision.bin').read_bytes());palettes=[]
for index,(name,x,y,filename) in enumerate(specs):
 data=read_mpq(a,filename);n=struct.unpack_from('<I',data)[0];offsets=struct.unpack_from('<'+'I'*(n+1),data,4);images=[]
 for start,end in zip(offsets,offsets[1:]):
  frame=data[start:end];pos=struct.unpack_from('<H',frame)[0];pixels=[]
  while pos<len(frame):
   run=frame[pos];pos+=1
   if run>=128:pixels.extend([0]*(256-run))
   else:pixels.extend(frame[pos:pos+run]);pos+=run
  assert len(pixels)%96==0,(name,len(pixels));h=len(pixels)//96;assert h<=128,(name,h)
  im=Image.frombytes('P',(96,h),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM);images.append(im)
 palette,lut=obj_palette(images,pal);palettes.extend(palette);blob=bytearray()
 for im in images:
  canvas=Image.new('P',(128,128),0);canvas.paste(im,(16,128-im.height));canvas=canvas.resize((64,64),Image.Resampling.NEAREST).point(lut);blob.extend(tile_bytes(canvas,4))
 (out/f'npc_{index}.bin').write_bytes(blob);counts.append(n);collision[y*96+x]=0
header += ['static const uint8_t npc_xy[NPC_COUNT][2]={'+','.join('{%d,%d}'%(x,y) for _,x,y,_ in specs)+'};','static const char *const npc_names[NPC_COUNT]={'+','.join('"'+name+'"' for name,_,_,_ in specs)+'};','static const int npc_frames[NPC_COUNT]={'+','.join(map(str,counts))+'};','static const uint8_t *const npc_assets[NPC_COUNT]={'+','.join('asset_npc_'+str(i) for i in range(9))+'};','#endif']
(out/'towners.h').write_text('\n'.join(header)+'\n');(out/'npc_palette.bin').write_bytes(rgb15(palettes));(out/'town_collision.bin').write_bytes(collision)
(out/'npc-report.json').write_text(json.dumps({'names':[s[0] for s in specs],'frames':counts,'sources':[s[3] for s in specs]},indent=2));print(counts)
