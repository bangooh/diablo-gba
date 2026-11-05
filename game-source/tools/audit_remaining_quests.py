"""Audit native quest DUNs and distinguish enabled Banner from pending side levels."""
from pathlib import Path
import json,hashlib,struct,sys,re
r=Path(__file__).resolve().parents[1];out=r/'source_assets/quest_remaining';out.mkdir(exist_ok=True)
paths=['Levels\\L1Data\\Banner1.DUN','Levels\\L1Data\\Banner2.DUN','Levels\\L1Data\\SklKng1.DUN','Levels\\L1Data\\SklKng2.DUN','Levels\\L3Data\\Foulwatr.DUN']
if len(sys.argv)>1:
 import mpyq
 from extract_assets import read_mpq
 a=mpyq.MPQArchive(sys.argv[1],listfile=False)
 for path in paths:(out/path.split('\\')[-1]).write_bytes(read_mpq(a,path))
source=(r/'source_assets/monster_catalog/monstdat.cpp').read_text()
conversion=re.search(r'char MonstConvTbl\[\] = \{\s*#endif(.*?)\};',source,re.S)[1]
types=[v.strip() for v in re.sub(r'//[^\n]*','',conversion.split('#ifdef')[0]).split(',') if v.strip()]
report={'source_revision':'9f01757243412bb154d02d9af65c804159e6e0ea','runtime_enabled':['Ogden Sign','King Leoric'],'maps':{}}
for path in paths:
 name=path.split('\\')[-1];b=(out/name).read_bytes();v=struct.unpack('<'+'H'*(len(b)//2),b);w,h=v[:2];start=2+w*h+4*w*h
 monsters=[{'x':i%(2*w),'y':i//(2*w),'native_type':types[n-1]} for i,n in enumerate(v[start:start+4*w*h]) if n and types[n-1]!='0']
 report['maps'][name]={'mpq_path':path,'sha256':hashlib.sha256(b).hexdigest(),'width':w,'height':h,'monster_layer':monsters,'words':len(v)}
report['quests']=[{'name':'Ogden Sign','native_id':7,'floor':4,'reward_unique':5,'branches':['Snotspil introduction unlocks sign chest','return sign to Ogden awards Harlequin Crest','giving sign to Snotspil consumes it without Harlequin reward','both routes open the native room and make Snotspil hostile'],'needs':['L1 Banner2 closed/Banner1 open overlays','Snotspil talk-to-hostile transition','third pending town reward ledger']},{'name':'King Leoric','native_id':12,'floor':3,'setlevel':1,'reward_unique':1,'needs':['separate Cathedral map routing and saved state','Skeleton King combat/reward','return entrance restoration']},{'name':'Poisoned Water','native_id':13,'floor':2,'setlevel':4,'reward_unique':4,'needs':['separate Caves map routing and saved state','all non-golem monsters killed completion','Pepin reward and poisoned-to-clean palette transition']}]
for quest in report['quests']:
 quest['runtime_enabled']=quest['native_id'] in (7,12)
 if quest['native_id'] in (7,12):quest['implemented']=quest.pop('needs')
report['selection']='King/Water mutually exclusive; Banner shares the native Butcher/Banner/Gharbad group. Exact original RNG sequence is not yet ported.'
report['archive_constraint']='18 compact snapshots fit: 65320 payload bytes, 3560 bytes per region. Normal floors 1..16 and King region17 are routed; slot18 is reserved for Poisoned Water.'
(out/'source-report.json').write_text(json.dumps(report,indent=2)+'\n')
print({name:(m['width'],m['height'],len(m['monster_layer'])) for name,m in report['maps'].items()})
