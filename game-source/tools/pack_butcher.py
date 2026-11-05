"""Retail MPQ Butcher animations, RND6 setpiece and native room objects."""
from audio_pcm import guarded_pcm
from pathlib import Path
import sys,struct,subprocess,re,mpyq
from PIL import Image
from extract_assets import read_mpq,cl2_frames,pieces
from pack_gba_assets import obj_palette,tile_bytes,rgb15
from lz77 import compress,indexed
r=Path(__file__).resolve().parents[1];out=r/'generated';src=r/'source_assets/butcher';src.mkdir(exist_ok=True)
a=mpyq.MPQArchive(sys.argv[1],listfile=False)
def get(path):
 b=read_mpq(a,path);(src/path.split('\\')[-1]).write_bytes(b);return b
raw=get('Levels\\L1Data\\L1_1.PAL');pal=[tuple(raw[i:i+3]) for i in range(0,768,3)]
trn=get('Monsters\\Monsters\\GENRL.TRN');groups={}
for state in 'nwahd':groups[state]=[[im.point(list(trn)) for im in d] for d in cl2_frames(get('Monsters\\FatC\\FatC'+state+'.CL2'),128)]
palette,lut=obj_palette([im for group in groups.values() for d in group for im in d],pal)
header=['#pragma once','#include <stdint.h>']
def blob(name,data):
 (out/(name+'.lz')).write_bytes(data);header.append(f'extern const uint8_t asset_{name}[];')
blob('butcher_palette',compress(rgb15(palette+[(0,0,0)]+[tuple(min(255,round(c+(255-c)*.18)) for c in rgb) for rgb in palette[1:]])))
for state,group in groups.items():
 frames=[];count=len(group[0]);assert len(group)==8 and all(len(d)==count for d in group)
 for d in group:
  for im in d:
   canvas=Image.new('P',(128,128));canvas.paste(im,((128-im.width)//2,128-im.height));frames.append(tile_bytes(canvas.resize((64,64),Image.Resampling.NEAREST).point(lut),4))
 blob('butcher_'+state,indexed(b''.join(frames),2048));header.append(f'#define FRAMES_BUTCHER_{state.upper()} {count}')
for state in 'ahd':
 wav=src/('FatC'+state+'1.WAV');get('Monsters\\FatC\\'+wav.name)
 pcm=subprocess.run(['ffmpeg','-v','error','-i',str(wav),'-ac','1','-ar','16384','-af','volume=0.85','-f','s8','-'],stdout=subprocess.PIPE,check=True).stdout
 blob('audio_butcher_'+state,guarded_pcm(pcm));header.append(f'#define AUDIO_BUTCHER_{state.upper()}_LENGTH {len(pcm)}')
b=get('Levels\\L1Data\\rnd6.DUN');w,h=struct.unpack_from('<HH',b);assert (w,h)==(6,6)
ids=list(struct.unpack_from('<36H',b,4));header.append('static const uint8_t butcher_room[6][6]={'+','.join('{'+','.join(map(str,ids[i:i+6]))+'}' for i in range(0,36,6))+'};')
til=get('Levels\\L1Data\\L1.TIL');sol=get('Levels\\L1Data\\L1.SOL')
placements=[]
for y in range(6):
 for x in range(6):
  pp=struct.unpack_from('<4H',til,(ids[y*6+x]-1)*8)
  for j,p in enumerate(pp):
   p+=1;xx=x*2+j%2;yy=y*2+j//2
   if p==367:header += [f'#define BUTCHER_ANCHOR_X {xx}',f'#define BUTCHER_ANCHOR_Y {yy}']
   if p in (44,51,214,46,56):placements.append((xx,yy,6 if p in (44,51,214) else 7))
header.append('static const uint8_t butcher_doors[][3]={'+','.join('{'+','.join(map(str,p))+'}' for p in placements)+'};')
# Native prop frames append after the unchanged 20 container frames.
prop=bytearray((out/'prop_frames.bin').read_bytes()[:20*4096])
for name,width in [('L1Doors',64),('TSoul',128),('TNudeM',128),('TNudeW',128)]:
 b=get('Objects\\'+name+'.CEL');n=struct.unpack_from('<I',b)[0];offs=struct.unpack_from('<'+'I'*(n+1),b,4)
 header.append(f'#define PROP_{name.upper()}_BASE {len(prop)//4096}')
 for lo,hi in zip(offs,offs[1:]):
  f=b[lo:hi];p=struct.unpack_from('<H',f)[0];pix=bytearray()
  while p<len(f):
   c=f[p];p+=1
   if c>=128:pix.extend(bytes(256-c))
   else:pix.extend(f[p:p+c]);p+=c
  assert len(pix)%width==0
  im=Image.frombytes('P',(width,len(pix)//width),bytes(pix)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
  scale=min(.5,64/im.height);im=im.resize((round(width*scale),round(im.height*scale)),Image.Resampling.NEAREST)
  canvas=Image.new('P',(64,64));canvas.paste(im,((64-im.width)//2,64-im.height));prop.extend(canvas.tobytes())
(out/'prop_frames.bin').write_bytes(prop);(out/'prop_frames.lz').write_bytes(indexed(bytes(prop),4096))
p=out/'packed_assets.h';p.write_text(re.sub(r'#define RAW_PROP_FRAMES \d+',f'#define RAW_PROP_FRAMES {len(prop)}',p.read_text()))
(out/'butcher_assets.h').write_text('\n'.join(header)+'\n')
# All original MIN pieces needed by the enlarged Cathedral atlas.
cat=r/'source_assets/cathedral'
ims=pieces(get('Levels\\L1Data\\L1.CEL'),get('Levels\\L1Data\\L1.MIN'))
for p in set([393,395,408]+[v+1 for i in list(range(1,25))+list(range(57,69))+ids for v in struct.unpack_from('<4H',til,(i-1)*8)]):
 im=ims[p-1];im.putpalette(raw);im.save(cat/'pieces'/f'{p:03}.png')
print('Butcher frames', {k:len(v[0]) for k,v in groups.items()},'doors',placements,'props',len(prop)//4096)
