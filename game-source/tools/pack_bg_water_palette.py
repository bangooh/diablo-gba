"""Stage42 lossless BG reindexing from immutable Stage41 shipping inputs.

Changes palette references only, not geometry, masks, animation, or local floor
codec bodies. Run after restoring/generating the baseline input directory;
subsequent runs are deterministic because inputs are never overwritten.
"""
from pathlib import Path
import ctypes
import hashlib
import json
import struct
import subprocess
import tempfile

from audit_background_palettes import array
from floor_palette_style import palette_size
from lz77 import compress, decompress, indexed

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'source_assets/bg_stage41'
GEN=ROOT/'generated'
REVIEW=GEN/'background-palette-review'


def main():
    report={'stage':42,'operation':'lossless BG color-index reallocation; no pixel geometry changes',
            'inputs':{},'outputs':{},'static_pixel_checks':0,'floor_palette_bytes':0}
    def read(name):
        b=(SRC/name).read_bytes();report['inputs'][name]=hashlib.sha256(b).hexdigest();return b
    def write(name,b):
        (GEN/name).write_bytes(b);report['outputs'][name]={'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
    old=struct.unpack('<256H',read('bg_palette.bin'))
    mapping=(REVIEW/'static_index_map.bin').read_bytes()
    native=(REVIEW/'l3_native_index_map.bin').read_bytes()
    audit=json.loads((REVIEW/'audit.json').read_text())
    static=audit['palette']['static_slots'];dynamic=audit['palette']['dynamic_slots']
    new=list(struct.unpack('<256H',(REVIEW/'caves.rgb555.bin').read_bytes()))
    assert not(set(static)&set(dynamic)) and len(static)==210 and len(dynamic)==30
    # Two unused static colors keep the procedural mana liquid blue at highlights.
    spare=[i for i in range(1,256) if i not in static and i not in dynamic]
    orb_blue={spare[0]:2|(8<<5)|(26<<10),spare[1]:5|(14<<5)|(31<<10)}
    for slot,color in orb_blue.items():new[slot]=color;static.append(slot)
    static.sort();report['new_orb_blue_RGB555']=orb_blue
    def translate(b):
        for i in set(b):
            assert bool(i)==bool(mapping[i]),('opacity',i)
            assert not i or mapping[i] in static
            assert old[i]==new[mapping[i]],('color',i)
        report['static_pixel_checks']+=len(b)
        return b.translate(mapping)

    for name in ('world_l1.rle','world_l2.rle','world_l4.rle','dungeon_art_raw.rle','floor_variants.rle'):
        write(name,translate(read(name)))
    l3=read('world_l3.rle');converted=l3.translate(native)
    quest=read('water_quest.h').decode()
    caves,foul,clean=[array(quest,n+'_lut') for n in ('caves','water_foul','water_clean')]
    states=[caves,foul]+[clean[:i+1]+foul[i+1:] for i in range(32)]
    palettes=[]
    for state in range(34):
        filename=('caves' if state==0 else 'water_foul' if state==1 else f'water_step_{state-2:02d}')+'.rgb555.bin'
        words=struct.unpack('<256H',(REVIEW/filename).read_bytes());palettes.append(words)
        for i in set(l3):
            assert bool(i)==bool(native[i]) and words[native[i]]==old[states[state][i]]
    report['L3_pixel_state_checks']=len(l3)*34
    write('world_l3.rle',converted)

    with tempfile.TemporaryDirectory(prefix='bg-water-pack-') as temporary:
        shared=Path(temporary)/'lz.so'
        subprocess.run(['cc','-O2','-shared','-fPIC',str(ROOT/'src/lz.c'),'-o',str(shared)],check=True)
        c=ctypes.CDLL(str(shared));c.lz_block.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint]
        buffer=ctypes.create_string_buffer(4096)
        prop_colors=set()
        for name in ('prop_frames','door_frames','explosive_barrels','wall_traps'):
            blob=read(name+'.lz');count=struct.unpack_from('<I',blob)[0];frames=[]
            for frame in range(count):
                assert c.lz_block(blob,frame,buffer,4096)==4096
                prop_colors.update(buffer.raw);frames.append(translate(buffer.raw))
            packed=indexed(b''.join(frames),4096)
            for i,expected in enumerate(frames):
                assert c.lz_block(packed,i,buffer,4096)==4096 and buffer.raw==expected
            write(name+'.lz',packed)
            if name=='prop_frames':write(name+'.bin',b''.join(frames))

    # All references to a shared local palette are transformed exactly once.
    floor=read('floor_decals.rle');changed=bytearray(floor);count=struct.unpack_from('<I',floor)[0]
    offsets=set(struct.unpack_from(f'<{count}I',floor,4));locations=set()
    for off in offsets:
        n=palette_size(floor,off);start=struct.unpack_from('<I',floor,off+4)[0]
        locations.update(range(start,start+n))
    for off in locations:changed[off]=translate(bytes([floor[off]]))[0]
    assert all(a==b or i in locations for i,(a,b) in enumerate(zip(floor,changed)))
    report['floor_palette_bytes']=len(locations);report['floor_frames']=count
    write('floor_decals.rle',bytes(changed))
    highlight=array(read('prop_highlight.h').decode(),'prop_highlight');out=list(range(256))
    seen={}
    for i in prop_colors:
        j=mapping[i];target=mapping[highlight[i]]
        assert not i or target
        if j in seen:assert old[highlight[i]]==old[highlight[seen[j]]]
        seen[j]=i;out[j]=target
    write('prop_highlight.h',('static const uint8_t prop_highlight[256]={'+','.join(map(str,out))+'};\n').encode())

    # Keep intermediate .bin in sync with the exact shipping compressed stream.
    for stem in ('control_panel_dungeon','dungeon_font','dungeon_font_large','dungeon_overlay_font','dungeon_status_frame'):
        raw=decompress(read(stem+'.lz'));raw=translate(raw)
        write(stem+'.lz',compress(raw));write(stem+'.bin',raw)
    write('bg_palette.bin',struct.pack('<256H',*new));write('bg_palette.lz',compress(struct.pack('<256H',*new)))
    mask=bytes(sum(1<<(i&7) for i in static if i//8==j) for j in range(32))
    write('bg_static_slots.bin',mask)
    used=sorted(set(l3)&set(range(1,32)));assert used==list(range(1,31))
    slots=[native[i] for i in used]
    header=['#pragma once','#include <stdint.h>',
            '/* State0=caves,1=foul,2..33=original purification steps0..31. */',
            'static const uint8_t bg_static_slots[32]={'+','.join(map(str,mask))+'};',
            'static const uint8_t bg_water_slots[30]={'+','.join(map(str,slots))+'};',
            'static const uint16_t bg_water_rgb[3][30]={'+','.join('{'+','.join(str(old[lut[i]]) for i in used)+'}' for lut in (caves,foul,clean))+'};',
            'static inline int bg_static_index(unsigned i){return (bg_static_slots[i>>3]>>(i&7))&1;}',
            'static inline uint16_t bg_water_color(unsigned state,unsigned n){',
            ' unsigned row=state==0?0:state==1?1:(n+1<=state-2?2:1);return bg_water_rgb[row][n];}',
            '']
    write('bg_water_palette.h','\n'.join(header).encode())
    for s in range(34):
        for n,slot in enumerate(slots):
            row=0 if s==0 else 1 if s==1 else 2 if n+1<=s-2 else 1
            assert old[(caves,foul,clean)[row][n+1]]==palettes[s][slot]
    report['static_slots']=static;report['dynamic_slots']=dynamic;report['RGB555_and_opacity_mismatches']=0
    (GEN/'stage42-validation').mkdir(exist_ok=True)
    (GEN/'stage42-validation/bg-palette-repack.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('static_pixel_checks','L3_pixel_state_checks','floor_palette_bytes','floor_frames','RGB555_and_opacity_mismatches')},indent=2))


if __name__=='__main__':main()
