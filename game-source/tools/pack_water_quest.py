"""Native Poisoned Water map, guards, entrance and two cave palettes."""
from pathlib import Path
import hashlib,json,re,struct,sys
import mpyq
from extract_assets import read_mpq,tile_frame
from PIL import Image
r=Path(__file__).resolve().parents[1];src=r/'source_assets/quest_remaining';out=r/'generated';world=r/'source_assets/worldgen'
if len(sys.argv)>1:
    a=mpyq.MPQArchive(sys.argv[1],listfile=False)
    for name in ['L3pfoul.pal','L3pwater.pal']:(src/name).write_bytes(read_mpq(a,'Levels\\L3Data\\'+name))
b=(src/'Foulwatr.DUN').read_bytes();v=struct.unpack('<'+'H'*(len(b)//2),b);w,h=v[:2];assert (w,h)==(19,37)
s=(r/'source_assets/monster_catalog/monstdat.cpp').read_text();body=re.search(r'char MonstConvTbl\[\] = \{\s*#endif(.*?)\};',s,re.S)[1].split('#ifdef')[0]
types=[n.strip() for n in re.sub(r'//[^\n]*','',body).split(',') if n.strip()]
mapping={'MT_DFALLSP':39,'MT_YFALLSD':38,'MT_NGOATMC':15,'MT_NGOATBW':37};guards=[]
for i,n in enumerate(v[2+5*w*h:2+9*w*h]):
    if n and types[n-1]!='0':guards.append((i%(2*w),i//(2*w),mapping[types[n-1]]))
assert len(guards)==38
objects=[(i,n) for i,n in enumerate(v[2+9*w*h:2+13*w*h]) if n];assert not objects
s=(r/'source_assets/cathedral_details/drlg_l1.cpp').read_text();entrance=list(map(int,re.findall(r'\d+',re.search(r'BYTE PWATERIN\[\d*\] = \{(.*?)\};',s,re.S)[1])))
assert entrance[:2]==[6,6] and len(entrance)==74
entry=entrance[38:];assert entry[7]==202
header=['#pragma once','static const uint8_t water_entrance[36]={'+','.join(map(str,entry))+'};','static const uint8_t water_macros[]={'+','.join(map(str,v[2:2+w*h]))+'};','static const uint8_t water_guards[][3]={'+','.join('{%d,%d,%d}'%g for g in guards)+'};']
base=(world/'L1_1.PAL').read_bytes();rgb=[tuple(base[i:i+3]) for i in range(0,768,3)]
for name,path in [('caves',world/'L3_1.PAL'),('water_foul',src/'L3pfoul.pal'),('water_clean',src/'L3pwater.pal')]:
    palette=path.read_bytes();colors=[tuple(palette[i:i+3]) for i in range(0,768,3)]
    lut=[0]+[min(range(1,256),key=lambda j:sum((rgb[j][k]-colors[i][k]*.5)**2 for k in range(3))) for i in range(1,256)]
    header.append('static const uint8_t '+name+'_lut[256]={'+','.join(map(str,lut))+'};')
(out/'water_quest.h').write_text('\n'.join(header)+'\n')
# Retain native L3 pixel indices so both quest palettes share the ordinary cave
# atlas. Existing caves use their exact previous mapping at render time.
cel=(world/'L3.CEL').read_bytes();mini=(world/'L3.MIN').read_bytes();count=len(mini)//20
blob=bytearray();atlas={};tiles=[]
for n in range(count):
    im=Image.new('P',(64,160))
    for j,word in enumerate(struct.unpack_from('<10H',mini,n*20)):
        t=tile_frame(cel,word);im.paste(t,((j&1)*32,(j//2)*32),Image.frombytes('L',t.size,bytes(255 if c else 0 for c in t.tobytes())))
    data=im.resize((32,80),Image.Resampling.NEAREST).tobytes()
    for y in range(10):
        for x in range(4):
            block=bytes(data[(y*8+j)*32+x*8+i] for j in range(8) for i in range(8))
            if block not in atlas:atlas[block]=len(blob)//64;blob.extend(block)
            tiles.append(atlas[block]*4+(0 if not any(block) else 1 if all(block) else 2))
assert max(tiles)<65536
p=out/'world_l3.inc';text=p.read_text();text=re.sub(r'(l3_tiles\[\]=\{)[^}]+',lambda m:m[1]+','.join(map(str,tiles)),text);p.write_text(text)
(out/'world_l3.rle').write_bytes(blob)
report={'revision':'9f01757243412bb154d02d9af65c804159e6e0ea','map':[w,h],'spawn':[15,67],'exit':[14,67],'guards':guards,'entry':entry,'native_palette_to_shared_BG':True,'native_L3_atlas_bytes':len(blob),'source_sha256':{name:hashlib.sha256((src/name).read_bytes()).hexdigest() for name in ['Foulwatr.DUN','L3pfoul.pal','L3pwater.pal','setmaps.cpp','trigs.cpp']}}
(src/'water-source-report.json').write_text(json.dumps(report,indent=2)+'\n');print('Water: 38 native guards, 19x37 map, original entrance, foul/clean palettes;',len(blob),'L3 atlas bytes')
