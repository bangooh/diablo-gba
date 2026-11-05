"""MPQ inventory art -> four 64x64 8bpp OBJs, composed in shared scratch.

Usage: python tools/pack_inventory.py [DIABDAT.MPQ]
Without MPQ, rebuild from source_assets/inventory. Icon cursor IDs and source
dimensions follow Devilution itemdat.cpp/cursor.cpp (9f017572). Keep the v62
logical footprints, including the compact 2x2 small-shield adapter.
"""
from pathlib import Path
import sys, struct, json, hashlib
from PIL import Image
from lz77 import compress, indexed, decompress
from pack_gba_assets import tile_bytes, rgb15
from item_roster import IDS, binding, art_bindings, ICON_UNIQUES

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'generated'
SRC = ROOT / 'source_assets/inventory'
SRC.mkdir(exist_ok=True)
if len(sys.argv) > 1:
    import mpyq
    from extract_assets import read_mpq
    archive = mpyq.MPQArchive(sys.argv[1], listfile=False)
    for name in ['Inv', 'Inv_rog', 'Inv_Sor', 'Objcurs']:
        (SRC / (name + '.CEL')).write_bytes(read_mpq(archive, 'Data\\Inv\\' + name + '.CEL'))
    (SRC / 'town.pal').write_bytes(read_mpq(archive, 'Levels\\TownData\\Town.PAL'))

def cel(raw, frame, width, clipped=False):
    start, end = struct.unpack_from('<II', raw, frame * 4)
    data = raw[start:end]
    p = struct.unpack_from('<H', data)[0] if clipped else 0
    pixels = bytearray()
    while p < len(data):
        run = data[p]; p += 1
        if run >= 128:
            pixels.extend(bytes(256 - run))
        else:
            assert p + run <= len(data)
            pixels.extend(data[p:p + run]); p += run
    assert len(pixels) % width == 0
    return Image.frombytes('P', (width, len(pixels) // width), bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)

rawpal = (SRC / 'town.pal').read_bytes()
palette = [tuple(rawpal[i:i+3]) for i in range(0, 768, 3)]
# OBJ bank5 remains the live potion belt palette. All other banks are restored
# from base_palette on close; automap/world sprites are hidden in this view.
allowed = [i for i in range(1, 256) if not 80 <= i < 96]
lut = [0] + [min(allowed, key=lambda j: sum((palette[j][k]-palette[i][k])**2 for k in range(3))) for i in range(1, 256)]
black = min(allowed, key=lambda j: sum(c*c for c in palette[j]))
gold = min(allowed, key=lambda j: sum((palette[j][k]-(255,208,80)[k])**2 for k in range(3)))
red = min(allowed, key=lambda j: sum((palette[j][k]-(240,48,32)[k])**2 for k in range(3)))
h = ['#pragma once', '#include <stdint.h>', 'extern const uint8_t asset_inventory_panels[];',
     'extern const uint8_t asset_inventory_icons[];', 'extern const uint8_t asset_inventory_palette[];',
     f'#define INV_BLACK {black}', f'#define INV_GOLD {gold}', f'#define INV_RED {red}']
panels = []
for name in ['Inv', 'Inv_rog', 'Inv_Sor']:
    im = cel((SRC/(name+'.CEL')).read_bytes(), 1, 320)
    assert im.size == (320, 352), im.size
    im.putpalette(rawpal); im.save(SRC/(name+'.png'))
    im = im.resize((120,128),Image.Resampling.NEAREST).point([black]+lut[1:])
    canvas = Image.new('P',(128,128)); canvas.paste(im,(8,0))
    canvas.putpalette(rawpal); canvas.save(SRC/(name+'-gba.png'))
    panels.append(b''.join(tile_bytes(canvas.crop((x,y,x+64,y+64))) for y in (0,64) for x in (0,64)))
(OUT/'inventory_panels.lz').write_bytes(indexed(b''.join(panels),16384))
(OUT/'inventory_palette.lz').write_bytes(compress(rgb15(palette)))
# Footprints and cursor frames come from the same binding as ItemDef.
specs = [(b['cursor'],b['source_width'],b['width'],b['height']) for b in art_bindings()]
icons = []; sizes=[]; atlas = Image.new('P',(10*40,((len(specs)+9)//10)*40)); atlas.putpalette(rawpal)
cursor = (SRC/'Objcurs.CEL').read_bytes()
for kind, (idx,width,fw,fh) in enumerate(specs):
    im=cel(cursor,idx+12,width,True); im.putpalette(rawpal)
    im.save(SRC/f'item-{kind:02}.png')
    w,hgt=round(fw*28*120/320),round(fh*28*128/352)
    small=im.resize((w,hgt),Image.Resampling.NEAREST).point(lut)
    canvas=Image.new('P',(32,32));canvas.paste(small,(0,0));icons.append(canvas.tobytes());sizes.append((w,hgt))
    atlas.paste(small,((kind%10)*40+4,(kind//10)*40+4))
atlas.save(SRC/'items-gba.png',transparency=0)
(OUT/'inventory_icons.lz').write_bytes(indexed(b''.join(icons),1024))
h += ['static const uint8_t inventory_icon_size[][2]={'+','.join('{%d,%d}'%s for s in sizes)+'};']
(OUT/'inventory_assets.h').write_text('\n'.join(h)+'\n')
report={'panel_size':[120,128], 'classes':3, 'item_images':len(specs),
        'scratch_bytes':16384+1024, 'obj_bytes':16384, 'extra_ewram_art_bytes':0,
        'source_revision':'devilution 9f01757243412bb154d02d9af65c804159e6e0ea',
        'source_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in SRC.glob('*.CEL')},
        'packed_bytes':sum((OUT/n).stat().st_size for n in ['inventory_panels.lz','inventory_icons.lz','inventory_palette.lz'])}
for name,raw in [('inventory_palette',rgb15(palette))]:
    assert decompress((OUT/(name+'.lz')).read_bytes()) == raw
(OUT/'inventory-art-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))

# Shared stable art index: base items first, then enabled visual overrides.
unique_art=[0]*90
unique_size=[(0,0)]*90
unique_sound=[255]*90
for index,b in enumerate(art_bindings()):
    if 'unique_id' in b:
        unique_art[b['unique_id']]=index
        unique_sound[b['unique_id']]=b['sound_kind']
        unique_size[b['unique_id']]=(b['width'],b['height'])
(OUT/'item_art.h').write_text('#pragma once\nstatic const uint8_t unique_art_index[90]={'+','.join(map(str,unique_art))+'};\nstatic const uint8_t unique_art_size[90][2]={'+','.join('{%d,%d}'%size for size in unique_size)+'};\nstatic const uint8_t unique_sound_kind[90]={'+','.join(map(str,unique_sound))+'};\n')
