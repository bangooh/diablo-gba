"""Map the approved 7x7 bitmap font to existing HUD and frontend glyph slots."""
from pathlib import Path
import json, struct
from lz77 import compress, decompress
from font3x5 import GLYPHS as overlay_glyphs, ROWS as tiny_rows, render as tiny_render

ROOT = Path(__file__).resolve().parents[1]
out = ROOT / 'generated'
rows = (ROOT / 'source_assets/exocet/exocet-8x8-1bpp.bin').read_bytes()
small_rows = (ROOT / 'source_assets/exocet/exocet-5x5-1bpp.bin').read_bytes()
assert len(small_rows) == 640
(out/'info_font.h').write_text('static const uint8_t info_font_rows[128][5]={'+','.join('{'+','.join(map(str,small_rows[c*5:c*5+5]))+'}' for c in range(128))+'};\n')
glyphs = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 /:-'
assert len(rows) == 1024
assert all(rows[c * 8 + 7] == 0 for c in range(128))
assert all(v & 1 == 0 for v in rows)

def colors(raw):
    return [tuple(((c >> s) & 31) * 255 // 31 for s in (0, 5, 10))
            for c in struct.unpack('<256H', raw)]

def nearest(palette, color, stop=256, allowed=None):
    return min((i for i in range(1, stop) if allowed is None or allowed[i>>3]&(1<<(i&7))),
               key=lambda i: sum((palette[i][k] - color[k]) ** 2 for k in range(3)))

def bevel(ch, palette, fg, bg, left=0, width=7, allowed=None):
    pick=lambda rgb: nearest(palette,rgb,252 if allowed is None else 256,allowed)
    rgb=palette[fg]
    light=pick(tuple(min(255,c*5//4+16) for c in rgb))
    dark=pick(tuple(c*3//5 for c in rgb))
    shadow=pick((8,4,4))
    def ink(x,y):
        return 0 <= x < width and 0 <= y < 7 and rows[ord(ch)*8+y] & (128 >> (x+left))
    return bytes((light if not ink(x,y-1) else dark if not ink(x,y+1) else fg) if ink(x,y)
                 else shadow if ink(x-1,y-1) else bg for y in range(8) for x in range(8))

def small_glyph(ch, fg, bg):
    return bytes(fg if 1 <= y < 6 and x < 5 and small_rows[ord(ch) * 5 + y - 1] & (128 >> x) else bg
                 for y in range(8) for x in range(8))

for region, palette_file in [('town', 'town_palette.bin'), ('dungeon', 'bg_palette.bin')]:
    palette = colors((out / palette_file).read_bytes())
    allowed=(out/'bg_static_slots.bin').read_bytes() if region=='dungeon' and (out/'bg_static_slots.bin').exists() else None
    pick=lambda color: nearest(palette,color,allowed=allowed)
    fg, bg = pick((232, 220, 184)), pick((0, 0, 0))
    raw = bytes(64) + b''.join(small_glyph(ch, fg, bg) for ch in glyphs)
    assert len(raw) == 2624
    packed = compress(raw)
    assert decompress(packed) == raw
    (out / f'{region}_font.bin').write_bytes(raw)
    (out / f'{region}_font.lz').write_bytes(packed)
    large = b''.join(bevel(ch, palette, pick((232, 196, 100)), bg, allowed=allowed) for ch in glyphs)
    (out / f'{region}_font_large.bin').write_bytes(large)
    (out / f'{region}_font_large.lz').write_bytes(compress(large))
    # Nine bevelled frame tiles, using spare center tiles in panel row three.
    gold = pick((150, 128, 80))
    light = pick((202, 181, 127))
    dark = pick((54, 48, 36))
    frame = bytearray()
    for ty in range(3):
        for tx in range(3):
            for y in range(8):
                for x in range(8):
                    top = ty == 0 and y in (0, 1, 2)
                    bottom = ty == 2 and y in (5, 6, 7)
                    left = tx == 0 and x in (0, 1, 2)
                    right = tx == 2 and x in (5, 6, 7)
                    v = bg
                    if top or left: v = light if y == 1 or x == 1 else gold
                    if bottom or right: v = dark if y == 7 or x == 7 else gold
                    frame.append(v)
    (out / f'{region}_status_frame.lz').write_bytes(compress(bytes(frame)))
    overlay = tiny_render('LEVEL: 1',5,fg,bg)+tiny_render('DIFFICULTY: NORMAL',11,fg,bg)
    (out / f'{region}_overlay_font.bin').write_bytes(overlay)
    (out / f'{region}_overlay_font.lz').write_bytes(compress(bytes(overlay)))

(out / 'dual_font.h').write_text('extern const uint8_t asset_town_font_large[];\nextern const uint8_t asset_dungeon_font_large[];\nextern const uint8_t asset_town_status_frame[];\nextern const uint8_t asset_dungeon_status_frame[];\n')
(out / 'overlay_font.h').write_text('extern const uint8_t asset_town_overlay_font[];\nextern const uint8_t asset_dungeon_overlay_font[];\n#define OVERLAY_GLYPHS "'+overlay_glyphs+'"\n#define OVERLAY_FONT_BYTES 1024\nstatic const uint8_t overlay_rows[][5]={'+','.join('{'+','.join(map(str,tiny_rows[c]))+'}' for c in overlay_glyphs)+'};\n')

palette = colors(decompress((out / 'front_palette.lz').read_bytes()))
normal = nearest(palette, (205, 180, 120), 252)
bright = nearest(palette, (255, 245, 210), 252)
# These historical .lz names are raw ROM arrays read directly by frontend.inc.
bank = len(glyphs) * 64
# Crop the empty side bearings for proportional frontend text. The renderer
# already reads per-glyph advances; keep 8x8 storage and one blank pixel gap.
def front_bounds(ch):
    columns = [x for x in range(7) if any(rows[ord(ch)*8+y] & (128 >> x) for y in range(7))]
    return (min(columns), max(columns)-min(columns)+1) if columns else (0, 3)

def front_glyph(ch, fg):
    left, width = front_bounds(ch)
    return bevel(ch, palette, fg, 0, left, width)

(out / 'front_font.lz').write_bytes(b''.join(front_glyph(ch, fg) for fg in (normal, bright) for ch in glyphs))
(out / 'front_font_widths.lz').write_bytes(bytes(front_bounds(ch)[1]+1 for ch in glyphs))
(out / 'frontend_font.h').write_text('extern const uint8_t asset_front_font_widths[];\n#define FRONT_FONT_HEIGHT 8\n#define FRONT_FONT_STRIDE 8\n#define FRONT_FONT_BANK ' + str(bank) + '\n')
(out / 'exocet-font-report.json').write_text(json.dumps({'small_ink': [5, 5], 'large_ink': [7, 7], 'cell': [8, 8], 'glyphs': glyphs, 'hud_raw_bytes': 2624, 'frontend_bank_bytes': bank, 'large_font_vram': 'unused center globe-panel tiles, first two rows', 'source': 'Exocet draft7 with user reference 20261010 UXANGSCLEYRP; 5x5 draft1', 'antialiasing': False}, indent=2))
print('Exocet: town/dungeon HUD and frontend regenerated; existing glyph slots retained')
