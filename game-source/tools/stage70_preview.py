"""Actual ROM comparisons only; no image reconstruction or generative assets."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
r=Path(__file__).resolve().parents[1];v=r/'generated/stage70-validation';n=v/'native'
f=lambda s:ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',s)
im=Image.new('RGB',(1024,1258),'#12151b');d=ImageDraw.Draw(im)
d.text((24,16),'STAGE 70 / NATIVE SPELL GRAPHICS',font=f(25),fill='#efede6')
d.text((24,54),'Direct source reduction / 16-ray Nova / no sprite enlargement',font=f(17),fill='#b3bfd0')
for row,(spell,title) in enumerate([(18,'NOVA'),(6,'FIREWALL'),(30,'CHARGED BOLT')]):
 y=90+row*385
 for col,(stage,label) in enumerate([('stage69','BEFORE'),('stage70','AFTER')]):
  x=24+496*col;d.text((x,y),title+' / '+label,font=f(18),fill='#f1c86b');shot=Image.open(n/(stage+'-spell-'+str(spell)+'.png')).convert('RGB');im.paste(shot.resize((480,320),Image.Resampling.NEAREST),(x,y+30))
d.text((24,1228),'Actual mGBA captures from the same fixture; animation phases are not frame-matched.',font=f(14),fill='#b3bfd0')
im.save(v/'stage70-spell-comparison.png')
