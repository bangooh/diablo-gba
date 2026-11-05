#!/usr/bin/env python3
"""Pack exact native prop pixels into a deduplicated ROM 8x8 tile atlas.

The .lz asset suffix is the bundle's discovery convention: prop_atlas.lz holds
raw, row-major 8x8 byte tiles, not an LZ stream. Tile zero is transparent. The
header maps every existing virtual frame to 64 uint16 tile IDs, so runtime can
skip empty tiles and blend the rest without whole-frame decoding or a cache.
No MPQ, image conversion, quantization or palette changes are involved.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from lz77 import decompress

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'generated'
TILE_BYTES = 64
FRAME_BYTES = 4096


def sha(data):
    return hashlib.sha256(data).hexdigest()


def macro(path, name):
    match = re.search(r'^#define ' + name + r' (\d+)\s*$', path.read_text(), re.M)
    assert match, name
    return int(match[1])


def decoded_frames(path):
    data = path.read_bytes()
    assert len(data) >= 4
    count = struct.unpack_from('<I', data)[0]
    assert 4 + count * 4 <= len(data)
    offsets = struct.unpack_from(f'<{count}I', data, 4)
    frames = []
    for offset in offsets:
        assert 4 + count * 4 <= offset < len(data)
        assert data[offset] == 0x10
        frame = decompress(data[offset:])
        assert len(frame) == FRAME_BYTES
        frames.append(frame)
    return data, frames


def source_frames():
    door = OUT / 'door_frames.h'
    explosive = OUT / 'explosive_barrels.h'
    wall = OUT / 'wall_traps.h'
    environment = OUT / 'environment_props.h'
    fountain = OUT / 'fountain_props.h'
    specs = [
        ('prop_frames.lz', 0, macro(door, 'PROP_REGIONAL_DOORS_BASE')),
        ('door_frames.lz', macro(door, 'PROP_REGIONAL_DOORS_BASE'),
         macro(door, 'PROP_REGIONAL_DOORS_FRAMES')),
        ('explosive_barrels.lz', macro(explosive, 'PROP_BARRELEX_BASE'),
         macro(explosive, 'PROP_BARRELEX_FRAMES')),
        ('wall_traps.lz', macro(wall, 'PROP_WALL_TRAP_BASE'),
         macro(wall, 'PROP_WALL_TRAP_FRAMES')),
        ('environment_props.lz', macro(environment, 'PROP_ENVIRONMENT_BASE'),
         macro(environment, 'PROP_ENVIRONMENT_FRAMES')),
        ('fountain_props.lz', macro(fountain, 'PROP_FOUNTAIN_BASE'),
         macro(fountain, 'PROP_FOUNTAIN_FRAMES')),
    ]
    specs.append(('quest_objects71.lz', macro(OUT/'quest_objects71.h','PROP_QUEST71_BASE'), macro(OUT/'quest_objects71.h','PROP_QUEST71_FRAMES')))
    specs.append(("environment76.lz", macro(OUT/"environment76.h","PROP_ENV76_BASE"), macro(OUT/"environment76.h","PROP_ENV76_FRAMES")))
    frames, sources = [], []
    for name, base, count in specs:
        packed, native = decoded_frames(OUT / name)
        assert base == len(frames) and count == len(native), (name, base, count)
        if name == 'prop_frames.lz':
            assert b''.join(native) == (OUT / 'prop_frames.bin').read_bytes()
        sources.append({'asset': name, 'virtual_base': base, 'frames': count,
                        'packed_bytes': len(packed), 'packed_sha256': sha(packed),
                        'decoded_sha256': sha(b''.join(native))})
        frames.extend(native)
    assert len(frames) == macro(OUT/"environment76.h", "PROP_ENV76_FRAME_LAST") + 1
    return frames, sources


def build():
    frames, sources = source_frames()
    # Preserve the shipping open-door visibility treatment: clear one pixel
    # in every 2x2 block. Closed poses and native source streams stay intact.
    open_doors = []
    for header, name in (('butcher_assets.h', 'PROP_L1DOORS_BASE'),
                         ('door_frames.h', 'PROP_L2DOORS_BASE'),
                         ('door_frames.h', 'PROP_L3DOORS_BASE')):
        base = macro(OUT / header, name)
        for index in (base + 2, base + 3):
            frame = bytearray(frames[index])
            for y in range(1, 64, 2):
                for x in range(1, 64, 2):
                    frame[y * 64 + x] = 0
            frames[index] = bytes(frame)
            open_doors.append(index)
    zero = bytes(TILE_BYTES)
    unique, seen, indices = [zero], {zero: 0}, []
    for frame in frames:
        mapping = []
        for ty in range(0, 64, 8):
            for tx in range(0, 64, 8):
                tile = b''.join(frame[y * 64 + tx:y * 64 + tx + 8]
                                for y in range(ty, ty + 8))
                assert len(tile) == TILE_BYTES
                if tile not in seen:
                    seen[tile] = len(unique)
                    unique.append(tile)
                mapping.append(seen[tile])
        assert len(mapping) == 64
        indices.append(mapping)
    assert len(unique) <= 65536
    atlas = b''.join(unique)
    assert atlas[:TILE_BYTES] == zero and len(atlas) % 4 == 0
    assert all(any(tile) for tile in unique[1:])
    flat_indices = [index for mapping in indices for index in mapping]
    index_bytes = struct.pack(f'<{len(flat_indices)}H', *flat_indices)

    # The highlight LUT is presentation-only. Preserve zero transparency and
    # nonzero opaque black exactly; do not reinterpret a color as an alpha bit.
    match = re.search(r'prop_highlight\[256\]\s*=\s*\{([^}]+)\}',
                      (OUT / 'prop_highlight.h').read_text())
    assert match
    highlight = [int(value) for value in re.findall(r'\d+', match[1])]
    assert len(highlight) == 256 and all(0 <= value <= 255 for value in highlight)
    assert highlight[0] == 0 and all(highlight[1:])
    recovered = []
    empty, solid, mixed, highlight_pixels = 0, 0, 0, 0
    for frame, mapping in zip(frames, indices):
        restored = bytearray(FRAME_BYTES)
        for row in range(8):
            for col in range(8):
                index = mapping[row * 8 + col]
                assert 0 <= index < len(unique)
                tile = atlas[index * TILE_BYTES:(index + 1) * TILE_BYTES]
                assert len(tile) == TILE_BYTES
                empty += not any(tile)
                solid += all(tile)
                mixed += any(tile) and not all(tile)
                # Independent inverse of the packer's tile extraction.
                for y in range(8):
                    for x in range(8):
                        at = (row * 8 + y) * 64 + col * 8 + x
                        value = tile[y * 8 + x]
                        restored[at] = value
                        assert bool(value) == bool(frame[at])
                        assert highlight[value] == highlight[frame[at]]
                        highlight_pixels += bool(value)
        assert bytes(restored) == frame
        recovered.append(bytes(restored))
    assert b''.join(recovered) == b''.join(frames)

    lines = ['/* Generated native prop atlas with the existing open-door visibility mask. */',
             '#pragma once', '#include <stdint.h>',
             f'#define PROP_ATLAS_FRAMES {len(frames)}u',
             f'#define PROP_ATLAS_TILES {len(unique)}u',
             f'#define PROP_ATLAS_TILE_BYTES {TILE_BYTES}u',
             f'#define PROP_ATLAS_BYTES {len(atlas)}u',
             f'#define PROP_ATLAS_INDEX_BYTES {len(index_bytes)}u',
             '/* Raw 64-byte tiles; the asset suffix is not its compression format. */',
             'extern const uint8_t asset_prop_atlas[];',
             '/* Frame-local tile order: y/8*8+x/8. Tile0 is entirely transparent. */',
             'static const uint16_t prop_atlas_tiles[PROP_ATLAS_FRAMES][64]={']
    for frame, mapping in enumerate(indices):
        lines.append(f' /* Virtual frame {frame}. */ {{')
        for row in range(8):
            lines.append('  ' + ','.join(map(str, mapping[row * 8:(row + 1) * 8])) + ',')
        lines.append(' },')
    lines.extend(['};', ''])
    header = '\n'.join(lines).encode()
    old_bytes = sum(source['packed_bytes'] for source in sources)
    old_aligned = sum((source['packed_bytes'] + 3) & ~3 for source in sources)
    report = {'status': 'PASS', 'format': 'raw-row-major-8x8-u8-deduplicated-v1',
              'sources': sources, 'open_door_visibility_frames': open_doors, 'open_door_visibility': 'clear odd x / odd y pixels; 75 percent coverage', 'frames': len(frames), 'frame_bytes': FRAME_BYTES,
              'unique_tiles': len(unique), 'transparent_tile': 0,
              'tile_bytes': TILE_BYTES, 'atlas_bytes': len(atlas),
              'index_bytes': len(index_bytes), 'total_rom_asset_bytes': len(atlas) + len(index_bytes),
              'replaced_lz_bytes': old_bytes, 'replaced_lz_aligned_bytes': old_aligned,
              'rom_asset_delta_aligned_bytes': len(atlas) + len(index_bytes) - old_aligned,
              'referenced_tile_counts': {'empty': empty, 'opaque': solid, 'mixed': mixed},
              'decoded_sha256': sha(b''.join(frames)), 'reconstructed_sha256': sha(b''.join(recovered)),
              'atlas_sha256': sha(atlas), 'index_sha256': sha(index_bytes),
              'header_sha256': sha(header), 'highlight_lut_sha256': sha(bytes(highlight)),
              'native_pixels_checked': len(frames) * FRAME_BYTES,
              'nonzero_highlight_pixels_checked': highlight_pixels,
              'pixel_alpha_highlight_verification': 'PASS: every pixel, tile index and highlight LUT result matches presentation frames after the open-door visibility mask',
              'persistent_ram_added': 0,
              'qualification': 'Asset-only ROM delta excludes runtime code changes and whole-bundle alias effects; no ROM build.'}
    return {'prop_atlas.lz': atlas, 'prop_atlas.h': header,
            'prop-atlas-report.json': (json.dumps(report, indent=2) + '\n').encode()}, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    outputs, report = build()
    for name, data in outputs.items():
        path = OUT / name
        if args.check:
            assert path.read_bytes() == data, name + ' is stale'
        elif not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)
    print(f"PASS: {report['frames']} exact prop frames, {report['unique_tiles']} tiles; "
          f"{report['atlas_bytes']:,}B atlas + {report['index_bytes']:,}B indices; "
          f"{report['rom_asset_delta_aligned_bytes']:+,}B aligned ROM asset delta")


if __name__ == '__main__':
    main()
