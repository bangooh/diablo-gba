"""Restore native entry/cow IMA4 voices and three native cow animations from MPQ."""
from pathlib import Path
import sys,struct,re,json,hashlib,subprocess
import mpyq
from PIL import Image
from extract_mpq134 import read_mpq
from pack_gba_assets import tile_bytes
root=Path(__file__).resolve().parents[1];out=root/'generated';m=mpyq.MPQArchive(sys.argv[1],listfile=False)
steps=list(map(int,re.search(r'steps\[89\]=\{([^}]+)',(root/'src/audio.c').read_text())[1].split(',')));adjust=[-1,-1,-1,-1,2,4,6,8]
def ima(samples):
 pred=idx=0;codes=[]
 for v in samples:
  delta=v-pred;code=8 if delta<0 else 0;delta=abs(delta);step=steps[idx];change=step>>3
  for bit,amount in [(4,step),(2,step>>1),(1,step>>2)]:
   if delta>=amount:code|=bit;delta-=amount;change+=amount
  pred=max(-32768,min(32767,pred+(-change if code&8 else change)));idx=max(0,min(88,idx+adjust[code&7]));codes.append(code)
 if len(codes)&1:codes.append(0)
 return bytes(codes[i]|codes[i+1]<<4 for i in range(0,len(codes),2))
paths=[]
for folder,name in [('Warrior','Warior'),('Rogue','Rogue'),('Sorceror','Mage')]:
 for suffix in ['97','96','98','99']:paths.append(f'Sfx\\{folder}\\{name}{suffix}.wav')
paths+=['Sfx\\Misc\\Lvl16int.wav','Sfx\\Towners\\Cow1.wav','Sfx\\Towners\\Cow2.wav']
for folder,name in [('Warrior','Warior'),('Rogue','Rogue'),('Sorceror','Mage')]:
 for suffix in ['52','49','50']:paths.append(f'Sfx\\{folder}\\{name}{suffix}.wav')
h=['#pragma once','#define ENTRY_COW_AUDIO_COUNT 24'];records=[];report=[]
for i,p in enumerate(paths):
 wav=read_mpq(m,p);raw=subprocess.run(['ffmpeg','-v','error','-i','pipe:0','-ac','1','-ar','6144','-af','volume=0.85','-f','s16le','pipe:1'],input=wav,stdout=subprocess.PIPE,check=True).stdout
 data=ima(struct.unpack('<'+'h'*(len(raw)//2),raw));name=f'audio_entry_cow_{i}'
 (out/(name+'.lz')).write_bytes(data);h.append(f'extern const uint8_t asset_{name}[];');records.append(f'{{asset_{name},{len(raw)//2},{256+i}}}')
 report.append({'voice':256+i,'path':p,'samples':len(raw)//2,'wav_sha256':hashlib.sha256(wav).hexdigest(),'adpcm_sha256':hashlib.sha256(data).hexdigest()})
h+=['static const GreetingAudioClip entry_cow_audio[ENTRY_COW_AUDIO_COUNT]={',','.join(records),'};'];(out/'entry_cow_audio.h').write_text('\n'.join(h)+'\n')
# Reuse Griswold's existing town palette bank: cows are neutral brown/grey.
def lz(b):
 n=int.from_bytes(b[:4],'little')>>8;o=bytearray();i=4
 while len(o)<n:
  f=b[i];i+=1
  for bit in [128,64,32,16,8,4,2,1]:
   if len(o)>=n:break
   if f&bit:
    a,c=b[i:i+2];i+=2;d=((a&15)<<8)+c+1
    for _ in range((a>>4)+3):o.append(o[-d])
   else:o.append(b[i]);i+=1
 return bytes(o)
pal=struct.unpack('<16H',lz((out/'npc_palette.lz').read_bytes())[:32]);rgb=[((v&31)*8,((v>>5)&31)*8,(v>>10)*8) for v in pal]
tp=read_mpq(m,'Levels\\TownData\\Town.pal');source=[tuple(tp[i:i+3]) for i in range(0,768,3)]
lut=[0]+[min(range(1,16),key=lambda j:sum((source[i][k]-rgb[j][k])**2 for k in range(3))) for i in range(1,256)]
b=read_mpq(m,'Towners\\Animals\\Cow.CEL');frames=[]
for direction in [1,3,4]:
 off=struct.unpack_from('<I',b,direction*4)[0];count=struct.unpack_from('<I',b,off)[0];assert count==12
 offsets=struct.unpack_from('<13I',b,off+4)
 for a,z in zip(offsets,offsets[1:]):
  f=b[off+a:off+z];pos=int.from_bytes(f[:2],'little');px=[]
  while pos<len(f):
   run=f[pos];pos+=1
   if run>=128:px.extend([0]*(256-run))
   else:px.extend(f[pos:pos+run]);pos+=run
  assert len(px)%128==0
  im=Image.frombytes('P',(128,len(px)//128),bytes(px)).transpose(Image.Transpose.FLIP_TOP_BOTTOM).resize((64,len(px)//256),Image.Resampling.NEAREST)
  canvas=Image.new('P',(64,64));canvas.paste(im,(0,64-im.height));canvas=canvas.point(lut);frames.append(tile_bytes(canvas,4))
records=[]
for raw in frames:
 tiles=[raw[j:j+32] for j in range(0,2048,32)];mask=sum(1<<i for i,t in enumerate(tiles) if any(t));payload=b''.join(t for t in tiles if any(t));records.append(struct.pack('<III',len(payload),mask&0xffffffff,mask>>32)+payload)
off=12+4*len(records);offsets=[]
for r in records:offsets.append(off);off+=len(r)
(out/'npc_cows.lz').write_bytes(struct.pack('<III',0x32525053,len(records),off)+struct.pack('<36I',*offsets)+b''.join(records))
(out/'entry_cow_assets.json').write_text(json.dumps({'audio':report,'cow_source_sha256':hashlib.sha256(b).hexdigest(),'cow_directions':[1,3,4],'cow_frames':36,'cow_palette':'Existing Griswold bank 6'},indent=2)+'\n')
print('packed',len(paths),'voices,',len(records),'cow frames')
# Restore the native cow roster as well as its artwork. The historical tool
# produced sprites but depended on an independently edited towners.h.
roster = out / 'towners.h'
text = roster.read_text()
if '#define NPC_COUNT 9\n' in text:
 source = (root / 'source_assets/town_dialogue_stage53/towners.cpp').read_text()
 def positions(name):
  return list(map(int, re.search(r'int '+name+r'\[\] = \{([^}]+)\}', source)[1].replace(' ', '').split(',')))
 xs, ys = positions('TownCowX'), positions('TownCowY')
 assert len(xs) == len(ys) == 3
 text = text.replace('#define NPC_COUNT 9\n', '#define NPC_COUNT 12\nextern const uint8_t asset_npc_cows[];\n')
 additions = {'npc_xy': ','.join('{%d,%d}' % xy for xy in zip(xs, ys)),
              'npc_names': '"COW","COW","COW"', 'npc_frames': '12,12,12',
              'npc_assets': 'asset_npc_cows,asset_npc_cows,asset_npc_cows'}
 for name, tail in additions.items():
  text, count = re.subn(r'('+name+r'\[NPC_COUNT\](?:\[2\])?=\{[^\n]+)(\};)', lambda m: m[1]+','+tail+m[2], text)
  assert count == 1, name
 roster.write_text(text)
else:
 assert '#define NPC_COUNT 12\n' in text, 'Unexpected NPC roster; review required'
