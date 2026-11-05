"""MPQ hit sprites and original music/SFX. PCM goes directly to GBA FIFOs."""
from audio_pcm import guarded_pcm
from pathlib import Path
import sys,struct,json,subprocess
import mpyq
from PIL import Image
from extract_assets import read_mpq,cl2_frames
from pack_gba_assets import tile_bytes
from lz77 import indexed,decompress
r=Path(__file__).resolve().parents[1];out=r/'generated';src=r/'source_assets/combat';src.mkdir(exist_ok=True)
a=mpyq.MPQArchive(sys.argv[1],listfile=False);raw=read_mpq(a,'levels\\l1data\\l1_1.pal');pal=[tuple(raw[i:i+3]) for i in range(0,768,3)]
header=[];report={}
actors=[('warrior','plrgfx/warrior/wls/wlsht',96,'obj',0,None),('rogue','plrgfx/rogue/rls/rlsht',96,'rogue',0,None),('sorcerer','plrgfx/sorceror/slt/sltht',96,'sorcerer',0,None),('zombie','Monsters/Zombie/Zombieh',128,'obj',32,None),('skeleton','Monsters/SkelAxe/SklAxh',128,'skeleton',0,'Monsters/SkelAxe/White.TRN'),('fallen_spear','Monsters/FalSpear/Phallh',128,'fallen_spear',0,'Monsters/FalSpear/FallenT.TRN'),('fallen_sword','Monsters/FalSword/Fallh',128,'fallen_sword',0,'Monsters/FalSword/FallenT.TRN'),('scavenger','Monsters/Scav/Scavh',128,'scavenger',0,None),('captain','Monsters/SkelSd/SklSrh',128,'captain',0,'Monsters/SkelSd/White.TRN')]
for name,path,width,palette,offset,trn in actors:
 data=read_mpq(a,path.replace('/','\\')+'.cl2');(src/(name+'_hit.cl2')).write_bytes(data);groups=cl2_frames(data,width)
 colors=struct.unpack('<16H',decompress((out/(palette+'_palette.lz')).read_bytes())[offset:offset+32]);rgb=[tuple(((c>>shift)&31)*255//31 for shift in (0,5,10)) for c in colors]
 lut=[0]+[min(range(1,16),key=lambda j:sum((rgb[j][k]-pal[i][k])**2 for k in range(3))) for i in range(1,256)]
 translation=list(read_mpq(a,trn.replace('/','\\'))) if trn else list(range(256));frames=[];count=len(groups[0]);assert len(groups)==8 and all(len(d)==count for d in groups)
 for direction in groups:
  for im in direction:
   im=im.point(translation);canvas=Image.new('P',(128,128),0);canvas.paste(im,((128-im.width)//2,128-im.height));frames.append(tile_bytes(canvas.resize((64,64),Image.Resampling.NEAREST).point(lut),4))
 (out/(name+'_hit.lz')).write_bytes(indexed(b''.join(frames),2048));header += [f'extern const uint8_t asset_{name}_hit[];',f'#define FRAMES_{name.upper()}_HIT {count}'];report[name+'_hit']=count
sounds=[('music_town','Music/DTowne.wav',0.60),('music_dungeon','Music/DLvlA.wav',0.60),('music_menu','Music/Dintro.wav',0.60),('swing','Sfx/Misc/Swing.wav',0.85),('hero_warrior','Sfx/Warrior/Warior69.wav',0.85),('hero_rogue','Sfx/Rogue/Rogue69.wav',0.85),('hero_sorcerer','Sfx/Sorceror/Mage69.wav',0.85)]
for name,folder,stem in [('zombie','Zombie','Zombie'),('skeleton','SkelAxe','SklAx'),('fallen_spear','FalSpear','Phall'),('fallen_sword','FalSword','Fall'),('scavenger','Scav','Scav'),('captain','SkelSd','SklSr')]:
 for suffix in ('a','h','d'):sounds.append((name+'_'+suffix,f'Monsters/{folder}/{stem}{suffix}1.wav',0.85))
for name,path,volume in sounds:
 raw=read_mpq(a,path.replace('/','\\'));wav=src/(name+'.wav');wav.write_bytes(raw)
 pcm=subprocess.run(['ffmpeg','-v','error','-i',str(wav),'-ac','1','-ar','16384','-af',f'volume={volume}','-f','s8','-'],stdout=subprocess.PIPE,check=True).stdout
 # VBlank IRQ bounds FIFO B guard; music retains the long loop guard.
 (out/('audio_'+name+'.lz')).write_bytes(guarded_pcm(pcm,name.startswith("music_")));header += [f'extern const uint8_t asset_audio_{name}[];',f'#define AUDIO_{name.upper()}_LENGTH {len(pcm)}'];report[name]={'source':path,'samples':len(pcm),'seconds':len(pcm)/16384}
(out/'combat_assets.h').write_text('\n'.join(header)+'\n');(out/'combat-assets-report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
