"""Derive runtime palettes from native CL2 weights and ordered base/unique TRNs."""
import argparse,json,struct,hashlib
from pathlib import Path
import numpy as np
import mpyq
from extract_assets import read_mpq,cl2_frames
from pack_gba_assets import obj_palette,rgb15
from pack_difficulty import retail,table
from lz77 import compress
from legacy_monster_palette_style import OVERRIDES,rgb555
ROOT=Path(__file__).resolve().parents[1]

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--mpq',type=Path,required=True);a=p.parse_args()
 records=json.loads((ROOT/'source_assets/monster_stage71/catalog.json').read_text())['records']
 native_rows=table(retail((ROOT/'source_assets/monster_catalog/monstdat.cpp').read_text()),'MonsterData monsterdata[] = {')
 archive=mpyq.MPQArchive(str(a.mpq),listfile=False);native=read_mpq(archive,'Levels\\L1Data\\L1_1.PAL');colors=np.frombuffer(native,np.uint8).reshape(256,3);pal=[tuple(map(int,c)) for c in colors];groups={};rows=[]
 for r in records:
  if r.get('role')=='minion':continue
  graphic=r['graphic'].lower()
  if graphic not in groups:
   images=[]
   for state,n in zip('nwahds',r['frames']):
    if n:
     for direction in cl2_frames(read_mpq(archive,r['graphic'].replace('%c',state)),r['width']):images.extend(direction)
   _,lut=obj_palette(images,pal);weights=np.zeros(256,np.int64)
   for im in images:weights+=np.bincount(np.frombuffer(im.tobytes(),np.uint8),minlength=256)
   groups[graphic]=(np.array(lut),weights)
  lut,weights=groups[graphic]
  source=native_rows[r['native_id']][7];base=None if source=='NULL' else json.loads(source)
  trn=bytes(range(256))
  if base:
   # Native loader treats 255 in the base translation as black.
   t=read_mpq(archive,base);t=bytes(0 if v==255 else v for v in t);trn=bytes(t[v] for v in trn)
  unique=r.get('translation')
  if unique and (not base or unique.lower()!=base.lower()) and r['kind']!=33:
   t=read_mpq(archive,unique);trn=bytes(t[v] for v in trn)
  mapped=colors[np.frombuffer(trn,np.uint8)].astype(np.int64);fitted=[(0,0,0)]
  for slot in range(1,16):
   active=lut==slot;total=weights[active].sum();fitted.append(tuple(map(int,np.floor((weights[active,None]*mapped[active]).sum(axis=0)/max(1,total)+.5))))
  bright=[(0,0,0)]+[tuple(min(255,round(c+(255-c)*.18)) for c in rgb) for rgb in fitted[1:]]
  words=list(struct.unpack('<32H',rgb15(fitted+bright)))
  if r['kind'] in (26,39):
   key='obsidian_lord' if r['kind']==26 else 'devil_kin_spear'
   for bank,(_,new) in enumerate(OVERRIDES[key]):words[bank*16+15]=rgb555(new)
  data=compress(struct.pack('<32H',*words));name=f"m71_pal_{r['kind']}.lz";(ROOT/'generated'/name).write_bytes(data)
  rows.append(dict(file=name,sha256=hashlib.sha256(data).hexdigest()))
 archive.file.close();(ROOT/'generated/native-monster-palette-report.json').write_text(json.dumps(rows,indent=2)+'\n');print('PASS:',len(rows),'native palettes')
if __name__=='__main__':main()
