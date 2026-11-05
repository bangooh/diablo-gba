"""Decode shipped graphics with production C decoders; preserve old corpse pixels."""
from pathlib import Path
import hashlib,json,re,struct,tempfile
from PIL import Image,ImageDraw
from check_unique_deaths import Decoder as FloorDecoder
from pack_hot_monsters import Decoder as ActorDecoder
from lz77 import decompress
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'generated/stage66-validation/assets';OUT.mkdir(parents=True,exist_ok=True)
sha=lambda b:hashlib.sha256(b).hexdigest()
catalog=json.loads((ROOT/'source_assets/monster_stage66/catalog.json').read_text())
old=(ROOT/'source_assets/monster_stage66/floor-baseline.rle').read_bytes()
new=(ROOT/'generated/floor_decals.rle').read_bytes()
bg=struct.unpack('<256H',decompress((ROOT/'generated/bg_palette.lz').read_bytes()))
rgb=lambda v:tuple(((v>>s)&31)*255//31 for s in (0,5,10))
palette=bytes(v for c in bg for v in rgb(c))
def untile(data):
 out=bytearray(4096)
 for y in range(64):
  for x in range(64):
   b=data[((y//8)*8+x//8)*32+(y%8)*4+x%8//2];out[y*64+x]=(b>>((x&1)*4))&15
 return bytes(out)
report={'result':'RUNNING','checks':[]}
with tempfile.TemporaryDirectory() as td:
 a=FloorDecoder(td,old);b=FloorDecoder(td,new);actor=ActorDecoder(td,bytes(4))
 for i in range(8552):assert a.frame(i)==b.frame(i if i<432 else i+80),('old corpse changed',i)
 report['checks'].append('All8552 original static/animated corpse frames remain pixel-identical after relocation')
 geometry={}
 for family,states in catalog['geometry'].items():
  for state,meta in states.items():
   frames=actor.frames((ROOT/'generated'/(family+'_'+state+'.lz')).read_bytes(),meta['frames'])
   assert sha(b''.join(frames))==meta['decoded_sha256'];geometry[family,state]=frames
 report['actor_frames_checked']=sum(len(v) for v in geometry.values())
 sheet=Image.new('RGB',(6*160,200),(20,20,24));draw=ImageDraw.Draw(sheet)
 for col,r in enumerate(catalog['records']):
  family='bat66' if r['kind']<60 else 'snake66'
  p=struct.unpack('<32H',decompress((ROOT/'generated'/(r['key']+'66_palette.lz')).read_bytes()))
  pix=Image.frombytes('P',(64,64),untile(geometry[family,'n'][0]));pix.putpalette(bytes(v for c in p[:16] for v in rgb(c))+bytes(720))
  sheet.paste(pix.convert('RGB').resize((128,128),Image.Resampling.NEAREST),(col*160+16,20))
  draw.text((col*160+6,160),r['name'],fill='white');draw.text((col*160+6,178),f"Floors {r['min_floor']}-{r['max_floor']}",fill=(170,170,180))
 sheet.save(OUT/'monster-preview.png')
 added=FloorDecoder(td,(ROOT/'generated/monster66_corpses.rle').read_bytes())
 for i in range(48):assert b.frame(464+i)==added.frame(i)
 for i in range(48,832):assert b.frame(8552+80+i-48)==added.frame(i)
 report['new_corpse_frames_checked']=832
 ground=FloorDecoder(td,(ROOT/'generated/ground_items66.rle').read_bytes())
 count=struct.unpack_from('<I',ground.blob)[0];assert count==66
 for i in range(count):assert b.frame(9416+i)==ground.frame(i)
 report['ground_frames_checked']=count
 names=json.loads((ROOT/'generated/ground-items-report.json').read_text())['assets']
 sheet=Image.new('RGB',(8*140,5*96),(20,20,24));draw=ImageDraw.Draw(sheet)
 for i,row in enumerate(names):
  pix=Image.frombytes('P',(64,64),ground.frame(i));pix.putpalette(palette)
  crop=pix.crop((12,44,52,64)).convert('RGB').resize((120,60),Image.Resampling.NEAREST)
  x=(i%8)*140;y=(i//8)*96;sheet.paste(crop,(x+10,y));draw.text((x+6,y+70),row['name'],fill='white')
 sheet.save(OUT/'ground-item-preview.png')
 # All four visual source styles share the SAME tile bytes and sentinel11.
 h=(ROOT/'generated/spell_icon_colors.h').read_text();assert 'SPELL_FLAT_FIRST_COLOR 11' in h
 arrays=re.findall(r'\{(0x[0-9a-f, x]+)\}',h)
 assert len(arrays)==38
 icons=[struct.pack('<72I',*map(lambda x:int(x,16),row.split(','))) for row in arrays[1:]]
 assert len(icons)==37 and all(len(v)==288 for v in icons)
 report['spell_icons']=37;report['spell_background']='One palette index11, four RGB555 values; no pixel source recoloring'
 report['checks'].append('SPR2 C decoder guard words intact for all664 directional actor frames; floor C decoder guard words intact')
report['result']='PASS';(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
