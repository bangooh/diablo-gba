"""Import native static Traphole.CEL orientations and wall eligibility bits.

Usage: python3 tools/pack_wall_traps.py DIABDAT.MPQ
       python3 tools/pack_wall_traps.py --check

OBJ_TRAPL/OBJ_TRAPR share Objects\\Traphole.CEL frames1/2. They are visible,
unselectable static objects; firing/disarming never changes their picture.
Native64x144 becomes32x72, bottom-aligned in the existing64x64 prop canvas.
Only transparent top rows are clipped. CEL literal zero stays opaque black.
"""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import re
import struct
import subprocess
import tempfile

from PIL import Image
from lz77 import indexed, decompress

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'generated'
SOURCE = OUT / 'wall_traps-source.cel'
BASE, COUNT, FRAME_BYTES = 115, 2, 4096


def sha(data):
    return hashlib.sha256(data).hexdigest()


def frames(raw):
    assert struct.unpack_from('<I', raw)[0] == COUNT
    offsets = struct.unpack_from('<3I', raw, 4)
    assert offsets[0] == 16 and offsets[-1] == len(raw)
    result = []
    for lo, hi in zip(offsets, offsets[1:]):
        assert lo < hi
        frame = raw[lo:hi]
        pos = struct.unpack_from('<H', frame)[0]
        assert pos == 10
        pixels, alpha = bytearray(), bytearray()
        while pos < len(frame):
            length = frame[pos]
            pos += 1
            assert length
            if length >= 128:
                pixels.extend(bytes(256-length))
                alpha.extend(bytes(256-length))
            else:
                assert pos+length <= len(frame)
                pixels.extend(frame[pos:pos+length])
                alpha.extend([255]*length)
                pos += length
        assert len(pixels) == len(alpha) == 64*144
        result.append((Image.frombytes('P',(64,144),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM),
                       Image.frombytes('L',(64,144),bytes(alpha)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)))
    return result


def native_rules():
    data=(ROOT/'source_assets/item_catalog/objdat.cpp').read_text()
    records=[]
    for line in data.splitlines():
        if re.match(r'\s*\{\s*1,\s*OFILE_TRAPHOLE,',line):
            records.append([f.strip() for f in line[line.index('{')+1:line.rindex('}')].split(',')])
    assert len(records)==2
    for direction,row in enumerate(records):
        assert len(row)==17
        assert row[7:11]==['0',str(direction+1),'0','64']
        assert row[11:17]==['FALSE','TRUE','TRUE','0','0','FALSE']
    assert '"Traphole"' in data
    code=(ROOT/'source_assets/item_catalog/objects.cpp').read_text()
    setup=code.split('void SetupObject(',1)[1].split('\nvoid ',1)[0]
    add=code.split('void AddTrap(',1)[1].split('\nvoid ',1)[0]
    trigger=code.split('void Obj_Trap(',1)[1].split('\nvoid ',1)[0]
    assert 'object[i]._oPreFlag = FALSE;' in setup
    assert 'object[i]._oAnimFrame = AllObjects[ot].oAnimDelay;' in setup
    assert '_oAnim' not in add and '_oAnim' not in trigger
    assert 'object[i]._oVar4 = 1;' in trigger
    return {'selection_flag':0,'animation_flag':0,'animation_length':0,
            'pre_draw_flag':False,'solid_flag':False,'missile_flag':True,
            'light_flag':True,'native_frames':[1,2],'changes_after_firing':False}


