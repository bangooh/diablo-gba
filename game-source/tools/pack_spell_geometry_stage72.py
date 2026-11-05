"""Native one-pass effect resampling. No enlarged resident sprites."""
from pathlib import Path
import struct,json,hashlib,sys
r=Path(__file__).resolve().parents[1];out=r/'generated';src=r/'source_assets/spell_expansion'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if '--check' in sys.argv:
 for name,h in json.loads((out/'spell-geometry-stage72-check.json').read_text()).items():assert sha(r/name)==h,name
 print('PASS native Stage70 effect atlas hashes');sys.exit(0)
from PIL import Image
from extract_assets import cl2_frames
from pack_gba_assets import tile_bytes
from lz77 import decompress
rawpal=(r/'source_assets/spells/l1.pal').read_bytes();rgb=[tuple(rawpal[i:i+3]) for i in range(0,768,3)]
old=[(0,0,0),(4,4,5),(10,10,11),(20,19,15),(8,0,0),(18,0,0),(31,3,0),(0,0,4),(0,0,18),(3,8,31),(31,26,0),(31,30,20),(2,2,3),(31,31,31),(20,20,20),(0,0,0)]
# Dungeon bank15 entries1..5 remain reserved for map/player/NPC markers.
new=[(0,0,0)]*6+[(31,8,18),(13,14,17),(23,24,27),(31,31,31),(8,1,0),(16,3,0),(25,7,0),(31,14,1),(31,23,6),(31,30,19)]
oldrgb=[tuple(v*255//31 for v in c) for c in old];newrgb=[tuple(v*255//31 for v in c) for c in new];newrgb[5]=(5*255//31,255,7*255//31)
files=[('Fireba1',96),('Lghning',96),('Firewal2',128),('Guard2',96),('Inferno',96),('Holy1',96),('Flare',128),('Sklball1',96),('Lghning',96),('Newexp',96),('Portal2',96),('Manashld',96),('GolemW',96),('Bluexfr',160)]+[('Firerun'+str(i),96) for i in range(1,9)]+[('Firewal1',128)]+[('Acidbf'+str(i),96) for i in range(1,17)]+[('Magball'+str(i),128) for i in range(1,9)]+[('Ressur1',96),('Acidpud1',96),('Acidpud2',96)]
files += [('Bigexp',160),('Guard1',96),('Guard3',96)]+[('GolemW',96)]*8+[('GolemA',96)]*8
small=[];large=[];spec=[];audit=[];paths=[];frames_for_preview=[];composites={};native_frames={}

for k,(name,width) in enumerate(files):
 p=src/(('Monsters_Golem_' if name.startswith('Golem') else 'Missiles_')+name+'.CL2');paths.append(p);raw=p.read_bytes();groups=cl2_frames(raw if name.startswith('Golem') else struct.pack('<8I',*([32]*8))+raw,width);frames=groups[(k-53)%8 if k>=53 else 0]
 if name=='Bluexfr':
  p2=src/'Missiles_Bluexbk.CL2';paths.append(p2);back=cl2_frames(struct.pack('<8I',*([32]*8))+p2.read_bytes(),width)[0]
  for im,b in zip(frames,back):im.paste(b,(0,0),b.point([0]+[255]*255).convert('L'))
 file_frames=len(frames)
 if name=='Bigexp':frames=frames[:15]
 if name=='Firewal2':frames=frames[:11] # misfiledata.mAnimLen[1], not the file's13 stored frames
 boxes=[im.getbbox() for im in frames if im.getbbox()];box=(min(b[0] for b in boxes),min(b[1] for b in boxes),max(b[2] for b in boxes),max(b[3] for b in boxes))
 w,h=box[2]-box[0],box[3]-box[1];limit=16 if k==8 else 32;ratio=min(.5,limit/max(w,h));tw=max(1,round(w*ratio));th=max(1,round(h*ratio));size=16 if max(tw,th)<=16 else 32
 bank=5 if k in (6,7,10,11,12,13,47) or k>=53 else 15;palette=oldrgb if bank==5 else newrgb;allowed=range(1,16) if bank==5 else range(6,16)
 if name.startswith('Acid'):allowed=[5,7,8,9]
 if k in (1,8):allowed=range(7,10)
 lut=[0]+[min(allowed,key=lambda j:sum((palette[j][a]-rgb[i][a])**2 for a in range(3))) for i in range(1,256)]
 cx=round(((box[0]+box[2])/2-width/2)*.5);bottom=round((box[3]-frames[0].height)*.5)
 small_offset=len(small);big_offset=len(large);preview=[]
 for im in frames:
  # The full-resolution source feeds BOTH sizes independently, never16->32.
  for dim,pool in [(16,small)]+([(32,large)] if size==32 else []):
   rr=min(ratio,dim/max(w,h));ww=max(1,round(w*rr));hh=max(1,round(h*rr));canvas=Image.new('P',(dim,dim));piece=im.crop(box).resize((ww,hh),Image.Resampling.NEAREST).point(lut);canvas.paste(piece,((dim-ww)//2,dim-hh));pool.append(tile_bytes(canvas,4))
   if dim==size:canvas.putpalette(sum((list(c) for c in palette),[])+[0]*720);preview.append(canvas.convert('RGB'))
 spec.append((small_offset,big_offset,len(frames),size,cx,bottom,bank))
 audit.append({'class':k,'source':str(p.relative_to(r)),'name':name,'source_frames':file_frames,'resident_frames':len(frames),'source_box':box,'native_half_size':[(w+1)//2,(h+1)//2],'display_size':[tw,th],'resident_size':size,'center_x':cx,'bottom_y':bottom,'palette_bank':bank,'resampling':'one direct nearest-neighbour reduction from original; never enlarged','charged_adaptation':k==8})
 frames_for_preview.append(preview)
 # Composite pieces retain the native half-size; no 32px squeeze.
 if k in (3,9,10,13,47,50,51,52) or k>=53:
  ww=(w+1)//2;hh=(h+1)//2
  if k==50 and hh>64:ww=round(ww*64/hh);hh=64
  cols=(ww+15)//16;rows=(hh+15)//16
  ims=[]
  for im in frames:
   canvas=Image.new('P',(cols*16,rows*16));piece=im.crop(box).resize((ww,hh),Image.Resampling.NEAREST).point(lut)
   canvas.paste(piece,((cols*16-ww)//2,rows*16-hh));ims.append(canvas)
  native_frames[k]=(ims,cols,rows,cx,bottom,bank)
  audit[-1]['display_size']=[ww,hh];audit[-1]['composite']=True

for k,(ims,cols,rows,cx,bottom,bank) in native_frames.items():
 first=len(spec);pieces=0
 for y in range(rows):
  for x in range(cols):
   frames=[im.crop((x*16,y*16,x*16+16,y*16+16)) for im in ims]
   if not any(im.getbbox() for im in frames):continue
   offset=len(small);small.extend(tile_bytes(im,4) for im in frames)
   spec.append((offset,0,len(frames),16,cx+x*16+8-cols*8,bottom+(y+1)*16-rows*16,bank));pieces+=1
 composites[k]=(first,pieces)
assert len(spec)<240
classes={1:0,3:1,4:13,6:2,7:10,11:11,12:0,13:3,14:1,15:2,18:1,20:4,21:12,24:9,29:14,30:8,31:5,35:6,36:7,32:47}
h='#pragma once\n#include "spell_icon_colors.h"\n'
for label,data,words in [('small',small,32),('large',large,128)]:h+='static const uint32_t spell_'+label+'_tiles['+str(len(data))+']['+str(words)+']={'+','.join('{'+','.join(hex(x) for x in struct.unpack('<'+str(words)+'I',b))+'}' for b in data)+'};\n'
h+='static const struct {uint16_t small,large;uint8_t frames,size;int8_t center,bottom;uint8_t bank;} spell_effect_geometry['+str(len(spec))+']={'+','.join('{'+','.join(map(str,s))+'}' for s in spec)+'};\n'
h+='static const uint8_t spell_composite['+str(len(spec))+'][2]={'+','.join('{%d,%d}'%composites.get(i,(0,0)) for i in range(len(spec)))+'};\n'
h+='static const uint8_t spell_effect_class[37]={'+','.join(str(classes.get(i,0)) for i in range(37))+'};\n'
h+='static const uint16_t spell_native_colors[10]={'+','.join(str(c[0]|c[1]<<5|c[2]<<10) for c in new[6:])+'};\n'
raw=decompress((out/'bow_arrows.lz').read_bytes());h+='static const uint32_t spell_arrow_tiles['+str(len(raw)//128)+'][32]={'+','.join('{'+','.join(hex(x) for x in struct.unpack('<32I',raw[i:i+128]))+'}' for i in range(0,len(raw),128))+'};\n'
(out/'spell_expansion_assets.h').write_text(h);(out/'spell-geometry-stage72-report.json').write_text(json.dumps({'effects':audit,'small_frames':len(small),'large_frames':len(large),'atlas_bytes':sum(map(len,small+large))},indent=2)+'\n')
for k in (1,2,8):
 strip=Image.new('RGB',(len(frames_for_preview[k])*40,40),'#10141b')
 for j,im in enumerate(frames_for_preview[k]):strip.paste(im,(j*40,40-im.height))
 strip.save(out/f'stage72-native-class{k}.png')
paths += [out/'spell_expansion_assets.h',r/'source_assets/spells/l1.pal'];(out/'spell-geometry-stage72-check.json').write_text(json.dumps({str(p.relative_to(r)):sha(p) for p in paths},indent=2)+'\n')
print('PASS',len(small),'small and',len(large),'large native frames;',sum(map(len,small+large)),'bytes')
