#!/usr/bin/env python3
"""Bind Stage64 source, final ROM/ELF and completed object/control evidence.

Read-only gate: missing, stale, RUNNING or failed evidence never becomes PASS.
Compiler-equivalence evidence is intentionally bound to its separate Stage63
ARM/Thumb ROM pair, not mislabelled as a final Stage64 gameplay test.
"""
import argparse,hashlib,json,re,struct,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
VALIDATION=ROOT/'generated/stage64-validation'
AUTOMAP_SHA='4558049957cace8a2e45e36b830af2346d2a4f3cbc64b19a963acc9f8987187f'
HOSTS=('door-controls-host/report.json','item-shrines-host/report.json',
       'prop-archive-host/report.json','environment-host/report.json',
       'environment-assets/report.json','theme-architecture/report.json',
       'theme-rooms/report.json')
NATIVE={'door-controls-native/report.json':('cases',11),
        'environment-native/report.json':('interaction',4)}
HASH_KEYS=('source_sha256','sourceSha256','production_sources_sha256',
           'source_hashes','generated_hashes','harnessSha256','oracleSha256')

def sha(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
def load(path):return json.loads(path.read_text())
def rel(name):
 p=Path(name);assert not p.is_absolute() and '..' not in p.parts, name
 return p
def passed(d,name):assert d.get('result',d.get('status'))=='PASS',f'Not PASS: {name}'
def source_binding(d,name):
 bound={}
 if name=='armv4-abi/report.json':
  file='tools/stage64_thumb_helpers_audit.py';assert sha(ROOT/file)==d['toolSha256'],f'Stale audit tool: {name}'
  bound[file]=d['toolSha256']
 for key in HASH_KEYS:
  if key not in d:continue
  assert isinstance(d[key],dict) and d[key],(name,key)
  for file,digest in d[key].items():
   assert isinstance(digest,str) and re.fullmatch('[0-9a-f]{64}',digest),(name,file)
   path=ROOT/rel(file);assert sha(path)==digest,f'Stale tested input: {name}: {file}'
   bound[file]=digest
 return bound

def host_scope(name,d):
 if name.startswith('door-controls-host/'):
  a,b=d['cases']['baseline'],d['cases']['candidate']
  for key,n in [('movement_cases',139),('target_cases',96),('obstacle_cases',2048),('environment_cases',60)]:assert b[key]>=n,(name,key)
  assert a['cardinal_hash']==b['cardinal_hash'] and len(d['regional_doors_regression'])>=6
 elif name.startswith('item-shrines-host/'):
  assert d['checks']>=2365 and d['types']==[1,2,3]
  assert d['item_bytes']==d['drop_bytes']==16 and d['static_ram_added']==0
 elif name.startswith('prop-archive-host/'):
  c,f=d['codec'],d['flash'];passed(c,name+'/codec');passed(f,name+'/flash')
  assert c['valid_records']==14131200 and c['legacy_byte_identical']==10854400
  assert c['extended_records']==3276800 and c['all_24bit_inputs']==16777216
  assert c['canonical_decodes']==c['valid_records'] and c['invalid_state_records']==143155200
  assert f['flash_kind_state_combinations']==512 and f['checks']>=15250
  assert f['live_prop_bytes']==4 and f['saved_prop_bytes']==3 and f['static_ram_added']==0
 elif name.startswith('environment-host/'):
  assert d['checks']>=39000 and d['new_prop_kinds']==17 and d['native_fountains']==4
 elif name.startswith('environment-assets/'):
  assert d['new_frames']>=48 and d['new_pixels_checked']>=196608 and d['old_pixels_checked']>=557056
  assert d['persistent_ram_added']==0 and d['audio']['full_source_duration_retained'] is True
 elif name.startswith('theme-architecture/'):
  c=d['coverage'];assert c['fitCases']>=460800 and c['scanCases']>=1152
  assert c['fitMismatches']==c['scanMismatches']==0 and len(c['families'])==3
 elif name.startswith('theme-rooms/'):
  assert d['sourceStableBeforeAfter'] is True
  c=d['coverage'];assert c['floorCases']>=240 and c['regenerationCases']>=15 and c['committedRooms']>0 and c['zharCases']>=1
  assert c['zharActiveCases']>=5 and 'ZharNoLibraryPlayableFallback' in d['checks']
  assert len(c['themeCounts'])==17 and all(v>0 for v in c['themeCounts'])
  assert all(c['propCounts'].get(str(k),c['propCounts'].get(k,0))>0 for k in range(64,81))
  assert d['scratchBytes']<=16856 and d['runtimeRamDelta']==0
 elif name=='armv4-abi/report.json':
  assert d['auditedHelperCount']>=14 and d['unsafeHelpers']==[] and d['expectedUnsafeHelpers']==[]
  assert len(d['checks'])==3 and all(c['result']=='PASS' for c in d['checks'])
  wrappers=[c for c in d['checks'] if c['name'] in ('__aeabi_uidivmod','__aeabi_idivmod')]
  assert len(wrappers)==2
  for c in wrappers:
   assert c['strongRomWrapper'] is True and c['bytes']==32 and c['stackAlignmentAtCall']==8
   assert c['quotientRegister']=='r0' and c['remainderRegister']=='r1' and c['returnInstruction']=='BX lr'

def native_spec(s):
 parts=s.rsplit(':',2)
 assert len(parts)==3,'Use PATH:ARRAY:MIN for --native-report'
 name,field,count=parts;rel(name);assert int(count)>0
 return name,(field,int(count))

def compiler_comparison(path,baseline_path,baseline_sha,candidate_sha):
 d=load(path);passed(d,str(path));a,b=d['runs']['baseline'],d['runs']['candidate']
 floors={5,9,13};exact={'hero','downstairs','collisionSha256','propsSha256','enemiesSha256','dropsSha256','piecesAndDepthSha256','questSha256','propCount','enemyCount'}
 assert 'Private ROM' in d['method'] and d['limits'],'Compiler fixture derivation must be explicit'
 for label,r,want in [('baseline',a,baseline_sha),('candidate',b,candidate_sha)]:
  passed(r,'compiler/'+label);assert r['romSha256']==want and re.fullmatch('[0-9a-f]{64}',r['elfSha256'])
  assert len(r['levels'])==3 and {x['level'] for x in r['levels']}==floors
  assert set(r['functions'])=={'game_generate_dungeon','random_','worldgen_l2','worldgen_l3','worldgen_l4'}
  assert all(f['thumb']==(label=='candidate') for f in r['functions'].values()),label
  assert len(r['entryPatches'])==3 and {x['floor'] for x in r['entryPatches']}==floors
  for patch in r['entryPatches']:
   before,after=bytes.fromhex(patch['before']),bytes.fromhex(patch['after'])
   assert len(before)==len(after)==8 and before[:4]==bytes.fromhex('0110a0e3') and before[7]==0xea
   assert after==bytes([patch['floor']])+before[1:] and patch['changedBytes']==1
   offset=patch['romOffset'];assert patch['armAddress']==0x08000000+offset and offset>=192
   assert re.fullmatch('[0-9a-f]{64}',patch['derivedRomSha256'])
   if label=='baseline':
    original=bytearray(baseline_path.read_bytes());assert original[offset:offset+8]==before
    original[offset]=patch['floor'];assert hashlib.sha256(original).hexdigest()==patch['derivedRomSha256']
 assert candidate_sha!=baseline_sha
 shims=b['abiCompatShims'];assert shims['sourceSha256']==sha(ROOT/'src/start.s')
 assert re.fullmatch('[0-9a-f]{64}',shims['provenanceSha256'])
 provenance=path.with_name('private-build-provenance.json');assert sha(provenance)==shims['provenanceSha256']
 provenance_data=load(provenance)
 assert provenance_data['source_sha256']['src/start.s']==shims['sourceSha256']
 assert provenance_data['build_report']['sha256']==candidate_sha
 assert len(shims['functions'])==2 and {f['name'] for f in shims['functions']}=={'__aeabi_uidivmod','__aeabi_idivmod'}
 for f in shims['functions']:
  assert f['size']==32 and f['address']==0x08000000+f['romOffset'] and f['returnInstruction']=='bx lr' and f['stackAlignment']==8
  code=bytes.fromhex(f['bytes']);assert len(code)==32
  words=struct.unpack('<8I',code)
  assert words[:3]==(0xe52de004,0xe24dd004,0xe1a0200d) and words[3]>>24==0xeb
  assert words[4:]==(0xe59d1000,0xe28dd004,0xe49de004,0xe12fff1e)
 assert len(d['checks'])==3 and {x['floor'] for x in d['checks']}==floors
 for c in d['checks']:
  passed(c,'compiler floor');aa=next(x for x in a['levels'] if x['level']==c['floor']);bb=next(x for x in b['levels'] if x['level']==c['floor'])
  assert set(c['fields'])==exact
  for key in c['fields']:assert aa[key]==bb[key],('compiler',c['floor'],key)
 return d

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--baseline',type=Path,required=True);p.add_argument('--freeze',type=Path,required=True)
 p.add_argument('--output',type=Path,required=True);p.add_argument('--stage-doc',required=True)
 p.add_argument('--thumb-rom-sha',required=True,help='Exact separate compiler-only candidate ROM SHA256')
 p.add_argument('--thumb-report',default='thumb-worldgen-native/report.json')
 p.add_argument('--host-report',action='append',default=[],help='Additional report relative to Stage64 validation')
 p.add_argument('--native-report',action='append',default=[],metavar='PATH:ARRAY:MIN',help='Add or override an explicit native report array/count requirement')
 p.add_argument('--expected-change',action='append',default=[],help='Optional exact production diff allowlist')
 a=p.parse_args();assert re.fullmatch('[0-9a-f]{64}',a.thumb_rom_sha)
 rom,elf,build_report=[ROOT/'build'/('diablo-gba-assets'+s) for s in ['.gba','.elf','-build-report.json']]
 meta=load(build_report);rom_sha=sha(rom);old_sha=sha(a.baseline/'diablo-gba-assets.gba')
 assert rom_sha!=old_sha and meta['sha256']==rom_sha
 assert meta['arm_elf']==meta['gba_header_checksum']=='PASS'
 data=rom.read_bytes();assert len(data)==meta['rom_bytes'] and 0<len(data)<=33554432
 assert len(data)&(len(data)-1)==0 and (sum(data[160:190])+0x19)&255==0
 prod=sorted(x for x in (ROOT/'src').rglob('*') if x.is_file())+[ROOT/'build.py',ROOT/'link.ld']
 assets=sorted(x for x in (ROOT/'generated').iterdir() if x.suffix in ('.h','.inc','.lz','.rle','.bin'))
 required={str(x.relative_to(ROOT)) for x in prod+assets};frozen=load(a.freeze)
 assert set(frozen)==required,{'missing':sorted(required-set(frozen)),'extra':sorted(set(frozen)-required)}
 for name,digest in frozen.items():assert sha(ROOT/name)==digest,f'Input changed after build: {name}'
 changed=[str(x.relative_to(ROOT)) for x in prod if not (a.baseline/x.relative_to(ROOT)).exists() or sha(x)!=sha(a.baseline/x.relative_to(ROOT))]
 changed += [str(x.relative_to(a.baseline)) for x in (a.baseline/'src').rglob('*') if x.is_file() and not (ROOT/x.relative_to(a.baseline)).exists()]
 if a.expected_change:assert set(changed)==set(a.expected_change),changed
 assert sha(ROOT/'src/automap_raster.inc')==AUTOMAP_SHA,'Minimap changed despite door-marker exclusion'
 menu,link=(ROOT/'src/menu.inc').read_text(),(ROOT/'src/link.h').read_text()
 for text,key,value in [(menu,'SAVE_VERSION',128),(menu,'SAVE_GENERATOR',18),(link,'LINK_VERSION',132)]:
  assert int(re.search(r'\b'+key+r'\s*(?:=\s*)?(\d+)',text)[1])==value,key
 image=elf.read_bytes();assert image[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<H',image,18)[0]==40
 phoff=struct.unpack_from('<I',image,28)[0];phsize,phcount=struct.unpack_from('<HH',image,42)
 payload=ewram=iwram=0
 for i in range(phcount):
  kind,off,va,pa,filesz,memsz,flags,align=struct.unpack_from('<8I',image,phoff+i*phsize)
  if kind!=1:continue
  if 0x02000000<=va<0x02040000:assert va+memsz<=0x02040000;ewram+=memsz
  if 0x03000000<=va<0x03008000:assert va+memsz<=0x03006000;iwram+=memsz
  if filesz and 0x08000000<=pa<0x0a000000:
   dst=pa-0x08000000;end=dst+filesz;payload=max(payload,end)
   # build.py patches bytes4..191 only. The entry branch is also verified.
   for left,right in [(dst,min(end,4)),(max(dst,192),end)]:
    if right>left:assert data[left:right]==image[off+left-dst:off+right-dst],('ELF/ROM',left,right)
 assert 0<payload<=33554432 and data[payload:]==b'\xff'*(len(data)-payload)
 assert ewram==meta['ewram_static_bytes']<262144 and iwram==meta['iwram_code_bytes']<24576
 assert re.search(r'FAST\s*\(rwx\)\s*:\s*ORIGIN\s*=\s*0x03000000,\s*LENGTH\s*=\s*24K',(ROOT/'link.ld').read_text())
 startup=(ROOT/'src/start.s').read_text();assert 'ldr sp, =0x03007d00' in startup and 'ldr sp, =0x03007fa0' in startup
 symbols=subprocess.check_output(['readelf','-sW',str(elf)],text=True);assert not re.search(r'\bminimap_diag\b',symbols)
 hosts=list(HOSTS)+a.host_report;host_hashes={};evidence_inputs={};host_reports={}
 for name in hosts:
  path=VALIDATION/rel(name);d=load(path);passed(d,name);bound=source_binding(d,name);assert bound,f'No source binding: {name}'
  if 'romSha256' in d:assert d['romSha256']==rom_sha,f'Wrong final ROM: {name}'
  if 'elfSha256' in d:assert d['elfSha256']==sha(elf),f'Wrong final ELF: {name}'
  evidence_inputs.update(bound);host_scope(name,d);host_hashes[name]=sha(path);host_reports[name]=d
 natives=dict(NATIVE)
 for spec in a.native_report:name,condition=native_spec(spec);natives[name]=condition
 native_hashes={};native_counts={}
 for name,(field,minimum) in natives.items():
  path=VALIDATION/rel(name);d=load(path);passed(d,name)
  assert d.get('romSha256',d.get('rom_sha256'))==rom_sha,f'Wrong final ROM: {name}'
  if 'elfSha256' in d:assert d['elfSha256']==sha(elf),name
  assert isinstance(d[field],list) and len(d[field])>=minimum,(name,field,minimum)
  if 'controllerSourceSha256' in d:assert d['controllerSourceSha256']==sha(ROOT/'src/controller.c')
  evidence_inputs.update(source_binding(d,name));native_hashes[name]=sha(path);native_counts[name]={field:len(d[field])}
  if name=='door-controls-native/report.json':
   assert d['mode']=='candidate'
   for c in d['cases']:
    if c['kind'] in ('generated-door','generated-diagonal-wall-door'):assert c['minimapOn'] and c['enteredDoor'] and c['continuedThrough']
    if c['name']=='closed-door-chest-priority':assert c['actualAOperated']=='closed-door'
    if c['name']=='occupied-open-door':assert c['actorCollisionPreserved'] is True
  if name=='environment-native/report.json':
   assert d['minimapOn'] is True and d['newNativeFramesVerified']==48 and d['opaquePixelsVerified']>0
   assert {c['kind'] for c in d['interaction']}=={64,65,66,67}
   assert all(c['actualA'] and c['nativeSaveLoad'] and c['repeatedA']==('repeatable' if c['kind']<66 else 'spent') for c in d['interaction'])
   assert len(d['collision'])==8 and {c['kind'] for c in d['collision']}=={65,66}
   assert len({(c['kind'],c['side']) for c in d['collision']})==8
   assert all(c['blockedFootprint'] and c['actualAReachable'] for c in d['collision'])
   assert {f['frame'] for c in d['visuals'] for f in c['frames'] if f['frame']>=136}==set(range(136,184))
  if name=='minimap-pacing-native/report.json':
   passed(d['fixtureValidation'],name+'/fixture')
   assert d['fixtureValidation']['allOtherEnemyBytesUnchanged'] is True
   assert d['captureScope']['warmupHardwareFramesPerCondition']>=120 and d['captureScope']['measuredHardwareFramesPerCondition']>=600
   expected={('L1-native-fog',False),('L1-native-fog',True),('L1-all-explored',True)}
   assert {(c['region'],c['mapOn']) for c in d['samples']}==expected
   for c in d['samples']:
    assert c['mode']=='walk' and c['hardwareFrames']>=600 and c['before']['f']==0 and c['after']['f']==c['hardwareFrames']
    assert 0<c['fps']<=61 and 0<c['minRolling60HardwareFramesFps']<=61 and c['logicalMoves']>0 and c['mapOverruns']==0
   for state in d['states'].values():assert state['samePalette'] and state['sameTerrainPixels'] and state['sameHardwareLightMask']
 thumb_name=str(rel(a.thumb_report));thumb=compiler_comparison(VALIDATION/thumb_name,a.baseline/'diablo-gba-assets.gba',old_sha,a.thumb_rom_sha)
 provenance_path=(VALIDATION/thumb_name).with_name('private-build-provenance.json')
 evidence_inputs[str(provenance_path.relative_to(ROOT))]=sha(provenance_path)
 docs=['README.md',str(rel(a.stage_doc)),'tools/stage64_release_check.py','tools/package_stage64.py']
 deliverables={n:sha(ROOT/n) for n in docs}
 result={'result':'PASS','stage':64,'rom_sha256':rom_sha,'elf_sha256':sha(elf),'build_report_sha256':sha(build_report),
  'baseline_rom_sha256':old_sha,'changed_production_sources':sorted(changed),'build_inputs_sha256':frozen,
  'host_validation_sha256':host_hashes,'native_validation_sha256':native_hashes,'native_validation_counts':native_counts,
  'comparison_validation_sha256':{thumb_name:sha(VALIDATION/thumb_name)},'evidence_inputs_sha256':evidence_inputs,'deliverable_sha256':deliverables,
  'stage_document':str(rel(a.stage_doc)),'rom_payload_bytes':payload,'rom_free_bytes':33554432-payload,
  'ewram_static_bytes':ewram,'ewram_static_free_bytes':262144-ewram,'iwram_code_bytes':iwram,'iwram_code_headroom_bytes':24576-iwram,
  'iwram_stack_reserved_bytes':8192,'main_stack_top':'0x03007d00','irq_stack_top':'0x03007fa0',
  'save_version':128,'save_generator':18,'link_version':132,'prior_full_saves_compatible':False,
  'minimap_unchanged_sha256':AUTOMAP_SHA,'physical_console_tested':False,
  'item_record_bytes':host_reports['item-shrines-host/report.json']['item_bytes'],
  'drop_record_bytes':host_reports['item-shrines-host/report.json']['drop_bytes'],
  'prop_archive':host_reports['prop-archive-host/report.json']['flash'],
  'compiler_comparison':{'baseline_rom_sha256':old_sha,'candidate_rom_sha256':a.thumb_rom_sha,'checks':thumb['checks'],
   'entry_patches':{label:run['entryPatches'] for label,run in thumb['runs'].items()},'abi_compat_shims':thumb['runs']['candidate']['abiCompatShims'],'method':thumb['method'],'limits':thumb['limits'],
   'preserved_provenance':str(provenance_path.relative_to(ROOT)),
   'scope':'Separate ARM/Thumb compilation and ARMv4 ABI-adapter equivalence on unchanged game logic; private town-entry immediate fixtures for floors5/9/13; not final Stage64 gameplay or sequential campaign traversal'}}
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps({k:v for k,v in result.items() if not isinstance(v,dict)},indent=2))
if __name__=='__main__':main()
