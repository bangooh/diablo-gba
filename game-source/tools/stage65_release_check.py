"""Bind the UI-only Stage65 change to original source manifest and native tests."""
from pathlib import Path
import hashlib,json,struct
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 baseline=json.loads((ROOT/'PACKAGE-MANIFEST-STAGE64.json').read_text())
 expected={'src/shop_ui.inc','src/town_dialogue.inc','src/infopanel.inc'}
 sources=[p for p in (ROOT/'src').rglob('*') if p.is_file()]+[ROOT/'build.py',ROOT/'link.ld']
 assets=[p for p in (ROOT/'generated').iterdir() if p.suffix in ('.h','.inc','.lz','.rle','.bin')]
 inputs={str(p.relative_to(ROOT)):sha(p) for p in sources+assets}
 changes={n for n,h in inputs.items() if n not in baseline['files'] or baseline['files'][n]['sha256']!=h}
 assert changes==expected,changes
 old_sources={n for n in baseline['files'] if n.startswith('src/')}
 assert old_sources<={str(p.relative_to(ROOT)) for p in sources},'deleted production input'
 rom=ROOT/'build/diablo-gba-assets.gba';elf=ROOT/'build/diablo-gba-assets.elf';meta=ROOT/'build/diablo-gba-assets-build-report.json'
 data=rom.read_bytes();image=elf.read_bytes();build=json.loads(meta.read_text());rom_sha=sha(rom)
 assert len(data)==33554432 and build['sha256']==rom_sha and rom_sha!=baseline['rom_sha256']
 assert build['arm_elf']==build['gba_header_checksum']=='PASS'
 assert (sum(data[160:190])+0x19)&255==0
 off=struct.unpack_from('<I',image,28)[0];size,count=struct.unpack_from('<HH',image,42);end=ewram=iwram=0
 for i in range(count):
  kind,at,va,pa,fs,ms,flags,align=struct.unpack_from('<8I',image,off+i*size)
  if kind!=1:continue
  if 0x02000000<=va<0x02040000:assert va+ms<=0x02040000;ewram+=ms
  if 0x03000000<=va<0x03008000:assert va+ms<=0x03006000;iwram+=ms
  if fs and 0x08000000<=pa<0x0a000000:
   dst=pa-0x08000000;end=max(end,dst+fs)
   for a,b in ((dst,min(dst+fs,4)),(max(dst,192),dst+fs)):
    if b>a:assert data[a:b]==image[at+a-dst:at+b-dst]
 assert 0<end<=len(data) and data[end:]==b'\xff'*(len(data)-end)
 assert ewram==build['ewram_static_bytes'] and iwram==build['iwram_code_bytes']
 evidence={};reports={}
 for name in ('shop-host','shop-native','parity'):
  p=ROOT/f'generated/stage65-validation/{name}/report.json';d=json.loads(p.read_text());assert d['result']=='PASS',(name,d.get('error'))
  evidence[str(p.relative_to(ROOT))]=sha(p);reports[name]=d
  assert d['sourceSha256']
  for n,h in d['sourceSha256'].items():assert sha(ROOT/n)==h,(name,n);evidence[n]=h
 native=reports['shop-native'];assert native['romSha256']==rom_sha and native['elfSha256']==sha(elf)
 assert len(native['cases'])>=6 and all(c['result']=='PASS' for c in native['cases'])
 assert any(len(c.get('description',[]))==5 for c in native['cases'])
 for name in native['screenshots']:
  p=ROOT/'generated/stage65-validation/shop-native'/name;assert p.is_file();evidence[str(p.relative_to(ROOT))]=sha(p)
 assert len(reports['shop-host']['checks'])>=9
 docs=['README.md','SHOP-AND-AUDIT-STAGE65.md','PARITY-AUDIT-STAGE65.md','tools/stage65_release_check.py','tools/package_stage65.py','PACKAGE-MANIFEST-STAGE64.json']
 result={'result':'PASS','stage':65,'rom_sha256':rom_sha,'elf_sha256':sha(elf),'build_report_sha256':sha(meta),'rom_payload_bytes':end,'rom_free_bytes':len(data)-end,'ewram_static_bytes':ewram,'iwram_code_bytes':iwram,
  'changed_production_files':sorted(changes),'save_format_unchanged_from_stage64':True,'save_version':128,'save_generator':18,'link_version':132,'physical_console_tested':False,
  'baseline_rom_sha256':baseline['rom_sha256'],'build_inputs_sha256':inputs,'evidence_inputs_sha256':evidence,'deliverable_sha256':{p:sha(ROOT/p) for p in docs}}
 (ROOT/'generated/stage65-release-report.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps({k:v for k,v in result.items() if not isinstance(v,dict)},indent=2))
if __name__=='__main__':main()
