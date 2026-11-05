"""MPQ spell icons/effects/audio. Player casts: pack_player_armor.py."""
from audio_pcm import guarded_pcm
from pathlib import Path
import sys,struct,subprocess,json
from PIL import Image,ImageDraw
from extract_assets import read_mpq,cl2_frames
from pack_gba_assets import tile_bytes
import mpyq
r=Path(__file__).resolve().parents[1];src=r/'source_assets/spells';src.mkdir(exist_ok=True);out=r/'generated'
a=mpyq.MPQArchive(sys.argv[1],listfile=False) if len(sys.argv)>1 else None
paths={}
def get(name,path):
 paths[name]=path;p=src/name
 if a:p.write_bytes(read_mpq(a,path))
 return p.read_bytes()
pal=get('l1.pal','Levels\\L1Data\\L1_1.PAL');rgb=[tuple(pal[i:i+3]) for i in range(0,768,3)];header=['#pragma once']
# Player casts share the L/M/H pool with weapon poses. Rebuild them with
# tools/pack_player_armor.py; this tool only updates spell UI/audio resources.
header.append('#include "player_spell_art.h"')
# Palette5 is the shared HUD palette; icons occupy only one 32x32 cache.
colors=[(0,0,0),(4,4,5),(10,10,11),(20,19,15),(8,0,0),(18,0,0),(31,3,0),(0,0,4),(0,0,18),(3,8,31),(31,26,0),(31,30,20),(2,2,3),(31,31,31),(20,20,20),(0,0,0)]
colors=[tuple(c*255//31 for c in x) for x in colors]
lut=[0]+[min(range(1,16),key=lambda j:sum((colors[j][k]-rgb[i][k])**2 for k in range(3))) for i in range(1,256)]
def cel(raw,frame,width):
 lo,hi=struct.unpack_from('<II',raw,frame*4);d=raw[lo:hi];p=0;b=bytearray()
 while p<len(d):
  n=d[p];p+=1
  if n>=128:b.extend(bytes(256-n))
  else:b.extend(d[p:p+n]);p+=n
 assert len(b)%width==0
 return Image.frombytes('P',(width,len(b)//width),bytes(b)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
raw=get('SpelIcon.CEL','CtrlPan\\SpelIcon.CEL');icons=[]
for frame in [0,1,2]:
 canvas=Image.new('P',(32,32));draw=ImageDraw.Draw(canvas);draw.rectangle((0,0,23,23),fill=12,outline=3)
 if frame:
  im=cel(raw,frame,56);canvas.paste(im.resize((22,22),Image.Resampling.NEAREST).point(lut),(1,1))
 canvas.putpalette([v for c in colors for v in c]+[0]*720);canvas.save(src/f'icon{frame}.png');icons.append(tile_bytes(canvas,4))
header.append('static const uint32_t spell_icons[3][128]={'+','.join('{'+','.join(hex(x) for x in struct.unpack('<128I',b))+'}' for b in icons)+'};')
raw=get('Fireba1.CL2','Missiles\\Fireba1.CL2');group=cl2_frames(struct.pack('<8I',*([32]*8))+raw,96)[0];fire=[]
for n in [0,4,8]:
 im=group[n];im=im.crop(im.getbbox());im.thumbnail((16,16),Image.Resampling.NEAREST);canvas=Image.new('P',(16,16));canvas.paste(im,((16-im.width)//2,(16-im.height)//2));fire.append(tile_bytes(canvas.point(lut),4))
header.append('static const uint32_t firebolt_tiles[96]={'+','.join(hex(x) for x in struct.unpack('<96I',b''.join(fire)))+'};')
for name in ['Cast2','Cast8']:
 get(name+'.wav','Sfx\\Misc\\'+name+'.wav');pcm=subprocess.run(['ffmpeg','-v','error','-i',str(src/(name+'.wav')),'-ac','1','-ar','16384','-af','volume=0.85','-f','s8','-'],stdout=subprocess.PIPE,check=True).stdout
 symbol='audio_spell_'+name.lower();(out/(symbol+'.lz')).write_bytes(guarded_pcm(pcm));header.extend(['extern const uint8_t asset_'+symbol+'[];',f'#define AUDIO_{name.upper()}_LENGTH {len(pcm)}'])
(out/'spell_assets.h').write_text('\n'.join(header)+'\n');(src/'sources.json').write_text(json.dumps(paths,indent=2));print('spell UI/effects/audio updated; player casts use shared L/M/H assets')
