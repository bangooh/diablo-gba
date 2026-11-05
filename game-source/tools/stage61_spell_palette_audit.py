"""Prove the flat backgrounds use previously unused, reserved OBJ colors."""
from pathlib import Path
import hashlib
import json
import re
import struct
import subprocess
import sys
from lz77 import decompress
from pack_spell_icons import ROOT, ICON_IDS, decode_cel, icon_foreground
from PIL import Image

base = Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'build/diablo-gba-assets'
reserved = {11,12,13,14}
checks = {}
for name in ['bow_arrows','stairs']:
    data = decompress((ROOT/'generated'/f'{name}.lz').read_bytes())
    used = {value for byte in data for value in (byte&15,byte>>4)}
    assert not used & reserved, (name,used)
    checks[name] = {'used_indices':sorted(used),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
header = (ROOT/'generated/spell_expansion_assets.h').read_text()
array = re.search(r'spell_arrow_tiles\[[^=]+=(\{.*?\});',header,re.S).group(1)
words = [int(n,0) for n in re.findall(r'0x[0-9a-f]+|\b\d+\b',array)]
data = struct.pack('<'+'I'*len(words),*words)
used = {value for byte in data for value in (byte&15,byte>>4)}
assert not used & reserved
checks['spell_arrow_tiles']={'used_indices':sorted(used),'bytes':len(data)}
palette = struct.unpack('<16H',decompress((ROOT/'generated/bow_palette.lz').read_bytes()))
assert all(palette[n]==0 for n in reserved)
symbols = {}
for line in subprocess.check_output(['readelf','-sW',str(base)+'.elf'],text=True).splitlines():
    fields = line.split()
    if len(fields)>=8 and fields[0].endswith(':'):
        try:symbols[fields[7]]=(int(fields[1],16),int(fields[2]))
        except ValueError:pass
rom = Path(str(base)+'.gba').read_bytes()
address,size=symbols['monster_art'];assert size%28==0
banks = set()
for at in range(address-0x08000000,address-0x08000000+size,28):
    banks.update(rom[at+19:at+21])
assert 3 not in banks
checks['monster_palette_banks']={'entries':size//28,'banks':sorted(banks)}
source=(ROOT/'src/gba_mode0.c').read_text()
assert 'return !coop_active?0:cls==1?(game.area?1:6):cls==2?4:0;' in source
assert 'slot*64,6+i,0,3' in source
assert 'unsigned reserved=(1u<<3)|(1u<<5)|(1u<<15)' in (ROOT/'src/actor_palette.inc').read_text()
checks['other_users']={'hero_banks':[0,1,4,6],'npc_banks':'6 + NPC index','bank3_reserved_from_actor_shading':True,'bank3_render_assets':['bow_arrows / spell_arrow_tiles','stairs','new flat spell icons']}
report=json.loads((ROOT/'generated/spell-icon-colors-report.json').read_text())
assert report['dithering'] is False and report['bytes']==10664
assert report['flat_rgb5']==[[12,14,19],[19,9,9],[25,12,6],[14,14,14]]
checks['flat_background']={'icons':37,'glyph_pixels':report['glyph_pixels'],'flat_background_pixels':list(map(len,report['background_pixels'])),'rgb5':report['flat_rgb5'],'palette_indices':[59,60,61,62]}
print(json.dumps({'result':'PASS','romSha256':hashlib.sha256(rom).hexdigest(),'checks':checks,'note':'Only the four previously unused colors change. The renderer installs them after deriving the 8bpp death LUT; the paired native gate verifies the old LUT and all other palette entries.'},indent=2))
