"""Preserve the approved Stage37 highlight colors in the Stage71 assets.

Stage71 requantized sprite indices: the isolated highlight is now slot 15.
Kinds 26 and 57 share m71_pal_26; kind 39 uses m71_pal_39.
Other Carver variants and unique palettes are intentionally unaffected.
"""
import argparse
import struct
from pathlib import Path
if __package__:
    from .lz77 import compress, decompress
else:
    from lz77 import compress, decompress

ROOT = Path(__file__).resolve().parents[1]
RULES = {
    26: ((30, 30, 30), ((9, 9, 10), (13, 13, 14))),
    39: ((31, 31, 31), ((6, 8, 14), (11, 12, 17))),
}

def rgb555(rgb):
    return rgb[0] | rgb[1] << 5 | rgb[2] << 10

def verify_or_apply(generated, apply=False):
    for kind, (old, replacements) in RULES.items():
        path = generated / f'm71_pal_{kind}.lz'
        raw = decompress(path.read_bytes())
        assert len(raw) == 64, path
        colors = list(struct.unpack('<32H', raw))
        for bank, replacement in enumerate(replacements):
            index = bank * 16 + 15
            expected = rgb555(replacement)
            assert colors[index] in (rgb555(old), expected), f'{path}: changed quantization; review required'
            if not apply:
                assert colors[index] == expected, f'{path}: approved highlight correction missing; run tools/monster_palette_style.py --apply'
            colors[index] = expected
        styled = struct.pack('<32H', *colors)
        if apply and styled != raw:
            path.write_bytes(compress(styled))
    rows = (generated / 'monster71_art.inc').read_text().splitlines()
    assert 'asset_m71_pal_26,' in rows[26] and 'asset_m71_pal_26,' in rows[57]
    assert 'asset_m71_pal_39,' in rows[39]

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--generated', type=Path, default=ROOT / 'generated')
    args = parser.parse_args()
    verify_or_apply(args.generated, args.apply)
    print('Approved palette corrections verified: 26 / 39 / 57')
