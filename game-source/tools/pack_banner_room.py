"""Original Banner macro overlays and fixed guards (including zero conversion slots)."""
from pathlib import Path
import struct,re,json
r=Path(__file__).resolve().parents[1];src=r/'source_assets/quest_remaining';out=r/'generated'
s=(r/'source_assets/monster_catalog/monstdat.cpp').read_text();body=re.search(r'char MonstConvTbl\[\] = \{\s*#endif(.*?)\};',s,re.S)[1].split('#ifdef')[0];types=[v.strip() for v in re.sub(r'//[^\n]*','',body).split(',') if v.strip()]
header='#pragma once\n';report={}
for name in ['Banner1','Banner2']:
 data=(src/(name+'.DUN')).read_bytes();v=struct.unpack('<'+'H'*(len(data)//2),data);w,h=v[:2];assert (w,h)==(8,8)
 header+='static const uint8_t '+name.lower()+'[64]={'+','.join(map(str,v[2:66]))+'};\n'
 if name=='Banner2':
  guard=[]
  for i,n in enumerate(v[322:578]):
   if not n:continue
   ty=types[n-1]
   if ty=='0':continue
   assert ty in ['MT_BFALLSP','MT_BFALLSD','MT_FAT'],ty
   guard.append((i%16,i//16,{'MT_BFALLSP':30,'MT_BFALLSD':31,'MT_FAT':32}[ty]))
  header+='static const uint8_t banner_guards[][3]={'+','.join('{%d,%d,%d}'%p for p in guard)+'};\n'
  report['guards']=guard
report.update(source_revision='9f01757243412bb154d02d9af65c804159e6e0ea',npc=[8,12],chest=[10,3],rectangles_inclusive=[[6,6,9,9],[0,0,6,2],[0,0,9,9]])
(out/'banner_room.h').write_text(header);(src/'banner-source-report.json').write_text(json.dumps(report,indent=2)+'\n');print('Banner guards',len(guard))
