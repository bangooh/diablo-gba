"""Import the ten native BarrelEx CEL frames as an independent prop stream.

Usage: python tools/pack_explosive_barrels.py DIABDAT.MPQ
       python tools/pack_explosive_barrels.py --check

Retail BreakBarrel starts at native frame1 with delay1; Obj_StopAnim holds
frame10. Frame1 is intact, frames2..9 show the burst, frame10 is debris.
The existing prop canvas uses half resolution: native96x96 becomes48x48,
centered at(8,16) in64x64. CEL negative skips are transparent; literal zero
is opaque black and is represented by the BG palette's nonzero black entry.
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
SOURCE = OUT / 'explosive_barrels-source.cel'
REPORT = OUT / 'explosive_barrels-report.json'
BASE = 105
COUNT = 10
FRAME_BYTES = 4096


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_frames(raw):
    count = struct.unpack_from('<I', raw)[0]
    assert count == COUNT
    offsets = struct.unpack_from('<' + 'I' * (count + 1), raw, 4)
    assert offsets[0] >= 4 + 4 * (count + 1) and offsets[-1] == len(raw)
    frames = []
    for lo, hi in zip(offsets, offsets[1:]):
        assert lo < hi <= len(raw)
        frame = raw[lo:hi]
        pos = struct.unpack_from('<H', frame)[0]
        assert 2 <= pos <= len(frame)
        pixels, alpha = bytearray(), bytearray()
        while pos < len(frame):
            length = frame[pos]
            pos += 1
            assert length
            if length >= 128:
                pixels.extend(bytes(256 - length))
                alpha.extend(bytes(256 - length))
            else:
                assert pos + length <= len(frame)
                pixels.extend(frame[pos:pos + length])
                alpha.extend([255] * length)
                pos += length
        assert len(pixels) == len(alpha) == 96 * 96
        image = Image.frombytes('P', (96, 96), bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        mask = Image.frombytes('L', (96, 96), bytes(alpha)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        frames.append((image, mask))
    return frames


def validate(blob, frames):
    with tempfile.TemporaryDirectory(prefix='barrelex-check-') as temp:
        shared = Path(temp) / 'lz.so'
        subprocess.run(['gcc', '-O2', '-shared', '-fPIC', str(ROOT / 'src/lz.c'),
                        '-o', str(shared)], check=True)
        decoder = ctypes.CDLL(str(shared))
        decoder.lz_block.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_uint]
        decoder.lz_block.restype = ctypes.c_uint
        source = ctypes.create_string_buffer(blob)
        guarded = (ctypes.c_uint32 * 1026)()
        for index, expected in enumerate(frames):
            guarded[0] = guarded[1025] = 0xA5A5A5A5
            assert decoder.lz_block(source, index, ctypes.byref(guarded, 4), FRAME_BYTES) == FRAME_BYTES
            assert guarded[0] == guarded[1025] == 0xA5A5A5A5
            assert ctypes.string_at(ctypes.byref(guarded, 4), FRAME_BYTES) == expected
            before = bytes(guarded)
            assert decoder.lz_block(source, index, ctypes.byref(guarded, 4), FRAME_BYTES - 1) == 0
            assert bytes(guarded) == before, 'undersized destination was modified'
        before = bytes(guarded)
        assert decoder.lz_block(source, COUNT, ctypes.byref(guarded, 4), FRAME_BYTES) == 0
        assert bytes(guarded) == before, 'invalid frame modified destination'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mpq', type=Path, nargs='?')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if not args.check:
        if not args.mpq:
            parser.error('DIABDAT.MPQ is required unless --check is used')
        import mpyq
        from extract_assets import read_mpq
        archive = mpyq.MPQArchive(str(args.mpq), listfile=False)
        SOURCE.write_bytes(read_mpq(archive, 'Objects\\Barrelex.CEL'))
    raw = SOURCE.read_bytes()
    definitions = (OUT / 'door_frames.h').read_text()
    def macro(name):
        return int(re.search(r'#define ' + name + r' (\d+)', definitions)[1])
    assert BASE == macro('PROP_REGIONAL_DOORS_BASE') + macro('PROP_REGIONAL_DOORS_FRAMES')
    assert BASE + COUNT - 1 <= 126, 'prop cache uses signed eight-bit frame keys'
    native_palette = (ROOT / 'source_assets/worldgen/L1_1.PAL').read_bytes()
    hardware_palette = struct.unpack('<256H', decompress((OUT / 'bg_palette.lz').read_bytes()))
    native_rgb555 = tuple((native_palette[i] >> 3) | (native_palette[i + 1] >> 3) << 5 |
                          (native_palette[i + 2] >> 3) << 10 for i in range(0, 768, 3))
    assert hardware_palette == native_rgb555, 'prop colors need their original common BG palette'
    black = next(i for i in range(1, 256) if hardware_palette[i] == hardware_palette[0] == 0)
    frames, records = [], []
    for index, (native, alpha) in enumerate(read_frames(raw)):
        native = native.resize((48, 48), Image.Resampling.NEAREST)
        alpha = alpha.resize((48, 48), Image.Resampling.NEAREST)
        pixels, mask = native.tobytes(), alpha.tobytes()
        mapped = bytes((pixel if pixel else black) if opaque else 0 for pixel, opaque in zip(pixels, mask))
        assert all(bool(pixel) == bool(opaque) for pixel, opaque in zip(mapped, mask))
        assert all(not opaque or hardware_palette[target] == native_rgb555[source]
                   for source, target, opaque in zip(pixels, mapped, mask))
        canvas = Image.new('P', (64, 64))
        canvas.paste(Image.frombytes('P', (48, 48), mapped), (8, 16))
        frame = canvas.tobytes()
        frames.append(frame)
        records.append({'native_frame': index + 1, 'virtual_frame': BASE + index,
                        'sha256': sha(frame), 'opaque_pixels': sum(bool(v) for v in mask),
                        'opaque_black_pixels': sum(bool(a) and p == 0 for p, a in zip(pixels, mask))})
    blob = indexed(b''.join(frames), FRAME_BYTES)
    assert len(blob) < 16 * 1024, 'explosive barrel stream exceeded budget'
    validate(blob, frames)
    unchanged = {name: sha((OUT / name).read_bytes()) for name in
                 ['prop_frames.bin', 'prop_frames.lz', 'door_frames.lz', 'bg_palette.lz']}
    ordinary = (OUT / 'prop_frames.bin').read_bytes()[11 * FRAME_BYTES:12 * FRAME_BYTES]
    # Both retail barrel kinds use the same intact picture. This new import
    # additionally retains its22 opaque black pixels lost by the old packer.
    assert all(a == b or (a == black and b == 0) for a, b in zip(frames[0], ordinary))
    report = {
        'source': 'retail DIABDAT.MPQ', 'mpq_path': 'Objects\\Barrelex.CEL',
        'source_revision': '9f01757243412bb154d02d9af65c804159e6e0ea',
        'source_file': str(SOURCE.relative_to(ROOT)), 'source_bytes': len(raw), 'source_sha256': sha(raw),
        'native_frames': COUNT, 'native_dimensions': [96, 96], 'native_frame_delay': 1,
        'frame_order': 'intact, eight burst frames, debris; native1..10 forward and stop',
        'scaled_dimensions': [48, 48], 'canvas_dimensions': [64, 64], 'canvas_offset': [8, 16],
        'virtual_base': BASE, 'virtual_last': BASE + COUNT - 1, 'bytes_per_frame': FRAME_BYTES,
        'codec': 'independent indexed LZ', 'bytes': len(blob), 'aligned_bytes': (len(blob) + 3) & ~3,
        'sha256': sha(blob), 'decoded_sha256': sha(b''.join(frames)), 'frames': records,
        'native_color_and_alpha_preserved': True, 'opaque_black_palette_index': black,
        'c_decoder_checked_frames': COUNT, 'destination_canaries_checked': True,
        'small_capacity_and_invalid_index_rejected': True, 'existing_asset_sha256': unchanged,
        'existing_assets_changed': False, 'additional_runtime_ram': 0,
    }
    header = '\n'.join([
        '/* Generated by tools/pack_explosive_barrels.py. Native frame1..10. */', '#pragma once',
        '#include <stdint.h>', f'#define PROP_BARRELEX_BASE {BASE}', f'#define PROP_BARRELEX_FRAMES {COUNT}',
        f'#define PROP_BARRELEX_LAST {BASE + COUNT - 1}', '#define PROP_BARRELEX_ANIM_TICKS 1',
        'extern const uint8_t asset_explosive_barrels[];', '',
    ])
    if args.check:
        assert (OUT / 'explosive_barrels.lz').read_bytes() == blob
        assert (OUT / 'explosive_barrels.h').read_text() == header
        assert json.loads(REPORT.read_text()) == report
    else:
        (OUT / 'explosive_barrels.lz').write_bytes(blob)
        (OUT / 'explosive_barrels.h').write_text(header)
        REPORT.write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS {COUNT} native explosive barrel frames; {len(blob)} bytes; '
          'native color/alpha and bounded C decoder verified')


if __name__ == '__main__':
    main()