def validate(blob, expected):
    with tempfile.TemporaryDirectory(prefix='wall-trap-check-') as temp:
        library=Path(temp)/'lz.so'
        subprocess.run(['gcc','-O2','-shared','-fPIC',str(ROOT/'src/lz.c'),'-o',str(library)],check=True)
        decoder=ctypes.CDLL(str(library))
        decoder.lz_block.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint]
        decoder.lz_block.restype=ctypes.c_uint
        source=ctypes.create_string_buffer(blob)
        guarded=(ctypes.c_uint32*1026)()
        for n,pixels in enumerate(expected):
            guarded[0]=guarded[1025]=0xA5A5A5A5
            assert decoder.lz_block(source,n,ctypes.byref(guarded,4),FRAME_BYTES)==FRAME_BYTES
            assert guarded[0]==guarded[1025]==0xA5A5A5A5
            assert ctypes.string_at(ctypes.byref(guarded,4),FRAME_BYTES)==pixels
            before=bytes(guarded)
            assert decoder.lz_block(source,n,ctypes.byref(guarded,4),FRAME_BYTES-1)==0
            assert bytes(guarded)==before
        before=bytes(guarded)
        assert decoder.lz_block(source,COUNT,ctypes.byref(guarded,4),FRAME_BYTES)==0
        assert bytes(guarded)==before


