"""Render and verify the actual C procedural HP/MP globes, not a mock-up.

Uses the shipping town/dungeon palettes and the actual water-palette C header.
The C header is compiled with a host compiler; no ROM is built. Requires Pillow.
"""
import argparse
import ctypes
import hashlib
import json
import struct
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]


def linear(data):
    return bytes(data[(y//8)*256+(x//8)*64+(y%8)*8+x%8]
                 for y in range(32) for x in range(32))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path,
                        default=ROOT/'generated/stage42-globes')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    U8 = ctypes.c_uint8
    U16 = ctypes.c_uint16
    report = {'renderer_sha256': hashlib.sha256((ROOT/'src/globes.h').read_bytes()).hexdigest(),
              'frame_format': '32x32,16 tiles of8x8,8bpp,palette index0 transparent',
              'fill_heights': 29, 'palette_LUT_bytes': 21, 'extra_frame_buffer_bytes': 0,
              'ROM_globe_asset_bytes_removed_before_renderer_cost': sum(
                  (ROOT/f'generated/control_globes_{area}.lz').stat().st_size
                  for area in ('town', 'dungeon')),
              'areas': {}, 'checks': {}}
    with tempfile.TemporaryDirectory(prefix='gba-procedural-globes-') as tmp:
        tmp = Path(tmp)
        (tmp/'wrapper.c').write_text(
            '#include "globes.h"\n'
            '#include "bg_water_palette.h"\n'
            'void p(uint8_t *a,const uint16_t *b,const uint8_t *c){globe_palette(a,b,c);}\n'
            'unsigned q(int a,int b){return globe_level(a,b);}\n'
            'void r(uint8_t *a,unsigned b,unsigned c,const uint8_t *d){globe_render(a,b,c,d);}\n'
            'void w(uint16_t *p,unsigned s){for(unsigned n=0;n<30;n++)p[bg_water_slots[n]]=bg_water_color(s,n);}\n'
            'unsigned m(unsigned i){return bg_static_slots[i];}\n')
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', '-I', str(ROOT/'src'),
                        '-I', str(ROOT/'generated'),
                        str(tmp/'wrapper.c'), str(ROOT/'src/lz.c'), '-o', str(tmp/'globes.so')], check=True)
        c = ctypes.CDLL(str(tmp/'globes.so'))
        c.p.argtypes = [ctypes.POINTER(U8), ctypes.POINTER(U16), ctypes.POINTER(U8)]
        c.q.argtypes = [ctypes.c_int, ctypes.c_int]
        c.q.restype = ctypes.c_uint
        c.r.argtypes = [ctypes.POINTER(U8), ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(U8)]
        c.w.argtypes = [ctypes.POINTER(U16), ctypes.c_uint]
        c.m.argtypes = [ctypes.c_uint]
        c.m.restype = ctypes.c_uint
        c.lz_decode.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint]
        quantization_cases = 0
        for maximum in range(0, 513):
            last = 0
            for value in range(-1, maximum+2):
                level = c.q(value, maximum)
                assert 0 <= level <= 28
                if value <= 0 or maximum <= 0:
                    assert level == 0
                elif value >= maximum:
                    assert level == 28
                else:
                    assert 0 < level < 28
                assert level >= last
                last = level
                quantization_cases += 1
        assert c.q(50,100)==14 and c.q(50,200)==7
        report['checks']['quantization_and_maximum_change_cases'] = quantization_cases+2
        raw_buffer = (U8*1088)()
        destination = ctypes.cast(ctypes.byref(raw_buffer,32), ctypes.POINTER(U8))
        sentinel = bytes([0xDA])*32

        def draw(side, level, colors):
            for i in range(len(raw_buffer)):
                raw_buffer[i] = 0xDA
            c.r(destination, side, level, colors)
            assert bytes(raw_buffer[:32]) == sentinel
            assert bytes(raw_buffer[1056:]) == sentinel
            return bytes(raw_buffer[32:1056])

        # Independent geometry checks: stable circular footprint and increasing
        # liquid coverage, with every output byte initialized and guardwords intact.
        markers = (U8*21)(*range(1,22))
        mask = None
        for side in range(2):
            coverage = []
            for level in range(29):
                raw = linear(draw(side,level,markers))
                assert all(p<=21 for p in raw)
                current = bytes(bool(p) for p in raw)
                if mask is None:
                    mask = current
                assert mask == current
                low = 12 if side==0 else 17
                coverage.append(sum(low<=p<low+5 for p in raw))
            # Each colored rim is present even when empty. Count liquid growth
            # relative to the dry rendering, not every liquid-color-role pixel.
            dry_coverage = coverage[0]
            coverage = [n-dry_coverage for n in coverage]
            assert coverage[0] == 0
            assert all(a<b for a,b in zip(coverage,coverage[1:]))
            assert draw(side,0xFFFFFFFF,markers)==draw(side,28,markers)
            report['checks'][f'{"HP" if side==0 else "MP"}_liquid_pixels'] = coverage
        assert mask[0]==mask[31]==mask[-32]==mask[-1]==0
        assert mask == b''.join(mask[y*32:(y+1)*32][::-1] for y in range(32))
        assert mask == b''.join(mask[y*32:(y+1)*32] for y in range(31,-1,-1))
        report['checks']['unchanged_circle_opaque_pixels'] = sum(mask)
        report['checks']['guarded_render_cases'] = 62

        # The requested MP design is exactly the HP design with a blue ramp.
        for level in range(29):
            hp=draw(0,level,markers)
            mp=draw(1,level,markers)
            assert mp==bytes(p+5 if 12<=p<=16 else p for p in hp)
        report['checks']['matching_HP_MP_geometry_and_shading_levels'] = 29
        report['checks']['guarded_render_cases'] += 58

        allowed = (U8*32)(*[c.m(i) for i in range(32)])
        assert bytes(allowed)==(ROOT/'generated/bg_static_slots.bin').read_bytes()
        static_slots = {i for i in range(1,256) if allowed[i>>3] & (1<<(i&7))}
        palettes = {}
        for area,filename,permit in [('Town','town_palette',None),('Dungeon','bg_palette',allowed)]:
            decoded = (U8*512)()
            assert c.lz_decode((ROOT/f'generated/{filename}.lz').read_bytes(),decoded,512)==512
            palettes[area] = (bytes(decoded),permit)
        rows = []
        for area,(raw_palette,permit) in palettes.items():
            words = struct.unpack('<256H',raw_palette)
            palette = (U16*256)(*words)
            colors = (U8*21)()
            c.p(colors,palette,permit)
            assert all(colors)
            for role,index in enumerate(colors[11:],start=11):
                word=words[index]
                red,green,blue=((word>>shift)&31 for shift in (0,5,10))
                assert (red and red>=4*green and red>=4*blue) if role<16 else (
                    blue and blue>=2*red and blue>=2*green)
            if permit is not None:
                assert set(colors) <= static_slots
                # Water animation cannot alter a selected globe color.
                for state in range(34):
                    state_palette = (U16*256)(*words)
                    c.w(state_palette,state)
                    assert all(state_palette[i]==words[i] for i in colors)
            report['areas'][area] = {'RGB555_palette_sha256':hashlib.sha256(raw_palette).hexdigest(),
                                      'color_indices':list(colors),'palette_color_count':len(set(colors))}
            rgb = [tuple(((word>>shift)&31)*255//31 for shift in (0,5,10)) for word in words]
            for side in range(2):
                images = []
                all_frames = []
                for level in range(29):
                    raw = draw(side,level,colors)
                    all_frames.append(raw)
                    img = Image.new('RGBA',(32,32))
                    img.putdata([(*rgb[p],255) if p else (0,0,0,0) for p in linear(raw)])
                    images.append(img)
                (args.output/f'{area.lower()}_{"hp" if side==0 else "mp"}.8bpp.bin').write_bytes(b''.join(all_frames))
                rows.append((area+('  HP' if side==0 else '  MP'),images))

        # Large nearest-neighbor samples expose exact pixels; the checkerboard
        # is preview-only and proves the corners are transparent, not black.
        levels = [0,1,7,14,21,28]
        width,height = 1160,780
        sheet = Image.new('RGB',(width,height),(19,22,27))
        d = ImageDraw.Draw(sheet)
        try:
            title = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',23)
            label = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',17)
        except OSError:
            title = label = ImageFont.load_default()
        d.text((24,19),'PROCEDURAL GLASS ORBS / EXACT C OUTPUT',fill=(235,236,239),font=title)
        d.text((24,53),'32 x 32 pixels · existing scene palettes · transparent exterior · 4x nearest-neighbor',fill=(160,170,185),font=label)
        for col,level in enumerate(levels):
            x = 177+col*160
            d.text((x,89),f'{level}/28',fill=(205,212,223),font=label)
        for row,(name,frames) in enumerate(rows):
            y = 120+row*160
            d.text((24,y+50),name,fill=(228,232,240),font=label)
            for col,level in enumerate(levels):
                x=164+col*160
                for cy in range(0,128,16):
                    for cx in range(0,128,16):
                        color=(42,45,52) if (cx//16+cy//16)&1 else (32,35,41)
                        d.rectangle((x+cx,y+cy,x+cx+15,y+cy+15),fill=color)
                scaled=frames[level].resize((128,128),Image.Resampling.NEAREST)
                sheet.paste(scaled,(x,y),scaled)
        sheet.save(args.output/'procedural-globes-preview.png')
        report['checks']['water_palette_states_preserve_globe_colors'] = 34
        report['checks']['guarded_render_cases'] += 116
    (args.output/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()
