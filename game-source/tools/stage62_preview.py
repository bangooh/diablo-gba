#!/usr/bin/env python3
"""Compose actual native screenshots; no redraw or retouching of game pixels."""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
args = p.parse_args()
validation = root / 'generated/stage62-validation'
candidate = hashlib.sha256((root / 'build/diablo-gba-assets.gba').read_bytes()).hexdigest()
for name in ['npc-ui-native', 'item-colors-native', 'minimap-native']:
    report = json.loads((validation / name / 'report.json').read_text())
    assert report['result'] == 'PASS' and report['romSha256'] == candidate, name

panels = [
    ('NPC selection / original red cursors', 'npc-ui-native/npc-8-landing.png'),
    ('Speech / right position scrollbar', 'npc-ui-native/speech-scroll-bottom.png'),
    ('Inventory / blue magic item name', 'item-colors-native/inventory-magic-identified-wrap.png'),
    ('Shop / gold unique item name', 'item-colors-native/shop-unique-requirements-2.png'),
    ('Minimap before / Stage61', 'minimap-baseline/L1-all-explored-static-on.png'),
    ('Minimap after / Stage62, 200%', 'minimap-native/L1-all-explored-static-on.png'),
]
font_path = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
bold_path = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
title = ImageFont.truetype(bold_path, 29)
label = ImageFont.truetype(font_path, 19)
small = ImageFont.truetype(font_path, 15)
im = Image.new('RGB', (1008, 1188), '#151516')
d = ImageDraw.Draw(im)
d.text((20, 16), 'DIABLO GBA / STAGE62', font=title, fill='#ddc687')
d.text((20, 56), 'Actual native ROM captures  /  240 x 160 at 2x nearest-neighbor scale', font=small, fill='#c4c1b9')
manifest = []
for i, (name, rel) in enumerate(panels):
    x, y = 20 + (i % 2) * 488, 98 + (i // 2) * 358
    image = Image.open(validation / rel).convert('RGB')
    assert image.size == (240, 160)
    d.text((x, y), name, font=label, fill='#e0d7bd')
    im.paste(image.resize((480, 320), Image.Resampling.NEAREST), (x, y + 27))
    manifest.append({'label': name, 'path': rel,
        'sha256': hashlib.sha256((validation / rel).read_bytes()).hexdigest()})
args.output.parent.mkdir(parents=True, exist_ok=True)
im.save(args.output)
(validation / 'preview-manifest.json').write_text(json.dumps({
    'result': 'PASS', 'romSha256': candidate, 'panels': manifest,
    'method': 'Unmodified native pixels, integer 2x nearest-neighbor scaling.'
}, indent=2) + '\n')
print(args.output)
