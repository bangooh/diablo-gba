#!/usr/bin/env python3
"""Restore original L1S.CEL arch7/8 in the three open Cathedral door pieces.

Retail OperateL1LDoor/RDoor sets dSpecial to 7/8 after changing dPiece to
393/408 or 395. The port stores complete terrain pictures instead of a
separate dSpecial pass, so bake that original overlay into the ROM atlas.
Only new deduplicated tiles and three existing frame mappings change.
No runtime work, RAM, palette changes or procedural placement is added.

Run after pack_bg_water_palette.py. Inputs always come from the immutable
Stage41 terrain atlas and original manifest, making repeated runs identical.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def cel_frame(data, frame):
    count = struct.unpack_from('<I', data)[0]
    assert 1 <= frame <= count
    lo, hi = struct.unpack_from('<II', data, frame * 4)
    assert 4 + (count + 1) * 4 <= lo < hi <= len(data)
    raw = data[lo:hi]
    pos = struct.unpack_from('<H', raw)[0]
    assert pos == 10
    pixels = bytearray()
    while pos < len(raw):
        length = raw[pos]
        pos += 1
        if length >= 128:
            pixels.extend(bytes(256 - length))
        else:
            assert length and pos + length <= len(raw)
            pixels.extend(raw[pos:pos + length])
            pos += length
    assert len(pixels) == 64 * 160
    return Image.frombytes('P', (64, 160), bytes(pixels)).transpose(
        Image.Transpose.FLIP_TOP_BOTTOM)


def replace_array(header, name, values):
    updated, count = re.subn(r'(' + name + r'\[\]=\{)[^}]*(\};)',
                            lambda m: m[1] + ','.join(map(str, values)) + m[2],
                            header)
    assert count == 1, name
    return updated


def build(root):
    generated = root / 'generated'
    source = root / 'source_assets'
    original = (source / 'bg_stage41/dungeon_art_raw.rle').read_bytes()
    mapping = (generated / 'background-palette-review/static_index_map.bin').read_bytes()
    assert len(mapping) == 256 and mapping[0] == 0
    assert all(mapping[i] for i in set(original) if i)
    base = original.translate(mapping)
    manifest = json.loads((generated / 'dungeon-art-manifest.json').read_text())
    indices = list(manifest['tile_ids'])
    pieces = manifest['pieces']
    assert len(indices) == len(pieces) * 40 and len(base) % 64 == 0
    assert max(indices) < len(base) // 64
    art = bytearray(base)
    seen = {base[i:i + 64]: i // 64 for i in range(0, len(base), 64)}
    original_indices = list(indices)
    cel = (source / 'door_arches/L1S.CEL').read_bytes()
    palette = (source / 'butcher/L1_1.PAL').read_bytes()
    colors = [tuple(palette[i:i + 3]) for i in range(0, 768, 3)]
    dark = [0] + [min(range(1, 256), key=lambda j: sum(
        (colors[j][k] - colors[i][k] * .50) ** 2 for k in range(3)))
        for i in range(1, 256)]
    lut = [mapping[i] for i in dark]
    targets = {393: 7, 395: 8, 408: 7}
    changes = []
    for record in pieces:
        piece, index = record['source_piece'], record['index']
        if piece not in targets:
            continue
        native = cel_frame(cel, targets[piece])
        assert all(lut[i] for i in set(native.tobytes()) if i)
        overlay = native.resize((32, 80), Image.Resampling.NEAREST).point(lut).tobytes()
        changed_pixels, changed_tiles = 0, 0
        for ty in range(10):
            for tx in range(4):
                at = index * 40 + ty * 4 + tx
                old = base[indices[at] * 64:(indices[at] + 1) * 64]
                special = bytes(overlay[(ty * 8 + y) * 32 + tx * 8 + x]
                                for y in range(8) for x in range(8))
                tile = bytes(b or a for a, b in zip(old, special))
                changed_pixels += sum(a != b for a, b in zip(old, tile))
                changed_tiles += old != tile
                if tile not in seen:
                    seen[tile] = len(art) // 64
                    art.extend(tile)
                indices[at] = seen[tile]
        changes.append({'piece': piece, 'variant': index,
                        'native_special_frame': targets[piece],
                        'changed_pixels': changed_pixels,
                        'changed_tiles': changed_tiles,
                        'native_frame_sha256': sha(native.tobytes()),
                        'mapped_overlay_sha256': sha(overlay)})
    assert len(changes) == 3 and all(c['changed_pixels'] > 0 for c in changes)
    assert len(art) // 64 < 65536
    flags = []
    for i in indices:
        tile = art[i * 64:(i + 1) * 64]
        flags.append(0 if not any(tile) else 1 if all(tile) else 2)
    occludes = [int(record['occludes']) for record in pieces]
    for record in pieces:
        if record['source_piece'] in targets:
            occludes[record['index']] = 1
        else:
            a = record['index'] * 40
            assert indices[a:a + 40] == original_indices[a:a + 40]
    header = (generated / 'dungeon_art.h').read_text()
    header = replace_array(header, 'dart_tile_ids', indices)
    header = replace_array(header, 'dart_tile_flags', flags)
    header = replace_array(header, 'dart_occludes', occludes)
    report = {'status': 'PASS', 'native_source': 'Levels\\L1Data\\L1S.CEL',
              'source_sha256': sha(cel), 'source_bytes': len(cel),
              'source_frames': struct.unpack_from('<I', cel)[0],
              'base_sha256': sha(base), 'atlas_sha256': sha(art),
              'header_sha256': sha(header.encode()), 'pieces': changes,
              'atlas_bytes_before': len(base), 'atlas_bytes_after': len(art),
              'additional_rom_tile_bytes': len(art) - len(base),
              'runtime_work_added': 0, 'persistent_ram_added': 0,
              'unchanged_other_pieces': len(pieces) - len(targets)}
    return {'dungeon_art_raw.rle': bytes(art), 'dungeon_art.h': header.encode(),
            'door-arches-report.json': (json.dumps(report, indent=2) + '\n').encode()}, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    outputs, report = build(args.root.resolve())
    for name, data in outputs.items():
        path = args.root / 'generated' / name
        if args.check:
            assert path.read_bytes() == data, name + ' is stale'
        elif not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
