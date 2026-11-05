"""Map original spell icon ramps to town and dungeon background palettes."""
from pathlib import Path
import re
import struct
from lz77 import decompress

OUT = Path(__file__).resolve().parents[1] / 'generated'


def values(text, name):
    body = re.search(name + r'[^=]*=\{(.*?)\};', text, re.S)[1]
    return [int(n, 0) for n in re.findall(r'0x[0-9a-fA-F]+|\d+', body)]


def color(word):
    return tuple((word >> shift) & 31 for shift in (0, 5, 10))


def main():
    source = list(struct.unpack('<16H', decompress((OUT / 'bow_palette.lz').read_bytes())))
    raw = values((OUT / 'spell_icon_colors.h').read_text(), 'spell_flat_colors')
    assert len(raw) == 20
    ramps = [raw[i:i+4] for i in range(0, 20, 4)]
    ramps = [ramps[4]] + ramps[:4]
    mask_path = OUT / 'bg_static_slots.bin'
    mask = mask_path.read_bytes() if mask_path.exists() else bytes(values((OUT / 'bg_water_palette.h').read_text(), 'bg_static_slots'))
    assert len(mask) == 32
    rows = []
    for region in ('town', 'bg'):
        palette = [color(v) for v in struct.unpack('<256H', (OUT / (region + '_palette.bin')).read_bytes())]
        allowed = [i for i in range(1, 256) if region == 'town' or mask[i // 8] & (1 << (i & 7))]
        region_rows = []
        for ramp in ramps:
            colors = source[:]
            colors[11:15] = ramp
            result = [0] * 16
            for index in range(1, 15):
                rgb = color(colors[index])
                result[index] = min(allowed, key=lambda j: sum((palette[j][axis] - rgb[axis]) ** 2 for axis in range(3)))
            region_rows.append('{' + ', '.join(map(str, result)) + '}')
        rows.append('{' + ', '.join(region_rows) + '}')
    text = '/* Precomputed original spell-icon colors for town and dungeon BG palettes. */\n'
    text += 'static const uint8_t spellbook_icon_lut[2][5][16]={' + ', '.join(rows) + '};\n'
    (OUT / 'spellbook_palette.h').write_text(text)


if __name__ == '__main__':
    main()
