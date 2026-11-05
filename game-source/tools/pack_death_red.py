"""Retail lighting.cpp MakeLightTable red translation and RGB555 source palettes.

RedBack applies this third special bank before drawing gmenu and the panel.
Original source references are retained in source_assets.
"""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
tr=[]
for i in range(8):
    tr += [0 if i==0 and c==226 else c for c in range(226,239)]+[0]*3
tr += list(range(224,240,2))*4
tr += (list(range(224,239))+[0])*6
assert len(tr)==256
palettes=[]
for n in range(1,5):
    raw=(ROOT/f'source_assets/worldgen/L{n}_1.PAL').read_bytes()
    palettes.append([sum((raw[i*3+j]>>3)<<(j*5) for j in range(3)) for i in range(256)])
(ROOT/'generated/death_red.h').write_text('/* Retail MakeLightTable red bank and regional palettes; GBA RGB555. */\nstatic const uint8_t death_red_translation[256]={'+','.join(map(str,tr))+'};\nstatic const uint16_t death_source_palette[4][256]={'+','.join('{'+','.join(map(str,p))+'}' for p in palettes)+'};\n')
print('Retail death red translation: 256 indices, four regional RGB555 palettes')
