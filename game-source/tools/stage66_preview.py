"""Contact sheet of exact native ROM captures and decoded original assets."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
r=Path(__file__).resolve().parents[1];g=r/'generated';n=g/'stage66-validation/native';a=g/'stage66-validation/assets'
out=Image.new('RGB',(1024,968),(18,19,24));d=ImageDraw.Draw(out)
font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',20)
small=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',15)
d.text((28,20),'STAGE 66  /  ORIGINAL ASSETS',font=font,fill=(233,222,201))
d.text((28,58),'Spell backgrounds: one palette index, four colors',font=small,fill=(165,170,181))
for i,(key,label) in enumerate([('learned-blue','Learned'),('scroll-red','Scroll'),('staff-orange','Staff'),('unavailable-gray','Unavailable')]):
 im=Image.open(n/('spell-'+key+'.png')).convert('RGB').crop((212,102,236,126)).resize((72,72),Image.Resampling.NEAREST)
 x=30+i*140;out.paste(im,(x,92));d.text((x,177),label,font=small,fill='white')
d.text((626,58),'Belt: normal / full potion',font=small,fill=(165,170,181))
im=Image.open(n/'belt-normal-full.png').convert('RGB').crop((80,152,128,160)).resize((336,56),Image.Resampling.NEAREST);out.paste(im,(626,103))
d.text((626,177),'Heal           Mana           Rejuvenation',font=small,fill='white')
d.text((28,225),'Re-extracted ground-item artwork',font=font,fill=(233,222,201))
ground=Image.open(a/'ground-item-preview.png').convert('RGB')
out.paste(ground.resize((968,415),Image.Resampling.NEAREST),(28,265))
d.text((28,715),'Six additional native monsters',font=font,fill=(233,222,201))
out.paste(Image.open(a/'monster-preview.png'),(32,748))
out.save(g/'stage66-preview.png')
