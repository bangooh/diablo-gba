"""Refresh raw asset declarations without recompressing finalized streams."""
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / 'generated'
lines = ['#ifndef PACKED_ASSETS_H', '#define PACKED_ASSETS_H', '#include <stdint.h>']
for path in sorted(OUT.glob('*.bin')):
    lines.extend([f'extern const uint8_t asset_{path.stem}[];',
                  f'#define RAW_{path.stem.upper()} {path.stat().st_size}'])
lines.append('#endif')
(OUT / 'packed_assets.h').write_text('\n'.join(lines) + '\n')
