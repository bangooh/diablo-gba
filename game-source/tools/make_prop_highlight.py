"""Remap only targeted prop pixels to slightly lighter existing BG colors."""
from pathlib import Path
import struct
root=Path(__file__).resolve().parents[1]
pal=struct.unpack('<256H',(root/'generated/bg_palette.bin').read_bytes())
rgb=[tuple((c>>s)&31 for s in (0,5,10)) for c in pal];lut=[0]
for c in rgb[1:]:
 target=[v+(31-v)*.18 for v in c]
 candidates=[j for j in range(1,256) if sum(rgb[j])>sum(c)]
 lut.append(min(candidates or range(1,256),key=lambda j:sum((rgb[j][k]-target[k])**2 for k in range(3))))
(root/'generated/prop_highlight.h').write_text('static const uint8_t prop_highlight[256]={'+','.join(map(str,lut))+'};\n')
