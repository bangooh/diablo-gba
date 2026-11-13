#!/usr/bin/env python3
"""Build a freestanding GBA ROM with Zig 0.13 (bundled Clang/LLD).
Usage: python build.py --zig /path/to/zig
No devkitPro, SDL or original Diablo data required for this hardware prototype.
"""
import argparse, json, pathlib, shutil, struct, subprocess, hashlib, sys
ROOT=pathlib.Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument('--zig',default=shutil.which('zig') or 'zig')
parser.add_argument('--reuse-assets',action='store_true',help='Developer rebuild: use already generated asset files')
parser.add_argument('--graphics',action='store_true',help='Build the MPQ asset / Mode 0 version')
parser.add_argument('--spell-lighting',choices=('on','off'),default='off',help='Transient magic illumination; spell artwork and damage are unchanged')
parser.add_argument('--demo-input',action='store_true',help='TEST ONLY: local co-op input fixture without serial cable')
args=parser.parse_args()
if args.graphics:
    subprocess.run([sys.executable,str(ROOT/'tools/pack_town_dialogue_font.py')],check=True)
    import sys
    subprocess.run([sys.executable,str(ROOT/'tools/pack_town_minimap_stage80.py')],check=True)
if args.graphics and not args.reuse_assets:
    import sys
    subprocess.run([sys.executable, str(ROOT/'tools/pack_cast_frames.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_cast_families_stage69.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_spell_geometry_stage73.py'),'--check'], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/import_item_catalog.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_bg_water_palette.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_door_arches.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_fixed_l1_stage76.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_environment_stage61.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_environment_stage64.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_theme_rules_stage64.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_ground_items.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_monsters_stage66.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_monsters_stage71.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_quest_prose_stage71.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_difficulty.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_item_specials.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_floor_masks.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_exocet_font.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_npc_cursor_stage62.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_spell_icons.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_belt.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_outer_mask.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/make_prop_dirty.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_quest_objects_stage71.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_environment_stage76.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_prop_atlas.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_loading_town.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_audio_gains.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'tools/pack_audio_stage66.py')], check=True)
if args.graphics:
    import sys
    subprocess.run([sys.executable,str(ROOT/'tools/pack_frontend_stage81.py')],check=True)
    subprocess.run([sys.executable,str(ROOT/'tools/pack_town_minimap_stage80.py')],check=True)
if args.graphics and not args.reuse_assets:
    subprocess.run([sys.executable, str(ROOT/'tools/pack_music_regions_stage71.py')], check=True)
if args.graphics:subprocess.run([sys.executable,str(ROOT/'tools/pack_fixed_wall_art.py')],check=True)
if args.graphics: subprocess.run([sys.executable,str(ROOT/'tools/pack_death_red.py')],check=True)
build=ROOT/'build'; build.mkdir(exist_ok=True)
# Zig0.13 can reuse an object after an included .inc file changes. Bind every
# compiler invocation to the actual source/header/embedded-asset bytes, so a
# warm cache remains safe without requiring callers to delete their cache.
fingerprint=hashlib.sha256()
inputs=sorted(p for p in (ROOT/'src').rglob('*') if p.is_file())+[ROOT/'link.ld',ROOT/'build.py']
if args.graphics:
    inputs+=sorted(p for p in (ROOT/'generated').iterdir() if p.suffix in ('.h','.inc','.lz','.rle','.bin'))
for p in inputs:
    fingerprint.update(str(p.relative_to(ROOT)).encode()+b'\0')
    fingerprint.update(hashlib.sha256(p.read_bytes()).digest())
flags=['-target','arm-freestanding-eabi','-mcpu=arm7tdmi','-marm','-O2','-g',
       '-ffreestanding','-fno-strict-aliasing','-fno-builtin','-fno-stack-protector',
       '-fno-unwind-tables','-fno-asynchronous-unwind-tables',
       '-Wall','-Wextra','-Werror','-Wno-unused-command-line-argument']
flags.append('-DBUILD_INPUT_SHA256="'+fingerprint.hexdigest()+'"')
if args.demo_input:flags.append('-DSTAGE71_DEMO_INPUT=1')
flags.append('-DSPELL_LIGHTING='+('1' if args.spell_lighting=='on' else '0'))
objects=[]
sources=['start.s','game.c','controller.c','arrows.c','progress.c','spells.c','items.c','shop.c','quests.c','monster_ai.c','objects.c','exploration.c','lz.c','actor_frames.c','flash.c','link.c','coop.c','coop_wire.c','dungeon.c','dungeon_visual.c','world.c','worldgen/runtime.c','worldgen/l2.c','worldgen/l3.c','worldgen/l4.c','potions.c','audio.c','gba_mode0.c'] if args.graphics else ['start.s','game.c','render.c','gba.c']
if args.graphics:flags.append('-DGRAPHICS_ASSETS')
for name in sources:
    obj=build/(pathlib.Path(name).stem+'.o')
    # Generation/terrain construction and shop transactions are cold paths.
    # Thumb size optimization frees ROM for native theme artwork. The hot
    # IWRAM renderer retains ARM/O2. Zig0.13 needs the Thumb target triple.
    source_flags=flags
    thumb=None
    if args.graphics and name in ('dungeon_visual.c','world.c','shop.c','dungeon.c','worldgen/l2.c','worldgen/l3.c','worldgen/l4.c'):
        thumb='-Oz'
    # ROM-resident simulation code is fetched over the 16-bit cartridge bus, where
    # a Thumb opcode costs one access and an ARM opcode two. These objects have no
    # IWRAM sections, so Thumb/O2 reduces opcode-fetch width and code size. Zig 0.13's
    # __aeabi_uldivmod/__aeabi_ldivmod return with POP {pc}, which cannot return to
    # a Thumb caller on ARMv4T; reject any Thumb object that references them.
    elif args.graphics and name in ('monster_ai.c','objects.c','spells.c','arrows.c','controller.c','items.c','progress.c','potions.c','exploration.c'):
        thumb='-O2'
    if thumb:
        replace={'arm-freestanding-eabi':'thumb-freestanding-eabi','-marm':'-mthumb','-O2':thumb}
        source_flags=[replace.get(flag,flag) for flag in flags]
    subprocess.run([args.zig,'cc',*source_flags,*(['-Wno-logical-op-parentheses','-Wno-switch-bool'] if name.startswith('worldgen/') else []),'-c',str(ROOT/'src'/name),'-o',str(obj)],check=True)
    if thumb:
        code=obj.read_bytes()
        assert b'__aeabi_uldivmod' not in code and b'__aeabi_ldivmod' not in code, name+': 64-bit division in a Thumb object'
    objects.append(str(obj))
