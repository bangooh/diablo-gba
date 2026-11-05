"""Verify original floor pixels and native unique corpses with the C decoder.

The first run accepts --baseline pointing to the preserved Stage25 generated
files and records their immutable decoded hash. Later --check runs require no
duplicate old asset: the same pixels are collected in their original order.
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
from extract_assets import cl2_frames
from lz77 import decompress
from pack_unique_monsters import GROUPS, ROOT, SOURCE, TRANSLATIONS, translation

OUT = ROOT / 'generated'
REPORT = OUT / 'unique-deaths-report.json'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def number(path, key):
    return int(re.search(key + r'=(\d+)', path.read_text())[1])


def values(text, key):
    return list(map(int, re.search(key + r'\[\]=\{([^}]+)', text)[1].split(',')))


class Decoder:
    def __init__(self, temp, blob):
        code = Path(temp) / 'floor.c'
        code.write_text('#include <stdint.h>\n#include "' + str(ROOT / 'src/floor_decode.h') +
                        '"\nvoid decode(const uint8_t *a,unsigned f,uint8_t *o){floor_decode(a,f,o);}\n')
        shared = Path(temp) / 'floor.so'
        subprocess.run(['gcc', '-O2', '-shared', '-fPIC', str(code), '-o', str(shared)], check=True)
        self.library = ctypes.CDLL(str(shared))
        self.library.decode.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p]
        self.source = ctypes.create_string_buffer(blob)
        self.destination = (ctypes.c_uint32 * 1026)()
        self.blob = blob

    def frame(self, index):
        self.destination[0] = self.destination[1025] = 0xA5A5A5A5
        self.library.decode(self.source, index, ctypes.byref(self.destination, 4))
        assert self.destination[0] == self.destination[1025] == 0xA5A5A5A5
        return ctypes.string_at(ctypes.byref(self.destination, 4), 4096)

    def body(self, index):
        frame = struct.unpack_from('<I', self.blob, 4 + index * 4)[0]
        return struct.unpack_from('<I', self.blob, frame + 8)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if args.baseline:
        old_blob = (args.baseline / 'floor_decals.rle').read_bytes()
        old_report = json.loads((args.baseline / 'floor-decals-report.json').read_text())
        provenance = {'encoded_sha256': sha(old_blob), 'encoded_bytes': len(old_blob),
                      'decoded_sha256': old_report['decoded_sha256'], 'frames': old_report['frames'],
                      'item_base': number(args.baseline / 'floor_decals.h', 'FLOOR_ITEM_BASE'),
                      'corpse_types': 50}
    else:
        assert args.check, 'use --baseline for the first verification, --check afterward'
        provenance = json.loads(REPORT.read_text())['original_floor_asset']
    blob = (OUT / 'floor_decals.rle').read_bytes()
    # Stage37 intentionally styles two ordinary corpse palettes. Recover the
    # exact preserved native layer for this historical provenance validator;
    # check-floor-palette-style.py checks the rendered style and all other pixels.
    style_path = OUT / 'floor-palette-style-report.json'
    if style_path.exists():
        from floor_palette_style import restore_baseline
        blob = restore_baseline(blob, json.loads(style_path.read_text()))
    item_base = number(OUT / 'floor_decals.h', 'FLOOR_ITEM_BASE')
    definitions = (OUT / 'monster_death.h').read_text()
    bases, counts = values(definitions, 'monster_death_base'), values(definitions, 'monster_death_frames')
    assert len(bases) == len(counts) == 54
    records = json.loads((SOURCE / 'monsters.json').read_text())['monsters']
    native_palette = (ROOT / 'source_assets/worldgen/L1_1.PAL').read_bytes()
    bg = struct.unpack('<256H', decompress((OUT / 'bg_palette.lz').read_bytes()))
    bg_rgb = [tuple(((color >> shift) & 31) * 255 // 31 for shift in (0, 5, 10)) for color in bg]
    native_rgb = [tuple(native_palette[i:i + 3]) for i in range(0, 768, 3)]
    def nearest(rgb):
        return min(range(1, 256), key=lambda j: sum((bg_rgb[j][c] - rgb[c]) ** 2 for c in range(3)))
    color_map = [nearest(tuple(v * .8 for v in color)) for color in native_rgb]
    rows = []
    with tempfile.TemporaryDirectory(prefix='unique-death-check-') as temp:
        decoder = Decoder(temp, blob)
        digest = hashlib.sha256()
        # Static corpses stay at their old indices; old animated deaths move
        # by 32, and items move after all 552 added corpse/death frames.
        for frame in range(provenance['frames']):
            target = frame if frame < 400 else frame + 32 if frame < provenance['item_base'] else frame + item_base - provenance['item_base']
            digest.update(decoder.frame(target))
        assert digest.hexdigest() == provenance['decoded_sha256'], 'existing monster or item pixels changed'
        for kind, base_kind, key, name, trn in GROUPS:
            base = records[base_kind]
            source = SOURCE / base['key'] / 'd.cl2'
            raw = source.read_bytes()
            composed = translation((source.parent / 'translation.trn').read_bytes(),
                                   (TRANSLATIONS / (trn + '.TRN')).read_bytes())
            mapping = bytes([0] + [color_map[index] for index in composed[1:]])
            groups = cl2_frames(raw, base['width'])
            assert all(len(group) == counts[kind] == counts[base_kind] for group in groups)
            pixels_hash = hashlib.sha256()
            checked = 0
            for direction, group in enumerate(groups):
                for index, native in enumerate(group):
                    side = max(128, base['width'], native.height)
                    canvas = Image.new('P', (side, side))
                    canvas.paste(native, ((side - native.width) // 2, side - native.height))
                    source_pixels = canvas.resize((64, 64), Image.Resampling.NEAREST).tobytes()
                    expected = bytes(mapping[pixel] for pixel in source_pixels)
                    frame = bases[kind] + direction * counts[kind] + index
                    base_frame = bases[base_kind] + direction * counts[base_kind] + index
                    assert decoder.frame(frame) == expected, (key, direction, index)
                    assert decoder.body(frame) == decoder.body(base_frame), 'native geometry duplicated in ROM'
                    assert all(bool(a) == bool(b) for a, b in zip(source_pixels, expected)), 'black became transparent'
                    pixels_hash.update(expected)
                    checked += 1
                assert decoder.frame(kind * 8 + direction) == expected
                assert decoder.body(kind * 8 + direction) == decoder.body(base_kind * 8 + direction)
                checked += 1
            rows.append({'kind': kind, 'key': key, 'base_kind': base_kind, 'checked_frames': checked,
                         'source_file': str(source.relative_to(ROOT)), 'source_sha256': sha(raw),
                         'death_pixels_sha256': pixels_hash.hexdigest(), 'all_bodies_share_base_geometry': True,
                         'native_black_opacity_preserved': True})
    report = {'original_floor_asset': provenance, 'original_pixels_unchanged': True,
              'corpse_types': 54, 'static_corpse_frames': 432,
              'frames': struct.unpack_from('<I', blob)[0], 'leaders': rows,
              'sha256': sha(blob), 'encoded_bytes': len(blob),
              'additional_bytes': len(blob) - provenance['encoded_bytes'],
              'added_frames': struct.unpack_from('<I', blob)[0] - provenance['frames'],
              'c_decoder_checked': True}
    if args.check:
        assert json.loads(REPORT.read_text()) == report
    else:
        REPORT.write_text(json.dumps(report, indent=2) + '\n')
    print('PASS', provenance['frames'], 'original floor frames unchanged;',
          report['added_frames'], 'native unique frames share their base geometry;',
          report['additional_bytes'], 'additional bytes')


if __name__ == '__main__':
    main()
