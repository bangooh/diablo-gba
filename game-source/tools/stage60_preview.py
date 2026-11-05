"""Assemble verified native captures; do not recolor or retouch game pixels."""
from pathlib import Path
import sys
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[1]
proof = root / 'generated/stage60-validation'
output = Path(sys.argv[1]) if len(sys.argv)>1 else proof/'stage60-preview.png'
font = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
title = ImageFont.truetype(font, 27)
small = ImageFont.truetype(font, 22)
canvas = Image.new('RGB', (1984, 928), '#171a20')
draw = ImageDraw.Draw(canvas)
for x, path, caption in (
    (24, proof/'dialogue/native/wounded-spacing.png', 'DIALOGUE / 1px character gap + 3px line gap'),
    (1000, proof/'doors/native/open.png', 'OPEN DOOR / Original arch restored + minimap B')):
    screen = Image.open(path).convert('RGB')
    assert screen.size == (240,160)
    draw.text((x,20), caption, font=title, fill='#eeeeef')
    canvas.paste(screen.resize((960,640),Image.Resampling.NEAREST),(x,64))
draw.text((24,735), 'SPELL SOURCE COLORS / Native ROM captures', font=title, fill='#eeeeef')
for i, (name, label, detail) in enumerate((
    ('learned-blue','LEARNED SPELL','Blue'),
    ('scroll-red','SCROLL','Original muted red'),
    ('staff-orange','STAFF','Orange'),
    ('mana-empty-gray','UNAVAILABLE','Gray'))):
    screen = Image.open(proof/'spell-colors/native'/f'{name}.png').convert('RGB')
    icon = screen.crop((212,102,236,126)).resize((96,96),Image.Resampling.NEAREST)
    x = 24+i*488
    canvas.paste(icon,(x,790))
    draw.text((x+114,805),label,font=title,fill='#eeeeef')
    draw.text((x+114,846),detail,font=small,fill='#b8bdc4')
output.parent.mkdir(parents=True,exist_ok=True)
canvas.save(output)
print(output)
