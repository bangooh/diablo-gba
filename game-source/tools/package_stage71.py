from pathlib import Path
import json,hashlib,struct,shutil,zipfile,csv,io
from PIL import Image,ImageDraw
import numpy as np
R=Path(__file__).resolve().parents[1];O=R.parent.parent/'stage71-delivery';O.mkdir(exist_ok=True)
V=R/'generated/stage71-validation';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report=json.loads((V/'native-final/report.json').read_text());spells=json.loads((V/'spells/report.json').read_text());build=json.loads((R/'build/diablo-gba-assets-build-report.json').read_text());assert report['result']==spells['result']=='PASS' and len(spells['spells'])==27 and len(report['monsters'])==11;assert report['romSha256']==build['sha256']==sha(R/'build/diablo-gba-assets.gba')
logs={'completion':'/tmp/stage71-completion.log','rosters':'/tmp/stage71-roster.log','effects':'/tmp/stage71-effects.log','music':'/tmp/stage71-music.log','quest_npcs':'/tmp/stage71-quests.log','quest_maps':'/tmp/stage71-quest-extra.log'}
for name,path in logs.items():
 text=Path(path).read_text();assert 'PASS' in text and 'Traceback' not in text and 'Assertion `' not in text,path;(V/(name+'-pass.log')).write_text(text)
# The ELF load addresses, including initialized RAM images, determine ROM use.
elf=(R/'build/diablo-gba-assets.elf').read_bytes();off=struct.unpack_from('<I',elf,28)[0];size,count=struct.unpack_from('<HH',elf,42);ends=[];ewram=[]
for i in range(count):
 typ,fo,va,pa,files,mem,flags,align=struct.unpack_from('<8I',elf,off+i*size)
 if typ==1 and 0x08000000<=pa<0x0a000000:ends.append(pa+files)
 if typ==1 and 0x02000000<=va<0x02040000:ewram.append(va+mem)
