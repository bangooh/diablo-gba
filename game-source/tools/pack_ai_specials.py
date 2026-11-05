"""Import retail CL2 special animations into the existing hardware palettes.

Usage: python tools/pack_ai_specials.py DIABDAT.MPQ
Existing ordinary clips, palettes and the shared actor pool are immutable.
Canonical geometry follows the verified Stage20 palette-sharing groups.
"""
from pathlib import Path
import argparse,hashlib,json,struct,tempfile
import mpyq
from PIL import Image
from extract_assets import read_mpq,cl2_frames
from pack_gba_assets import tile_bytes
from pack_hot_monsters import Decoder,raw_tiles
from lz77 import decompress,indexed

ROOT=Path(__file__).resolve().parents[1]
GROUPS={
 'rhino':('horned_demon',[23,24,25,26]),
 'sneak':('illusion_weaver',[28,49]),
 'goat':('stone_clan',[15,16,17,18,46]),
 'fat':('overlord',[32]),
 'fallen_spear':('blue_fallen_spear',[2,29,30,39]),
}
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('mpq',type=Path);a=ap.parse_args()
 archive=mpyq.MPQArchive(str(a.mpq),listfile=False)
 rawpal=read_mpq(archive,'Levels\\L1Data\\L1_1.PAL')
 source_rgb=[tuple(c>>3 for c in rawpal[i:i+3]) for i in range(0,768,3)]
 records=json.loads((ROOT/'source_assets/monster_catalog/monsters.json').read_text())['monsters']
 by_key={r['key']:r for r in records};out=ROOT/'generated';report=[]
 header=['#pragma once','#include <stdint.h>']
 with tempfile.TemporaryDirectory() as td:
  decoder=Decoder(td,(out/'hero_frame_pool.lz').read_bytes())
  for name,(key,kinds) in GROUPS.items():
   m=by_key[key];path=m['graphic'].replace('%c','s');source=read_mpq(archive,path)
   folder=ROOT/'source_assets/monster_catalog'/key;folder.mkdir(exist_ok=True)
   (folder/'s.cl2').write_bytes(source)
   translation=read_mpq(archive,m['translation']) if m['translation'] else bytes(range(256))
   palette=struct.unpack('<32H',decompress((out/(key+'_palette.lz')).read_bytes()))
   rgb=[tuple((c>>shift)&31 for shift in (0,5,10)) for c in palette[:16]]
   lut=[0]+[min(range(1,16),key=lambda j:sum((rgb[j][c]-source_rgb[translation[i]][c])**2 for c in range(3))) for i in range(1,256)]
   groups=cl2_frames(source,m['width']);count=m['frames'][5]
   assert len(groups)==8 and all(len(g)==count for g in groups)
   frames=[]
   for direction in groups:
    for im in direction:
     side=max(128,m['width'],im.height);canvas=Image.new('P',(side,side))
     canvas.paste(im,((side-im.width)//2,side-im.height))
     scaled=canvas.resize((64,64),Image.Resampling.NEAREST);mapped=scaled.point(lut)
     assert all(bool(x)==bool(y) for x,y in zip(scaled.tobytes(),mapped.tobytes()))
     frames.append(tile_bytes(mapped,4))
   sparse=raw_tiles(frames);compressed=indexed(b''.join(frames),2048)
   # Frequent charge loops get direct tile records. Infrequent transitions
   # use independently compressed frames to stay within the 32 MiB cartridge.
   blob=sparse if name=='rhino' else compressed
   assert decoder.frames(blob,len(frames))==frames
   asset='ai_'+name+'_s';(out/(asset+'.lz')).write_bytes(blob)
   header.append('extern const uint8_t asset_'+asset+'[];')
   report.append({'asset':asset,'canonical':key,'kinds':kinds,'mpq_path':path,
    'source_sha256':hashlib.sha256(source).hexdigest(),'source_bytes':len(source),
    'directions':8,'frames_per_direction':count,'decoded_sha256':hashlib.sha256(b''.join(frames)).hexdigest(),
    'codec':'SPR2' if name=='rhino' else 'indexed sparse LZ','bytes':len(blob),
    'sha256':hashlib.sha256(blob).hexdigest(),'raw_tile_bytes':len(sparse),'compressed_bytes':len(compressed),
    'palette_sha256':hashlib.sha256((out/(key+'_palette.lz')).read_bytes()).hexdigest()})
 (out/'ai_specials.h').write_text('\n'.join(header)+'\n')
 result={'source':'retail DIABDAT.MPQ','clips':report,'checked_frames':sum(x['directions']*x['frames_per_direction'] for x in report),'additional_bytes':sum(x['bytes'] for x in report),'existing_assets_changed':False}
 (out/'ai-specials-report.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(result,indent=2))
if __name__=='__main__':main()