def wall_flags():
    arrays, report={},{}
    for region,length,count in (('L1',453,40),('L2',559,67),('L3',560,0)):
        path=ROOT/'source_assets/worldgen'/f'{region}.SOL'
        raw=path.read_bytes()
        assert len(raw)==length and sum(bool(v&128) for v in raw)==count
        flags=bytearray((len(raw)+7)//8)
        for i,value in enumerate(raw):
            if value&128:flags[i//8]|=1<<(i%8)
        assert all(bool(flags[i//8]&(1<<(i%8)))==bool(value&128) for i,value in enumerate(raw))
        assert not any((flags[i//8]>>(i%8))&1 for i in range(length,len(flags)*8))
        if region!='L3':arrays[region.lower()]=bytes(flags)
        report[region]={'source':str(path.relative_to(ROOT)),'source_sha256':sha(raw),
                        'pieces':length,'eligible_pieces':count,'bitset_bytes':len(flags) if count else 0}
    return arrays,report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mpq',type=Path,nargs='?')
    parser.add_argument('--check',action='store_true')
    args=parser.parse_args()
    rules=native_rules()
    if not args.check:
        if not args.mpq:parser.error('DIABDAT.MPQ is required unless --check is used')
        import mpyq
        from extract_assets import read_mpq
        SOURCE.write_bytes(read_mpq(mpyq.MPQArchive(str(args.mpq),listfile=False),'Objects\\Traphole.CEL'))
    raw=SOURCE.read_bytes()
    previous=(OUT/'explosive_barrels.h').read_text()
    assert BASE==int(re.search(r'#define PROP_BARRELEX_LAST (\d+)',previous)[1])+1
    assert BASE+COUNT-1<=126
    native_palette=(ROOT/'source_assets/worldgen/L1_1.PAL').read_bytes()
    native555=tuple((native_palette[i]>>3)|((native_palette[i+1]>>3)<<5)|((native_palette[i+2]>>3)<<10) for i in range(0,768,3))
    palette=struct.unpack('<256H',decompress((OUT/'bg_palette.lz').read_bytes()))
    assert native555==palette
    black=next(i for i in range(1,256) if palette[i]==palette[0]==0)
    assert black==63
    packed,records=[],[]
    for i,(native,alpha) in enumerate(frames(raw)):
        image=native.resize((32,72),Image.Resampling.NEAREST)
        mask=alpha.resize((32,72),Image.Resampling.NEAREST)
        pixels,opaque=image.tobytes(),mask.tobytes()
        assert not mask.crop((0,0,32,8)).getbbox(), 'canvas would clip visible pixels'
        mapped=bytes((p if p else black) if a else 0 for p,a in zip(pixels,opaque))
        assert all(bool(p)==bool(a) for p,a in zip(mapped,opaque))
        assert all(not a or palette[m]==native555[p] for m,p,a in zip(mapped,pixels,opaque))
        canvas=Image.new('P',(64,64))
        canvas.paste(Image.frombytes('P',(32,72),mapped),(16,-8))
        expected_mask=Image.new('L',(64,64));expected_mask.paste(mask,(16,-8))
        output=canvas.tobytes()
        assert sum(bool(p) for p in output)==sum(bool(a) for a in opaque)
        assert all(bool(p)==bool(a) for p,a in zip(output,expected_mask.tobytes()))
        packed.append(output)
        records.append({'native_frame':i+1,'orientation':'left' if i==0 else 'right',
                        'virtual_frame':BASE+i,'sha256':sha(output),
                        'native_bbox':alpha.getbbox(),'canvas_bbox':expected_mask.getbbox(),
                        'native_opaque_pixels':sum(bool(a) for a in alpha.tobytes()),
                        'opaque_pixels':sum(bool(a) for a in opaque),
                        'opaque_black_pixels':sum(p==0 and bool(a) for p,a in zip(pixels,opaque))})
    blob=indexed(b''.join(packed),FRAME_BYTES)
    assert len(blob)<=1536
    validate(blob,packed)
    arrays,flag_report=wall_flags()
    header='''/* Generated by tools/pack_wall_traps.py. Static native Traphole1/2. */
#pragma once
#include <stdint.h>
#define PROP_WALL_TRAP_BASE 115
#define PROP_WALL_TRAP_LEFT 115
#define PROP_WALL_TRAP_RIGHT 116
#define PROP_WALL_TRAP_FRAMES 2
#define PROP_WALL_TRAP_FRAME_LAST 116
#define WALL_TRAP_L1_PIECES 453
#define WALL_TRAP_L2_PIECES 559
extern const uint8_t asset_wall_traps[];
/* Zero-based native WorldArt piece index; SOL bit0x80, low bit first. */
extern const uint8_t native_wall_trap_flags_l1[57];
extern const uint8_t native_wall_trap_flags_l2[70];
'''
    flags=['/* Generated by tools/pack_wall_traps.py. Include once. */','#include "wall_traps.h"']
    for region,data in arrays.items():
        flags.append(f'const uint8_t native_wall_trap_flags_{region}[{len(data)}]={{')
        flags += [' '+','.join(f'0x{b:02x}' for b in data[i:i+16])+',' for i in range(0,len(data),16)]
        flags.append('};')
    flags.append('')
    report={'source':'Objects\\Traphole.CEL','source_file':str(SOURCE.relative_to(ROOT)),
            'source_bytes':len(raw),'source_sha256':sha(raw),'source_revision':'9f01757243412bb154d02d9af65c804159e6e0ea',
            'native_rules':rules,'native_dimensions':[64,144],'scaled_dimensions':[32,72],
            'canvas_dimensions':[64,64],'canvas_offset':[16,-8],'visible_pixels_clipped':0,
            'opaque_black_palette_index':black,'virtual_base':BASE,'virtual_last':BASE+COUNT-1,
            'codec':'independent indexed LZ','bytes':len(blob),'aligned_bytes':(len(blob)+3)&~3,
            'sha256':sha(blob),'decoded_sha256':sha(b''.join(packed)),'frames':records,
            'wall_flags':flag_report,'wall_flags_bytes':sum(map(len,arrays.values())),
            'combined_aligned_runtime_bytes':((len(blob)+3)&~3)+128,
            'native_color_and_alpha_preserved':True,'c_decoder_checked_frames':COUNT,
            'small_capacity_invalid_index_and_canaries_checked':True,
            'existing_asset_sha256':{n:sha((OUT/n).read_bytes()) for n in
                 ('prop_frames.bin','prop_frames.lz','door_frames.lz','explosive_barrels.lz','bg_palette.lz')},
            'existing_assets_changed':False,'additional_runtime_ram':0}
    outputs={'wall_traps.h':header.encode(),'wall_traps.lz':blob,
             'wall_trap_flags.inc':'\n'.join(flags).encode(),
             'wall_traps-report.json':(json.dumps(report,indent=2)+'\n').encode()}
    for name,data in outputs.items():
        path=OUT/name
        if args.check:assert path.read_bytes()==data,name+' differs'
        else:path.write_bytes(data)
    print(f'PASS 2 native static wall trap frames, {len(blob)} compressed bytes +127 wall flag bytes; geometry/alpha/source states/C decoder verified')


if __name__=='__main__':
    main()
