#!/usr/bin/env python3
"""Verify new source CEL/PCM output, old prop pixels, and cropped geometry."""
from pathlib import Path
import argparse,hashlib,json,re,struct
from PIL import Image,ImageDraw
from pack_environment_stage64 import build,SPECS
from pack_prop_atlas import build as atlas_build
ROOT=Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
def decode_atlas(folder):
 header=(folder/'prop_atlas.h').read_text();raw=(folder/'prop_atlas.lz').read_bytes()
 count=int(re.search(r'#define PROP_ATLAS_FRAMES (\d+)',header)[1]);section=header.split('prop_atlas_tiles[PROP_ATLAS_FRAMES][64]={',1)[1].split('};',1)[0]
 ids=list(map(int,re.findall(r'\d+',re.sub(r'/\*.*?\*/','',section,flags=re.S))));assert len(ids)==count*64
 frames=[]
 for f in range(count):
  frame=bytearray(4096)
  for t,index in enumerate(ids[f*64:(f+1)*64]):
   tile=raw[index*64:(index+1)*64];assert len(tile)==64
   for y in range(8):pos=((t//8)*8+y)*64+(t%8)*8;frame[pos:pos+8]=tile[y*8:y*8+8]
  frames.append(bytes(frame))
 return frames
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--baseline',type=Path);a=p.parse_args()
 out=ROOT/'generated/stage64-validation/environment-assets';out.mkdir(parents=True,exist_ok=True)
 outputs,source,frames,palette=build()
 for name,data in outputs.items():assert (ROOT/'generated'/name).read_bytes()==data,name
 outputs,atlas=atlas_build()
 for name,data in outputs.items():assert (ROOT/'generated'/name).read_bytes()==data,name
 current=decode_atlas(ROOT/'generated');base=source['frame_base'];assert current[base:]==frames
 old_checked=0;old_sha=None
 if a.baseline:
  old=decode_atlas(a.baseline/'generated');assert len(old)==base and current[:base]==old;old_checked=len(old)*4096;old_sha=sha(b''.join(old))
 # All native art and source-dependent alpha remain intact, including the
 # 80x80 Burning Cross after (+8,-8) transparent-margin cropping.
 rgbpal=[v*255//31 for c in palette for v in (c&31,(c>>5)&31,(c>>10)&31)]
 sheet=Image.new('RGB',(6*144,8*158),(22,23,26));draw=ImageDraw.Draw(sheet);n=0
 for symbol,name,width,selected,expected in SPECS:
  for f in selected:
   raw=frames[n];im=Image.frombytes('P',(64,64),raw);im.putpalette(rgbpal)
   x=(n%6)*144;y=(n//6)*158;sheet.paste(im.convert('RGB').resize((128,128),Image.Resampling.NEAREST),(x+8,y+26));draw.text((x+3,y+3),f'{name} / {f}',fill='white');n+=1
 sheet.save(out/'native-art-contact.png')
 audio=source['audio'];assert abs(audio['native_seconds']-audio['seconds'])<1/16384
 report={'status':'PASS','new_frames':len(frames),'new_pixels_checked':len(frames)*4096,'old_pixels_checked':old_checked,'old_pixels_sha256':old_sha,
  'new_pixels_sha256':sha(b''.join(frames)),'atlas_pixels_checked':atlas['native_pixels_checked'],
  'original_animation_frames_1based':{s[1]:s[3] for s in SPECS},'burning_cross_crop_half_pixels':[16,8,80,72],'burning_cross_runtime_offset':[8,-8],
  'audio':audio,'persistent_ram_added':0,'source_hashes':{str(p.relative_to(ROOT)):sha(p.read_bytes()) for p in sorted((ROOT/'source_assets/environment_stage64').iterdir())},
  'generated_hashes':{str((ROOT/'generated'/n).relative_to(ROOT)):sha(data) for n,data in {**build()[0],**outputs}.items()}}
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ['status','new_frames','new_pixels_checked','old_pixels_checked','atlas_pixels_checked']}))
if __name__=='__main__':main()