summary=dict(result='PASS',stage=71,rom_sha256=build['sha256'],elf_sha256=sha(R/'build/diablo-gba-assets.elf'),rom_bytes=33554432,rom_payload_bytes=max(ends)-0x08000000,rom_free_bytes=0x0a000000-max(ends),ewram_static_sum_bytes=build['ewram_static_bytes'],ewram_after_alignment_free_bytes=0x02040000-max(ewram),iwram_code_bytes=build['iwram_code_bytes'],save_version=130,generator_version=20,link_version=137,normal_types=88,ordinary_uniques=72,added_normals=42,added_uniques=68,runtime_ids=242,spell_gifs=27,spell_demo_rom_sha256=spells['romSha256'],demo_note='Serial input adapter only for simulated2-player spell fixtures. Deliverable ROM has the production cable path.',performance=report['performance'],test_logs={k:sha(V/(k+'-pass.log')) for k in logs},limitations=['One ordinary unique type per floor; one ordinary translation per palette family.','Fifteen source-disabled/out-of-range uniques and Hellfire content not activated.','Bounded GBA AI/hazard/spell adaptations, not all PC actors/missile fragments simultaneously.','Quest prose and Lachdanan death completed; not every PC video/camera presentation.','No physical GBA/cable validation; minimum24FPS not guaranteed across all combat.'])
(R/'generated/stage71-release-report.json').write_text(json.dumps(summary,indent=2)+'\n')
# Record exact current code/tool inputs for reproducible source review.
inputs={str(p.relative_to(R)):sha(p) for folder in ['src','tools','tests'] for p in sorted((R/folder).rglob('*')) if p.is_file() and p.suffix in ('.c','.h','.inc','.py','.mjs','.s','.ld')}
inputs['build.py']=sha(R/'build.py');(V/'source-hashes.json').write_text(json.dumps(inputs,indent=2)+'\n')
# A frame with the largest positive luminance change makes short effects legible.
contact=Image.new('RGB',(1440,264*7),(16,20,27));draw=ImageDraw.Draw(contact)
for index,row in enumerate(spells['spells']):
 im=Image.open(V/'spells'/row['filename']);frames=[];base=None;best=(0,0)
 for f in range(im.n_frames):
  im.seek(f);frame=im.convert('RGB');arr=np.asarray(frame)[45:296,12:468].astype(np.int16)
  if base is None:base=arr
  score=np.maximum(arr-base,0).sum()
  if f>=8 and score>best[0]:best=(score,f)
 im.seek(best[1]);contact.paste(im.convert('RGB').resize((360,264),Image.Resampling.NEAREST),((index%4)*360,(index//4)*264));row['preview_frame']=best[1]
contact.save(O/'stage71-spell-preview.png');shutil.copy2(V/'spells/18-Nova.gif',O/'stage71-nova.gif');shutil.copy2(R/'build/diablo-gba-assets.gba',O/'diablo-gba-stage71.gba')
readme='''Stage71 마법27종 시연\n\n각 GIF는 실제 mGBA 롬 실행 화면입니다.\n고정된 고체력 표적을 배치하여 효과를 볼 수 있게 했습니다.\n부활/타인 치유는 직렬 입력만 대체한 로컬2인 테스트이며 실물 케이블 검사는 아닙니다.\n공격은 실제 HP 감소, 회복/상태/이동은 실제 상태 변화로 검사했습니다.\nreport.json에는 각 시연의 전후 값과 테스트 롬 해시가 있습니다.\nspell-source-stage71.json은 원본 spelldat의27종×9항목 비교 결과입니다.\n노바는16갈래 타원, 원본 직접 축소 그래픽입니다.\n롤백 저장과도 호환되지 않는 새 저장 형식이므로 배포 롬은 새 캠페인으로 시작하세요.\n\n전체 변경과 제한은 함께 제공된 COMPLETION-STAGE71.md를 참고하세요.\n'''
with zipfile.ZipFile(O/'stage71-spells-27-gifs.zip','w',zipfile.ZIP_DEFLATED,6) as z:
 for row in spells['spells']:z.write(V/'spells'/row['filename'],row['filename'])
 z.writestr('README.txt',readme);z.write(V/'spells/report.json','report.json');z.write(R/'generated/spell-source-stage71.json','spell-source-stage71.json');z.write(R/'COMPLETION-STAGE71.md','COMPLETION-STAGE71.md');z.write(O/'stage71-spell-preview.png','preview.png')
# Preserve all previous source entries, updating them from this working tree.
old=R.parent.parent/'stage70-delivery/dungeon-gba-post086-fixes-source.zip'
with zipfile.ZipFile(old) as z:paths={Path(n).relative_to('dungeon-gba') for n in z.namelist() if not n.endswith('/')}
for dirname in ('src','tools','tests','source_assets'):
 paths.update(p.relative_to(R) for p in (R/dirname).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix not in ('.pyc','.o'))
paths.update(p.relative_to(R) for p in (R/'generated').iterdir() if p.is_file())
paths.update(p.relative_to(R) for p in R.iterdir() if p.is_file())
for folder in ('native-final','spells'):
 paths.update(p.relative_to(R) for p in (V/folder).iterdir() if p.is_file() and p.suffix in ('.png','.gif','.json','.c'))
paths.update(p.relative_to(R) for p in V.iterdir() if p.is_file() and p.suffix in ('.json','.log'))
assert all((R/p).is_file() for p in paths)
with zipfile.ZipFile(O/'dungeon-gba-post086-fixes-source.zip','w',zipfile.ZIP_DEFLATED,6) as z:
 for p in sorted(paths):z.write(R/p,str(Path('dungeon-gba')/p))
with zipfile.ZipFile(O/'dungeon-gba-post086-fixes-source.zip') as z:
 assert z.testzip() is None;assert hashlib.sha256(z.read('dungeon-gba/build/diablo-gba-assets.gba')).hexdigest()==build['sha256']
with zipfile.ZipFile(O/'stage71-spells-27-gifs.zip') as z:assert z.testzip() is None and sum(n.endswith('.gif') for n in z.namelist())==27
files={p.name:{'bytes':p.stat().st_size,'sha256':sha(p)} for p in O.iterdir() if p.is_file()}
(O/'delivery-manifest.json').write_text(json.dumps(files,indent=2)+'\n');print(json.dumps(dict(summary=summary,files=files),indent=2))