if args.graphics:
    from tools.asset_bundle import emit_assets, referenced_assets
    required_assets=referenced_assets(objects)
    print('Asset bundle:', emit_assets(ROOT/'generated', ROOT/'generated/assets.s', required_assets))
    obj=build/'assets.o'
    subprocess.run([args.zig,'cc',*flags,'-c',str(ROOT/'generated/assets.s'),'-o',str(obj)],check=True)
    objects.append(str(obj))
prefix='diablo-gba-assets' if args.graphics else 'dungeon-lab'
elf=build/(prefix+'.elf')
subprocess.run([args.zig,'cc',*flags,'-nostdlib',*objects,
               '-Wl,-T,'+str(ROOT/'link.ld'),
               '-o',str(elf)],check=True)
data=elf.read_bytes()
assert data[:7]==b'\x7fELF\x01\x01\x01', 'Expected ELF32 little-endian'
assert struct.unpack_from('<H',data,18)[0]==40, 'Expected ARM'
phoff=struct.unpack_from('<I',data,28)[0]
phsize,phcount=struct.unpack_from('<HH',data,42)
rom=bytearray()
ram_used=0
fast_used=0
for i in range(phcount):
    kind,off,vaddr,paddr,filesz,memsz,flags_,align=struct.unpack_from('<8I',data,phoff+i*phsize)
    if kind!=1: continue
    if 0x02000000<=vaddr<0x02040000: ram_used+=memsz
    if 0x03000000<=vaddr<0x03008000: fast_used+=memsz
    if filesz and 0x08000000<=paddr<0x0A000000:
        dst=paddr-0x08000000
        if len(rom)<dst+filesz: rom.extend(bytes(dst+filesz-len(rom)))
        rom[dst:dst+filesz]=data[off:off+filesz]
if args.graphics:
    for blob in sorted(p for p in (ROOT/'generated').iterdir() if p.suffix in ('.lz','.rle')):
        if 'asset_'+blob.stem not in required_assets:continue
        assert blob.read_bytes() in rom, f'Stale or missing embedded asset: {blob.name}'
# Standard boot header logo required by the GBA BIOS; not game artwork.
logo=bytes.fromhex('24ffae51699aa2213d84820a84e409ad11248b98c0817f21a352be199309ce20'
 '10464a4af82731ec58c7e83382e3cebf85f4df94ce4b09c194568ac01372a7fc'
 '9f844d73a3ca9a615897a327fc039876231dc7610304ae56bf38840040a70efd'
 'ff52fe036f9530f197fbc08560d68025a963be03014e38e2f9a234ffbb3e0344'
 '780090cb88113a9465c07c6387f03cafd625e48b380aac7221d4f807')
assert len(logo)==156
rom[4:160]=logo
rom[160:172]=b'DIABLO LAB  ' if args.graphics else b'DUNGEON LAB '
rom[172:176]=b'DLAB';rom[176:178]=b'00';rom[178]=0x96
rom[179:189]=bytes(10)
rom[189]=(-sum(rom[160:189])-0x19)&255
rom[190:192]=bytes(2)
assert (sum(rom[160:190])+0x19)&255==0
assert len(rom)<=32*1024*1024
assert ram_used<256*1024
assert fast_used<24*1024 # Reserve at least ~8KiB for nested gameplay/co-op/render stacks.
# Power-of-two size is friendly to flash carts and emulators.
size=32768
while size<len(rom): size*=2
rom.extend(b'\xff'*(size-len(rom)))
# Replace the inode atomically: emulators can retain an mmap of the old ROM.
output=build/(prefix+'.gba'); pending=output.with_suffix('.gba.tmp')
pending.write_bytes(rom); pending.replace(output)
report={'rom_bytes':len(rom),'ewram_static_bytes':ram_used,'iwram_code_bytes':fast_used,
        'spell_lighting':args.spell_lighting,
        'sha256':hashlib.sha256(rom).hexdigest(),
        'gba_header_checksum':'PASS','arm_elf':'PASS',
        'note':'Static allocation only; IWRAM stack and hardware timing require separate validation.'}
(build/(prefix+'-build-report.json' if args.graphics else 'build-report.json')).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
