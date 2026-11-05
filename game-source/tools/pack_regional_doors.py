"""Import native Catacombs/Caves door poses without rewriting shared assets.

Usage: python tools/pack_regional_doors.py DIABDAT.MPQ
       python tools/pack_regional_doors.py --check

Retail Objects/L2Doors.CEL and L3Doors.CEL each contain four width-64 CEL
frames: left closed, right closed, left open, right open. OperateL2/L3*Door
adds/subtracts two from the one-based initial animation frame 1 or 2.
The ordinary prop renderer uses 64x64 row-major palette indices. Native
frames are half-sized, centered horizontally, and bottom aligned exactly
as in pack_butcher.py. Regional colors use the same 50% RGB-to-L1 palette
mapping as pack_world_terrain.py / the ordinary caves_lut in water_quest.h.
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
from lz77 import indexed


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'generated'
REPORT = OUT / 'door_frames-report.json'
BASE = 97
FRAME_BYTES = 4096
STATES = ['left_closed', 'right_closed', 'left_open', 'right_open']


def sha(data):
    return hashlib.sha256(data).hexdigest()


def decode_cel(raw):
    count = struct.unpack_from('<I', raw)[0]
    assert count == 4
    offsets = struct.unpack_from('<5I', raw, 4)
    assert offsets[0] >= 24 and offsets[-1] == len(raw)
    result = []
    for lo, hi in zip(offsets, offsets[1:]):
        assert lo < hi <= len(raw)
        frame = raw[lo:hi]
        pos = struct.unpack_from('<H', frame)[0]
        assert 2 <= pos <= len(frame)
        pixels = bytearray()
        while pos < len(frame):
            run = frame[pos]
            pos += 1
            if run >= 128:
                pixels.extend(bytes(256 - run))
            else:
                assert run and pos + run <= len(frame)
                pixels.extend(frame[pos:pos + run])
                pos += run
        assert len(pixels) == 64 * 128
        result.append(Image.frombytes('P', (64, 128), bytes(pixels)).transpose(
            Image.Transpose.FLIP_TOP_BOTTOM))
    return result


def palette_lut(region):
    base = (ROOT / 'source_assets/worldgen/L1_1.PAL').read_bytes()
    native = (ROOT / f'source_assets/worldgen/L{region}_1.PAL').read_bytes()
    assert len(base) == len(native) == 768
    rgb = [tuple(base[i:i + 3]) for i in range(0, 768, 3)]
    colors = [tuple(native[i:i + 3]) for i in range(0, 768, 3)]
    lut = [0] + [min(range(1, 256), key=lambda j: sum(
        (rgb[j][k] - colors[i][k] * .50) ** 2 for k in range(3)))
        for i in range(1, 256)]
    if region == 3:
        text = (OUT / 'water_quest.h').read_text()
        existing = re.search(r'caves_lut\[256\]=\{([^}]+)\}', text)[1]
        assert list(map(int, existing.split(','))) == lut
    return lut, sha(native)


def source_frames(region):
    source = ROOT / f'source_assets/objects/L{region}Doors.CEL'
    raw = source.read_bytes()
    lut, palette_hash = palette_lut(region)
    frames = []
    for native in decode_cel(raw):
        scaled = native.resize((32, 64), Image.Resampling.NEAREST)
        canvas = Image.new('P', (64, 64))
        canvas.paste(scaled, (16, 0))
        mapped = canvas.point(lut)
        assert all(bool(a) == bool(b) for a, b in zip(
            canvas.tobytes(), mapped.tobytes())), 'palette mapping changed opacity'
        frames.append(mapped.tobytes())
    return frames, {
        'region': region, 'source_file': str(source.relative_to(ROOT)),
        'source_sha256': sha(raw), 'source_bytes': len(raw),
        'native_dimensions': [64, 128], 'canvas_dimensions': [64, 64],
        'scaled_dimensions': [32, 64], 'canvas_offset': [16, 0],
        'native_frame_order': STATES, 'native_palette_sha256': palette_hash,
        'palette_lut_sha256': sha(bytes(lut)), 'opacity_preserved': True,
        'decoded_sha256': sha(b''.join(frames)),
        'frame_sha256': [sha(frame) for frame in frames],
    }


def validate(blob, frames):
    """Round-trip every frame through the shipped C indexed LZ decoder."""
    with tempfile.TemporaryDirectory(prefix='regional-door-check-') as temp:
        shared = Path(temp) / 'lz.so'
        subprocess.run(['cc', '-O2', '-shared', '-fPIC',
                        str(ROOT / 'src/lz.c'), '-o', str(shared)], check=True)
        decoder = ctypes.CDLL(str(shared))
        decoder.lz_block.argtypes = [ctypes.c_void_p, ctypes.c_uint,
                                    ctypes.c_void_p, ctypes.c_uint]
        decoder.lz_block.restype = ctypes.c_uint
        source = ctypes.create_string_buffer(blob)
        for index, expected in enumerate(frames):
            output = ctypes.create_string_buffer(FRAME_BYTES)
            assert decoder.lz_block(source, index, output, FRAME_BYTES) == FRAME_BYTES
            assert output.raw == expected, f'frame {index} pixel mismatch'
        assert decoder.lz_block(source, len(frames), output, FRAME_BYTES) == 0


def create_report(frames, records, blob):
    return {
        'source': 'retail DIABDAT.MPQ',
        'source_revision': '9f01757243412bb154d02d9af65c804159e6e0ea',
        'native_entry': 'OperateL2LDoor/RDoor, OperateL3LDoor/RDoor',
        'regions': records, 'virtual_base': BASE, 'frames': len(frames),
        'bytes_per_frame': FRAME_BYTES, 'codec': 'independent indexed LZ',
        'bytes': len(blob), 'aligned_bytes': (len(blob) + 3) & ~3,
        'sha256': sha(blob), 'decoded_sha256': sha(b''.join(frames)),
        'base_palette_sha256': sha((ROOT / 'source_assets/worldgen/L1_1.PAL').read_bytes()),
        'existing_prop_frames_sha256': sha((OUT / 'prop_frames.bin').read_bytes()),
        'color_conversion': 'native regional RGB * 0.50 to nearest nonzero L1 palette index',
        'existing_assets_changed': False, 'c_decoder_checked_frames': len(frames),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mpq', type=Path, nargs='?')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    assert len((OUT / 'prop_frames.bin').read_bytes()) == BASE * FRAME_BYTES
    definitions = (OUT / 'packed_assets.h').read_text()
    declared_bytes = int(re.search(r'#define RAW_PROP_FRAMES (\d+)', definitions)[1])
    assert declared_bytes == BASE * FRAME_BYTES, 'stale shared prop-frame byte count'
    if not args.check:
        if not args.mpq:
            parser.error('DIABDAT.MPQ is required unless --check is used')
        import mpyq
        from extract_assets import read_mpq
        archive = mpyq.MPQArchive(str(args.mpq), listfile=False)
        for region in (2, 3):
            native = read_mpq(archive, f'Objects\\L{region}Doors.CEL')
            (ROOT / f'source_assets/objects/L{region}Doors.CEL').write_bytes(native)
            assert read_mpq(archive, f'Levels\\L{region}Data\\L{region}_1.PAL') == (
                ROOT / f'source_assets/worldgen/L{region}_1.PAL').read_bytes()
    frames, records = [], []
    for region in (2, 3):
        group, record = source_frames(region)
        frames.extend(group)
        records.append(record)
    blob = indexed(b''.join(frames), FRAME_BYTES)
    validate(blob, frames)
    assert len(blob) < 16 * 1024, 'door assets exceeded 16KiB budget'
    report = create_report(frames, records, blob)
    header = '\n'.join([
        '/* Generated by tools/pack_regional_doors.py. */', '#pragma once',
        '#include <stdint.h>', f'#define PROP_REGIONAL_DOORS_BASE {BASE}',
        f'#define PROP_REGIONAL_DOORS_FRAMES {len(frames)}',
        f'#define PROP_L2DOORS_BASE {BASE}', f'#define PROP_L3DOORS_BASE {BASE + 4}',
        'extern const uint8_t asset_door_frames[];', '',
    ])
    if args.check:
        assert (OUT / 'door_frames.lz').read_bytes() == blob
        assert (OUT / 'door_frames.h').read_text() == header
        assert json.loads(REPORT.read_text()) == report
    else:
        (OUT / 'door_frames.lz').write_bytes(blob)
        (OUT / 'door_frames.h').write_text(header)
        REPORT.write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(frames)} native regional door frames; {len(blob)} bytes; '
          'native CEL pixels and C decoder verified')


if __name__ == '__main__':
    main()
