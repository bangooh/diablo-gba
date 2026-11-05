"""Original Rogue light-armour bow CL2 set, Arrows.CL2 and BFire.WAV."""
from audio_pcm import guarded_pcm
from pathlib import Path
import sys,struct,subprocess,mpyq
from PIL import Image
from extract_assets import read_mpq,cl2_frames
from pack_gba_assets import tile_bytes
from lz77 import indexed,compress,decompress
r=Path(__file__).resolve().parents[1];o=r/'generated';s=r/'source_assets/bow';s.mkdir(exist_ok=True)
a=mpyq.MPQArchive(sys.argv[1],listfile=False);raw=read_mpq(a,'Levels\\L1Data\\L1_1.PAL');pal=[tuple(raw[i:i+3]) for i in range(0,768,3)];h=['#pragma once','#include <stdint.h>']
def get(path):
 b=read_mpq(a,path);(s/path.split('\\')[-1]).write_bytes(b);return b

def lut_for(data,allowed=range(1,16)):
 colors=struct.unpack('<16H',data);rgb=[tuple(((c>>v)&31)*255//31 for v in (0,5,10)) for c in colors]
 return [0]+[min(allowed,key=lambda j:sum((rgb[j][k]-pal[i][k])**2 for k in range(3))) for i in range(1,256)]
lut=lut_for(decompress((o/'rogue_palette.lz').read_bytes()))
for st,width in [('as',96),('aw',96),('at',128),('ht',96),('st',96),('wl',96)]:
 groups=cl2_frames(get('PlrGFX\\Rogue\\RLB\\RLB'+st+'.CL2'),width);frames=[];count=len(groups[0])
 for d in groups:
  for im in d:
   canvas=Image.new('P',(128,128));canvas.paste(im,((128-im.width)//2,128-im.height));frames.append(tile_bytes(canvas.resize((64,64),Image.Resampling.NEAREST).point(lut),4))
 (o/('rogue_bow_'+st+'.lz')).write_bytes(indexed(b''.join(frames),2048));h.extend([f'extern const uint8_t asset_rogue_bow_{st}[];',f'#define FRAMES_ROGUE_BOW_{st.upper()} {count}'])
b=get('Missiles\\Arrows.CL2');n=struct.unpack_from('<I',b)[0];assert n==16;offs=struct.unpack_from('<17I',b,4);frames=[]
arrow_palette=bytearray(decompress((o/'obj_palette.lz').read_bytes())[96:128])
for i,c in enumerate([12|(9<<5)|(6<<10),20|(20<<5)|(20<<10),30|(30<<5)|(30<<10),6|(4<<5)|(2<<10)],7):struct.pack_into('<H',arrow_palette,i*2,c)
(o/'bow_palette.lz').write_bytes(compress(bytes(arrow_palette)));h.append('extern const uint8_t asset_bow_palette[];')
lut=lut_for(arrow_palette,range(1,11))
for lo,hi in zip(offs,offs[1:]):
 f=b[lo:hi];p=struct.unpack_from('<H',f)[0];pix=bytearray()
 while p<len(f):
  c=f[p];p+=1
  if c<128:pix.extend(bytes(c))
  else:
   c=256-c
   if c<=65:pix.extend(f[p:p+c]);p+=c
   else:pix.extend(bytes([f[p]])*(c-65));p+=1
 assert len(pix)%96==0
 im=Image.frombytes('P',(96,len(pix)//96),bytes(pix)).transpose(Image.Transpose.FLIP_TOP_BOTTOM);im=im.crop(im.getbbox());scale=min(.5,16/im.width,16/im.height);im=im.resize((max(1,round(im.width*scale)),max(1,round(im.height*scale))),Image.Resampling.NEAREST)
 canvas=Image.new('P',(16,16));canvas.paste(im,((16-im.width)//2,(16-im.height)//2));frames.append(tile_bytes(canvas.point(lut),4))
(o/'bow_arrows.lz').write_bytes(compress(b''.join(frames)));h.append('extern const uint8_t asset_bow_arrows[];')
wav=s/'BFire.wav';get('Sfx\\Misc\\BFire.wav');pcm=subprocess.run(['ffmpeg','-v','error','-i',str(wav),'-ac','1','-ar','16384','-f','s8','-'],stdout=subprocess.PIPE,check=True).stdout
(o/'audio_bow.lz').write_bytes(guarded_pcm(pcm));h.extend(['extern const uint8_t asset_audio_bow[];',f'#define AUDIO_BOW_LENGTH {len(pcm)}'])
(o/'bow_assets.h').write_text('\n'.join(h)+'\n');print('\n'.join(h))
