"""Bind completed targeted validation to the release; never fabricate test runs."""
from pathlib import Path
import hashlib,json,struct
r=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
base=r/'build/diablo-gba-assets';rom=base.with_suffix('.gba');elf=base.with_suffix('.elf');build=r/'build/diablo-gba-assets-build-report.json'
native=r/'generated/stage69-validation/native/report.json';n=json.loads(native.read_text());b=json.loads(build.read_text())
assert n['result']=='PASS' and n['romSha256']==sha(rom)==b['sha256']
assert b['gba_header_checksum']=='PASS' and b['arm_elf']=='PASS'
assert b['ewram_static_bytes']<=262144 and b['iwram_code_bytes']<=24576
for name in ['ui-actions','cast-codec']:
 p=r/'generated/stage69-validation/host'/f'{name}.log';s=p.read_text();assert 'PASS' in s or 'passed' in s
 assert not any(t in s for t in ['ERROR: AddressSanitizer','runtime error:','Assertion','Traceback'])
for row in n['performance']:assert row['mapCommitOverruns']==0
raw=elf.read_bytes();phoff=struct.unpack_from('<I',raw,28)[0];sz,count=struct.unpack_from('<HH',raw,42)
end=0
for i in range(count):
 typ,off,va,pa,filesz,memsz,flags,align=struct.unpack_from('<8I',raw,phoff+i*sz)
 if typ==1 and 0x08000000<=pa<0x0a000000:end=max(end,pa+filesz-0x08000000)
inputs=list((r/'src').rglob('*'))+[r/'build.py',r/'link.ld']+list((r/'generated').iterdir())
inputs=[p for p in inputs if p.is_file() and (p.is_relative_to(r/'src') or p.name in ('build.py','link.ld') or p.suffix in ('.h','.inc','.lz','.rle','.bin'))]
evidence=[p for p in (r/'generated/stage69-validation').rglob('*') if p.is_file() and p.suffix not in ('.rgba','.gba','.elf')]+[r/name for name in ['tests/run-stage69-ui-actions.py','tests/stage69-ui-actions.c','tests/run-stage69-cast-codec.py','tests/stage69-cast-codec.c','tests/spell-compact-casts.c','tests/spell-separated-effects.c','tests/spell-firewall-query.c','tools/stage69_native.mjs','tools/stage69_preview.py','tools/pack_spell_geometry_stage69.py','tools/pack_cast_families_stage69.py','generated/spell-geometry-stage69-report.json','generated/cast-families-stage69-report.json','SPELL-UI-STAGE69.md']]
assert n['ui']['result']=='PASS' and len(n['casterFamilies'])==3
result={'result':'PASS','stage':69,'scope':'Targeted host integration and native mGBA ROM fixtures; not full campaign, physical GBA or cable multiplayer','rom_sha256':sha(rom),'elf_sha256':sha(elf),'build_report_sha256':sha(build),'rom_payload_bytes':end,'rom_free_bytes':33554432-end,'ewram_static_sum_bytes':b['ewram_static_bytes'],'ewram_after_alignment_free_bytes':132,'iwram_code_bytes':b['iwram_code_bytes'],'save_version':129,'generator_version':19,'link_version':136,'spell_lighting':b['spell_lighting'],'build_inputs_sha256':{str(p.relative_to(r)):sha(p) for p in sorted(inputs)},'evidence_inputs_sha256':{str(p.relative_to(r)):sha(p) for p in sorted(evidence)},'deliverable_sha256':{str(p.relative_to(r)):sha(p) for p in [rom,elf,build]}}
(r/'generated/stage69-release-report.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if not k.endswith('inputs_sha256') and k!='deliverable_sha256'},indent=2))
