"""Retail MPQ spell icons and compact four-frame missile art. No RAM atlas."""
from pathlib import Path
import struct,sys,json
from PIL import Image
from extract_assets import cl2_frames,read_mpq
from pack_gba_assets import tile_bytes
from lz77 import decompress
from pack_spell_icons import build_icons
import mpyq
r=Path(__file__).resolve().parents[1];out=r/'generated';src=r/'source_assets/spell_expansion';src.mkdir(exist_ok=True)
a=mpyq.MPQArchive(sys.argv[1],listfile=False) if len(sys.argv)>1 else None
sources={}
def get(path):
 p=src/path.replace('\\','_');sources[p.name]=path
 if a:p.write_bytes(read_mpq(a,path))
 return p.read_bytes()
pal=(r/'source_assets/spells/l1.pal').read_bytes();rgb=[tuple(pal[i:i+3]) for i in range(0,768,3)]
colors=[(0,0,0),(4,4,5),(10,10,11),(20,19,15),(8,0,0),(18,0,0),(31,3,0),(0,0,4),(0,0,18),(3,8,31),(31,26,0),(31,30,20),(2,2,3),(31,31,31),(20,20,20),(0,0,0)]
colors=[tuple(c*255//31 for c in row) for row in colors];lut=[0]+[min(range(1,16),key=lambda j:sum((colors[j][k]-rgb[i][k])**2 for k in range(3))) for i in range(1,256)]
def array(name,blobs):
 return 'static const uint32_t '+name+'['+str(len(blobs))+']['+str(len(blobs[0])//4)+']={'+','.join('{'+','.join(hex(x) for x in struct.unpack('<'+str(len(b)//4)+'I',b))+'}' for b in blobs)+'};\n'
build_icons()
art=[];mapping={};preview=Image.new('RGB',(14*64,64),'#202020')
files=[('Fireba1',96),('Lghning',96),('Firewal1',128),('Guard2',96),('Inferno',96),('Holy1',96),('Flare',128),('Sklball1',96),('Miniltng',64),('MagBlos',128),('Portal2',96),('Manashld',96)]
for kind,(name,width) in enumerate(files):
 # Art flags0 append a file number even when there is just one direction.
 path='Missiles\\'+name+'.CL2'
 raw=get(path);frames=cl2_frames(struct.pack('<8I',*([32]*8))+raw,width)[0]
 selected=[frames[min(len(frames)-1,n*len(frames)//4)] for n in range(4)]
 box=None
 for im in selected:
  b=im.getbbox()
  if b:box=b if box is None else (min(box[0],b[0]),min(box[1],b[1]),max(box[2],b[2]),max(box[3],b[3]))
 assert box
 for im in selected:
  im=im.crop(box);im.thumbnail((16,16),Image.Resampling.NEAREST);canvas=Image.new('P',(16,16));canvas.paste(im,((16-im.width)//2,16-im.height));canvas=canvas.point(lut);art.append(tile_bytes(canvas,4))
  canvas.putpalette([v for c in colors for v in c]+[0]*720)
 preview.paste(canvas.convert('RGB').resize((64,64),Image.Resampling.NEAREST),(kind*64,0))
raw=get('Monsters\\Golem\\GolemW.CL2');frames=cl2_frames(raw,96)[0]
for n in range(4):
 im=frames[n*len(frames)//4];im=im.crop(im.getbbox());im.thumbnail((16,16),Image.Resampling.NEAREST);canvas=Image.new('P',(16,16));canvas.paste(im,((16-im.width)//2,16-im.height));art.append(tile_bytes(canvas.point(lut),4))
# Classes index into four raw16x16 frames. Stone has no missile graphic.
classes={1:0,3:1,4:9,6:2,7:10,11:11,12:0,13:3,14:1,15:2,18:1,20:4,21:12,24:9,29:0,30:8,31:5,35:6,36:7}
h='#pragma once\n#include "spell_icon_colors.h"\n'+array('spell_effect_tiles',art)
h+='static const uint8_t spell_effect_class[37]={'+','.join(str(classes.get(i,0)) for i in range(37))+'};\n'
raw=decompress((out/'bow_arrows.lz').read_bytes());h+=array('spell_arrow_tiles',[raw[i:i+128] for i in range(0,len(raw),128)])
(out/'spell_expansion_assets.h').write_text(h);(src/'sources.json').write_text(json.dumps(sources,indent=2));preview.save(src/'effects-preview.png');print('37 icons,',len(art),'effect frames; ROM arrays,8 runtime slots')
# Original casting groups not previously present: lightning and utility.
from audio_pcm import guarded_pcm
import subprocess
audio=['#pragma once']
for name in ['Cast4','Cast6']:
 raw=get('Sfx\\Misc\\'+name+'.wav');wav=src/('Sfx_Misc_'+name+'.wav')
 pcm=subprocess.run(['ffmpeg','-v','error','-i',str(wav),'-ac','1','-ar','16384','-af','volume=0.85','-f','s8','-'],stdout=subprocess.PIPE,check=True).stdout
 (out/('audio_spell_'+name.lower()+'.lz')).write_bytes(guarded_pcm(pcm));audio+=[f'extern const uint8_t asset_audio_spell_{name.lower()}[];',f'#define AUDIO_{name.upper()}_LENGTH {len(pcm)}']
(out/'spell_expansion_audio.h').write_text('\n'.join(audio)+'\n');(src/'sources.json').write_text(json.dumps(sources,indent=2))
