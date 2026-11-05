"""Pack proportional outer-light masks into gameplay-unused HUD cells.

Ten 256x256 masks cover game light radius2..11. Outer diameter is24*(r+1) by
15*(r+1), giving264x165 at radius10 and288x180 at radius11. Runtime disables
the mask for radius>=12 (light bonus+2). Fixed scroll (8,60) moves asset center (128,128) to screen (120,68).
Each variant reuses the same23 VRAM tile slots; only its ROM asset changes.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHAR_BASE = 0x06008000
MASK_IDS = list(range(346, 362)) + list(range(400, 404)) + list(range(498, 501))
MAP_ADDRESS = 0x0600F800
MIN_RADIUS, MAX_RADIUS = 2, 11


def outside(x, y, radius):
    dx, dy = 2 * x + 1 - 256, 2 * y + 1 - 256
    return 25 * dx * dx + 64 * dy * dy > 14400 * (radius + 1) * (radius + 1)


def flip(tile, flags):
    return tuple(tile[(7-y if flags & 2 else y) * 8 + (7-x if flags & 1 else x)]
                 for y in range(8) for x in range(8))


def map_tile(n):
    if n < 174:
        return 530 + n
    if n < 238:
        return 896 + n - 174
    if n < 282:
        return 266 + n - 238
    return 326 + n - 282


def palette_checks():
    static_header = (ROOT / 'generated/bg_water_palette.h').read_text()
    slots = re.search(r'bg_static_slots\[32\]=\{([^}]+)\}', static_header)
    static_bits = [int(v) for v in slots.group(1).split(',')]
    checked = {}
    for name, index in [('bg', 63), ('town', 127)]:
        data = (ROOT / f'generated/{name}_palette.bin').read_bytes()
        color = struct.unpack_from('<H', data, 2 * index)[0]
        assert color == 0 and index & 15 == 15, (name, index, color)
        if name == 'bg':
            assert static_bits[index >> 3] & (1 << (index & 7))
        checked[name] = {'index': index, 'bank': index >> 4, 'rgb555': color}
    return checked


def pack_variant(radius):
    # Stable initial IDs give the validation harness a known transparent tile.
    tiles = [(0,) * 64, (15,) * 64]
    lookup = {tile: i for i, tile in enumerate(tiles)}
    refs = []
    for ty in range(32):
        for tx in range(32):
            source = tuple(15 if outside(tx*8+x, ty*8+y, radius) else 0
                           for y in range(8) for x in range(8))
            canonical, flags = min((flip(source, f), f) for f in range(4))
            if canonical not in lookup:
                lookup[canonical] = len(tiles)
                tiles.append(canonical)
            index = lookup[canonical]
            assert index < len(MASK_IDS), (radius, len(tiles), 'mask tile budget')
            refs.append(MASK_IDS[index] | (flags << 10))
    assert len(tiles) <= len(MASK_IDS) == 23
    physical_tiles = dict(zip(MASK_IDS, tiles))
    for y in range(256):
        for x in range(256):
            ref = refs[(y // 8) * 32 + x // 8]
            tile = physical_tiles[ref & 1023]
            sx = 7 - (x & 7) if ref & 1024 else x & 7
            sy = 7 - (y & 7) if ref & 2048 else y & 7
            assert bool(tile[sy*8+sx]) == outside(x, y, radius), (radius, x, y)
    # The normal viewport never wraps the 256px texture, including radii11/12
    # whose horizontal ellipse extends beyond the texture's left/right edge.
    assert 0 <= 8 <= 8+239 < 256 and 0 <= 60 <= 60+159 < 256
    screen_black = 0
    for y in range(160):
        for x in range(240):
            dx, dy = 2*x+1-240, 2*y+1-136
            expected = 25*dx*dx+64*dy*dy > 14400*(radius+1)*(radius+1)
            assert outside(x+8, y+60, radius) == expected
            screen_black += expected
    pixels = bytes(tile[i] | (tile[i+1] << 4) for tile in tiles for i in range(0, 64, 2))
    pixels += bytes((len(MASK_IDS)-len(tiles))*32)
    assert len(pixels) == 736
    packed_map = struct.pack('<1024H', *refs)
    digest = hashlib.sha256(pixels+packed_map).hexdigest()
    return pixels, refs, {
        'radius': radius, 'light_bonus': radius-10,
        'ellipse_diameter': [24*(radius+1), 15*(radius+1)],
        'unique_tiles': len(tiles), 'used_tile_bytes': len(tiles)*32,
        'padded_tile_bytes': len(pixels), 'tilemap_bytes': len(packed_map),
        'screen_black_pixels_before_hud': screen_black,
        'asset_sha256': digest,
    }


def render():
    map_ids = {map_tile(n) for n in range(302)}
    assert len(map_ids) == 302 and not map_ids.intersection(MASK_IDS)
    assert not map_ids.intersection(range(960, 1024))  # BG3 screen block31.
    # All new storage is title-only or unallocated HUD cells. The existing
    # dictionary ranges C240..D7FF and F000..F7FF remain untouched.
    title_ids = set(range(266, 310)) | set(range(326, 362))
    free_ids = set(range(400, 404)) | set(range(498, 512))
    relocated = {map_tile(n) for n in range(238, 302)}
    assert relocated.union(MASK_IDS) <= title_ids.union(free_ids)
    assert len(relocated.union(MASK_IDS)) == 64 + 23
    variants = [pack_variant(radius) for radius in range(MIN_RADIUS, MAX_RADIUS+1)]
    count = len(variants)
    assert max(variant[2]['unique_tiles'] for variant in variants) == 23
    text = ('/* Generated by tools/pack_outer_mask.py. Do not edit.\n'
            ' * Variant index=game_light_radius-2 for radii2..11; disable at>=12.\n'
            ' * Opaque nibble15 selects static black bank3 (dungeon) or bank7 (town).\n'
            ' * Tile refs use charbase2 and no palette bank until load time. */\n'
            '#pragma once\n#include <stdint.h>\n'
            'enum { OUTER_MASK_MIN_RADIUS=2, OUTER_MASK_MAX_RADIUS=11,\n'
            '       OUTER_MASK_VARIANT_COUNT=10, OUTER_MASK_TILE_COUNT=23,\n'
            '       OUTER_MASK_TILE_BYTES=736, OUTER_MASK_TRANSPARENT_TILE=346,\n'
            '       OUTER_MASK_SOLID_TILE=347, OUTER_MASK_SCROLL_X=8, OUTER_MASK_SCROLL_Y=60 };\n'
            'static const uint8_t outer_mask_pixels[10][736] __attribute__((aligned(4)))={\n')
    for pixels, _, variant in variants:
        text += f' {{ /* radius{variant["radius"]}: {variant["unique_tiles"]} unique tiles */\n'
        text += '\n'.join('    ' + ','.join(f'0x{v:02x}' for v in pixels[i:i+16]) + ','
                          for i in range(0, len(pixels), 16))
        text += '\n },\n'
    text += '};\nstatic const uint16_t outer_mask_map[10][1024] __attribute__((aligned(4)))={\n'
    for _, refs, variant in variants:
        text += f' {{ /* radius{variant["radius"]} */\n'
        text += '\n'.join('    ' + ','.join(f'0x{v:04x}' for v in refs[i:i+16]) + ','
                          for i in range(0, len(refs), 16))
        text += '\n },\n'
    text += '\n};\n'
    report = {
        'status': 'PASS', 'size': [256, 256], 'center': [128, 128],
        'screen_center': [120, 68], 'ellipse_diameter_formula': ['24 * (radius + 1)', '15 * (radius + 1)'],
        'minimum_radius': MIN_RADIUS, 'maximum_radius': MAX_RADIUS,
        'disable_radius': MAX_RADIUS+1, 'variant_count': count,
        'sample': 'pixel center', 'transparent_tile_id': MASK_IDS[0],
        'solid_tile_id': MASK_IDS[1], 'maximum_unique_tiles': 23,
        'vram_tile_bytes': 736, 'vram_tilemap_bytes': 2048,
        'rom_asset_bytes': count * (736+2048),
        'decoded_pixels_checked': count * 256 * 256,
        'viewport_pixels_checked': count * 240 * 160,
        'viewport_source_bounds_inclusive': [8, 60, 247, 219],
        'viewport_wrap': False, 'base_ellipse_diameter': [264,165], 'bonus1_ellipse_diameter': [288,180], 'disable_light_bonus': 2,
        'minimap_dictionary_capacity': len(map_ids),
        'minimap_relocated_tiles': len(relocated), 'allocation_overlap': False,
        'mask_chunks': [
            {'address': '0x0600AB40', 'bytes': 512},
            {'address': '0x0600B200', 'bytes': 128},
            {'address': '0x0600BE40', 'bytes': 96}],
        'mask_tilemap_address': f'0x{MAP_ADDRESS:08X}',
        'spare_hud_bytes': 352, 'black_palettes': palette_checks(),
        'asset_persistent_ram_bytes': 0,
        'variants': [variant[2] for variant in variants],
    }
    return text, json.dumps(report, indent=2) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='verify without writing')
    args = parser.parse_args()
    header, report = render()
    for path, expected in [(ROOT / 'generated/outer_mask.h', header),
                           (ROOT / 'generated/outer-mask-stage53-check.json', report)]:
        if args.check:
            assert path.read_text() == expected, f'{path.relative_to(ROOT)} is stale'
        else:
            path.write_text(expected)
    print('PASS:10 outer masks for radii2..11; maximum23 tiles/736 bytes +2048-byte map; '
          '655360 decoded pixels +384000 viewport pixels verified; '
          'base264x165, bonus1=288x180, bonus2 disables; minimap302 tiles preserved; no overlap')


if __name__ == '__main__':
    main()
