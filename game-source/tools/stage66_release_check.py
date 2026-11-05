"""Bind complete native and host evidence, linked audio, and ROM capacity."""
from pathlib import Path
import hashlib,json,struct,subprocess
ROOT=Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 rom=ROOT/'build/diablo-gba-assets.gba';elf=ROOT/'build/diablo-gba-assets.elf';meta=ROOT/'build/diablo-gba-assets-build-report.json'
 data=rom.read_bytes();image=elf.read_bytes();build=json.loads(meta.read_text());rom_sha=sha(rom)
 assert len(data)==33554432 and build['sha256']==rom_sha and build['gba_header_checksum']==build['arm_elf']=='PASS'
 assert (sum(data[160:190])+0x19)&255==0
 off=struct.unpack_from('<I',image,28)[0];size,count=struct.unpack_from('<HH',image,42);end=ewram=iwram=0
 for i in range(count):
  kind,at,va,pa,fs,ms,flags,align=struct.unpack_from('<8I',image,off+i*size)
  if kind!=1:continue
  if 0x02000000<=va<0x02040000:assert va+ms<=0x02040000;ewram+=ms
  if 0x03000000<=va<0x03008000:assert va+ms<=0x03006000;iwram+=ms
  if fs and 0x08000000<=pa<0x0a000000:
   dst=pa-0x08000000;end=max(end,dst+fs)
   for a,b in ((max(dst,192),dst+fs),):
    if b>a:assert data[a:b]==image[at+a-dst:at+b-dst]
 assert ewram==build['ewram_static_bytes'] and iwram==build['iwram_code_bytes'] and set(data[end:])=={255}
 sy={}
 for line in subprocess.check_output(['readelf','-sW',str(elf)],text=True).splitlines():
  row=line.split()
  if len(row)>=8 and row[0].endswith(':') and row[1]!='Value':
   try:sy[row[7]]=int(row[1],16)
   except ValueError:pass
 audio=json.loads((ROOT/'generated/audio-stage66-report.json').read_text());assert audio['rate']==6144 and len(audio['clips'])==205
 intervals=[]
 for c in audio['clips']:
  at=sy[c['symbol']]-0x08000000;blob=(ROOT/'generated'/(c['symbol'][6:]+'.lz')).read_bytes()
  assert sha(ROOT/'generated'/(c['symbol'][6:]+'.lz'))==c['sha256'] and data[at:at+len(blob)]==blob
  intervals.append((at,at+len(blob)))
 physical=0;hi=0
 for a,b in sorted(intervals):physical+=max(0,b-max(a,hi));hi=max(hi,b)
 evidence={};reports={}
 for name in ('assets','host','native','map-ui','ending-native'):
  filename='report.json' if name!='ending-native' else 'runtime-report.json'
  p=ROOT/f'generated/stage66-validation/{name}/{filename}';d=json.loads(p.read_text());assert d['result']=='PASS',(name,d.get('error'));reports[name]=d;evidence[str(p.relative_to(ROOT))]=sha(p)
  if 'romSha256' in d:assert d['romSha256']==rom_sha,name
 host=reports['host'];assert len(host['checks'])==7
 for name,h in host['input_sha256'].items():assert sha(ROOT/name)==h,name
 for c in host['checks']:
  p=ROOT/'generated/stage66-validation/host'/(c['script']+'.log');assert sha(p)==c['log_sha256'];evidence[str(p.relative_to(ROOT))]=sha(p)
 assert len(reports['native']['monsters'])==6 and all(c['saveLoad'] and c['minimap'] for c in reports['native']['monsters'])
 assert len(reports['native']['spells'])==4 and len({c['tileSha256'] for c in reports['native']['spells']})==1
 assert reports['assets']['actor_frames_checked']==664 and reports['assets']['new_corpse_frames_checked']==832
 assert reports['assets']['ground_frames_checked']==66
 assert sum(len(c['ui']) for c in reports['map-ui']['cases'])==35
 for p in (ROOT/'generated/stage66-validation').rglob('*.png'):evidence[str(p.relative_to(ROOT))]=sha(p)
 sources=[p for p in (ROOT/'src').rglob('*') if p.is_file()]+[ROOT/'build.py',ROOT/'link.ld']
 assets=[p for p in (ROOT/'generated').iterdir() if p.suffix in ('.h','.inc','.lz','.rle','.bin')]
 docs=['README.md','tools/package_stage66.py','tools/stage66_release_check.py','tools/stage66_native.mjs','tools/stage66_ending_native.mjs','tools/stage66_assets_check.py','tools/stage66_preview.py','generated/stage66-preview.png','generated/audio-stage66-report.json','generated/monster-stage66-report.json','generated/ground-items-report.json','source_assets/monster_stage66/catalog.json','source_assets/audio_stage66/masters.json']
 result={'result':'PASS','stage':66,'rom_sha256':rom_sha,'elf_sha256':sha(elf),'build_report_sha256':sha(meta),'rom_payload_bytes':end,'rom_free_bytes':len(data)-end,'ewram_static_bytes':ewram,'iwram_code_bytes':iwram,'linked_audio_bytes':physical,'audio_rate':6144,'linked_audio_clips':205,'save_version':129,'save_generator':19,'link_version':133,'old_saves_compatible':False,'physical_console_tested':False,'performance':reports['native']['performance'],'build_inputs_sha256':{str(p.relative_to(ROOT)):sha(p) for p in sources+assets},'evidence_inputs_sha256':evidence,'deliverable_sha256':{p:sha(ROOT/p) for p in docs}}
 (ROOT/'generated/stage66-release-report.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if not isinstance(v,dict)},indent=2))
if __name__=='__main__':main()
