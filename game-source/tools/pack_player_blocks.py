"""Extract native shield block animations for all three classes and armor tiers."""
from pathlib import Path
import sys,struct,json,hashlib
import mpyq
from extract_assets import read_mpq
from pack_player_armor import converted,CLASSES
from lz77 import indexed,decompress
r=Path(__file__).resolve().parents[1];out=r/'generated';src=r/'source_assets/player_blocks';src.mkdir(exist_ok=True)
a=mpyq.MPQArchive(sys.argv[1],listfile=False) if len(sys.argv)>1 else None
rawpal=(r/'source_assets/weapon_heroes/l1_1.pal').read_bytes();pal=[tuple(rawpal[i:i+3]) for i in range(0,768,3)]
header=['#pragma once'];rows=[];report=[]
for cls,(folder,letter,palfile) in enumerate(CLASSES):
 colors=struct.unpack('<16H',decompress((out/(palfile+'.lz')).read_bytes())[:32]);rgb=[tuple(((c>>s)&31)*255//31 for s in (0,5,10)) for c in colors]
 lut=[0]+[min(range(1,16),key=lambda j:sum((rgb[j][k]-pal[i][k])**2 for k in range(3))) for i in range(1,256)]
 variants=[]
 for armor in 'LMH':
  for weapon in 'NUSDBMHTA':
   if weapon not in 'UDH':variants.append('{0,0}');continue
   prefix=letter+armor+weapon;path=f'PlrGFX\\{folder}\\{prefix}\\{prefix}BL.CL2';p=src/(prefix+'BL.CL2')
   if a:p.write_bytes(read_mpq(a,path))
   raw=p.read_bytes();frames,_,count=converted(raw,96,lut,cls,'bl');assert count==[2,4,6][cls]
   name='hero_'+prefix.lower()+'_bl';packed=indexed(b''.join(frames),2048);(out/(name+'.lz')).write_bytes(packed)
   header.append('extern const uint8_t asset_'+name+'[];');variants.append('{asset_'+name+','+str(count)+'}')
   report.append({'asset':name,'mpq_path':path,'frames':count,'directions':8,'source_sha256':hashlib.sha256(raw).hexdigest(),'decoded_sha256':hashlib.sha256(b''.join(frames)).hexdigest(),'packed_bytes':len(packed)})
 rows.append('{'+','.join(variants)+'}')
header.append('static const struct {const uint8_t *data;unsigned frames;} block_hero_art[3][27]={'+',\n'.join(rows)+'};')
(out/'player_block_art.h').write_text('\n'.join(header)+'\n');(out/'player-block-report.json').write_text(json.dumps(report,indent=2)+'\n');print('Block clips:',len(report),'frames:',sum(x['frames']*8 for x in report),'bytes:',sum(x['packed_bytes'] for x in report))
