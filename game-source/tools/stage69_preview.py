"""Actual native mGBA captures; nearest-neighbour display enlargement only."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
r=Path(__file__).resolve().parents[1];v=r/'generated/stage69-validation';n=v/'native'
f=lambda s:ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',s)
def canvas(title,subtitle,height):
 im=Image.new('RGB',(1024,height),'#12151b');d=ImageDraw.Draw(im);d.text((24,16),title,font=f(25),fill='#efede6');d.text((24,54),subtitle,font=f(16),fill='#b3bfd0');return im,d
def panel(im,d,x,y,name,title):
 d.text((x,y),title,font=f(18),fill='#f1c86b');im.paste(Image.open(n/(name+'.png')).convert('RGB').resize((480,320),Image.Resampling.NEAREST),(x,y+30))
im,d=canvas('STAGE 69 / SPELL GRAPHICS','Original CL2 geometry restored within a 32px display cap. Actual ROM captures.',866)
for x,y,name,title in [(24,90,'stage68-spell-18','BEFORE / Nova'),(520,90,'stage69-spell-18','AFTER / Nova'),(24,480,'stage68-flight-8','BEFORE / 8 lightning segments'),(520,480,'stage69-flight-8','AFTER / 8 lightning segments')]:panel(im,d,x,y,name,title)
d.text((24,841),'Same seeded fixtures; captures are not frame-matched. Effects remain 4 sampled frames.',font=f(14),fill='#b3bfd0');im.save(v/'stage69-spell-comparison.png')
im,d=canvas('STAGE 69 / INVENTORY + SPELL SHORTCUTS','Controller navigation, exact item placement and original casting families.',1170)
for x,y,name,title in [(24,90,'inventory-carry','A: pick/place   D-pad: move   B: cancel'),(520,90,'spell-picker-assigned','Select: spells   L/R: slot   A: assign'),(24,480,'inventory-belt','Potion placed into the chosen belt slot'),(520,480,'mana-shield','Mana Shield / above the head')]:panel(im,d,x,y,name,title)
for i,(name,title) in enumerate([('fm','Fire / FM'),('lm','Lightning / LM'),('qm','Magic / QM (Nova)')]):
 x=24+i*330;d.text((x,863),title,font=f(18),fill='#f1c86b');shot=Image.open(n/('caster-'+name+'.png')).convert('RGB');shot=shot.crop((64,4,176,112)).resize((280,270),Image.Resampling.NEAREST);im.paste(shot,(x,894))
im.save(v/'stage69-ui-preview.png')
