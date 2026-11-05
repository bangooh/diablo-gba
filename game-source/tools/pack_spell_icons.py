"""Original CEL texture reduced with nearest-neighbor sampling to22x22.
Four native background shades reuse unused arrow-palette entries11..14.
Glyphs and dark texture specks are retained; source changes palette only.
"""
from pathlib import Path
import hashlib
import json
import struct
import argparse
from PIL import Image, ImageDraw
from pack_gba_assets import tile_bytes

ROOT = Path(__file__).resolve().parents[1]
ICON_IDS = [0,1,2,3,4,5,6,7,8,9,28,13,12,18,16,14,18,19,11,20,15,21,23,24,25,22,26,29,37,38,39,42,41,40,10,36,30]
STYLES = ['skill', 'blue', 'beige', 'orange', 'invalid']


def decode_cel(raw, frame):
    lo, hi = struct.unpack_from('<II', raw, frame * 4)
    data, pixels, cursor = raw[lo:hi], bytearray(), 0
    while cursor < len(data):
        n = data[cursor]
        cursor += 1
        if n >= 128:
            pixels.extend(bytes(256 - n))
        else:
            pixels.extend(data[cursor:cursor+n])
            cursor += n
    assert len(pixels) == 56 * 56
    return Image.frombytes('P', (56, 56), bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)


def translation(style):
    lut = list(range(256))
    lut[255] = 0
    if style == 'skill':
        return lut
    start = {'blue':176, 'beige':160, 'orange':208, 'invalid':240}[style]
    lut[144:147] = [start+1, start+3, start+5]
    for ramp in (160, 192, 208):
        for shade in range(16):
            lut[ramp+shade] = 0 if style == 'invalid' and shade == 15 else start+shade
    return lut


def build_icons():
    from lz77 import decompress
    raw = (ROOT/'source_assets/spells/SpelIcon.CEL').read_bytes()
    palette = (ROOT/'source_assets/spells/l1.pal').read_bytes()
    rgb = [tuple(palette[i:i+3]) for i in range(0,768,3)]
    bow = list(struct.unpack('<16H',decompress((ROOT/'generated/bow_palette.lz').read_bytes())))
    assert bow[11:15] == [0,0,0,0], 'textured icon ramps require unused bow palette entries11..14'
    # Four native background shades share the arrow bank's unused entries.
    # Glyphs/texture are kept before nearest-neighbor reduction, with no speck filter.
    shades = [1,5,9,13]
    ramps = []
    for start in [176,160,208,240,192]:
        ramps.append([sum((rgb[start+n][k]>>3)<<(5*k) for k in range(3)) for n in shades])
    frames, counts = [], []
    preview = Image.new('RGB',(37*24,4*24))
    for col,frame in enumerate(ICON_IDS):
        canvas = Image.new('P',(24,24),8)
        if frame:
            art = decode_cel(raw,frame).resize((22,22),Image.Resampling.NEAREST)
            colors = [((v&31)*8,((v>>5)&31)*8,((v>>10)&31)*8) for v in bow]
            for y in range(22):
                for x in range(22):
                    source = art.getpixel((x,y))
                    if any(start<=source<start+16 for start in (160,192,208)):
                        color=11+min(3,(source%16)//4)
                    elif 144<=source<147:
                        color=11+min(3,(1+2*(source-144))//4)
                    else:
                        choices=list(range(1,11))+[15]
                        color=min(choices,key=lambda j:sum((colors[j][k]-rgb[source][k])**2 for k in range(3)))
                    canvas.putpixel((x+1,y+1),color)
        frames.append(tile_bytes(canvas,4));counts.append(len(set(canvas.tobytes())))
        for style in range(4):
            pal=bow[:];pal[11:15]=ramps[style]
            variant=canvas.copy()
            variant.putpalette([c for v in pal for c in ((v&31)*255//31,((v>>5)&31)*255//31,((v>>10)&31)*255//31)]+[0]*720)
            preview.paste(variant.convert('RGB'),(col*24,style*24))
    encoded = ',\n'.join('{'+','.join(hex(v) for v in struct.unpack('<72I',frame))+'}' for frame in frames)
    header = '#pragma once\n/* Original CEL texture; source changes four native ramp palette entries. */\n#define SPELL_FLAT_PALETTE_BANK 3\n#define SPELL_FLAT_FIRST_COLOR 11\n#define SPELL_FLAT_COLOR_COUNT 4\n'
    header += 'static const uint16_t spell_flat_colors[5][4]={'+','.join('{'+','.join(hex(v) for v in ramp)+'}' for ramp in ramps)+'};\n'
    header += 'static const uint32_t spell_flat_icons[37][72]={\n'+encoded+'\n};\n'
    (ROOT/'generated/spell_icon_colors.h').write_text(header)
    preview.resize((37*48,4*48),Image.Resampling.NEAREST).save(ROOT/'generated/spell-icon-colors-preview.png')
    report = {'source':'original SpelIcon.CEL full texture, nearest-neighbor 56 to22, four native background shades', 'styles':['learned-blue','scroll-red','staff-orange','unavailable-gray'], 'palette_bank':3,'palette_entries':[11,12,13,14], 'icons':37,'bytes':37*288+40,'dithering':False,'speck_filter':False,'new_static_ram':0,'colors_per_icon':counts,'visible_size':[24,24], 'cel_sha256':hashlib.sha256(raw).hexdigest()}
    (ROOT/'generated/spell-icon-colors-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Textured spell icons:',report['bytes'],'ROM bytes; no new RAM')



if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--mpq',type=Path);args=parser.parse_args()
    if args.mpq:
        import mpyq
        from extract_assets import read_mpq
        archive=mpyq.MPQArchive(str(args.mpq),listfile=False)
        for name,path in [('SpelIcon.CEL','CtrlPan\\SpelIcon.CEL'),('l1.pal','Levels\\L1Data\\L1_1.PAL')]:
            (ROOT/'source_assets/spells'/name).write_bytes(read_mpq(archive,path))
    build_icons()
