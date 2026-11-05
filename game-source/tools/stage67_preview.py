"""Compose actual indexed-asset renders and mGBA screenshots; no AI imagery."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'generated/stage67-validation'
font=lambda n:ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',n)
im=Image.new('RGB',(960,704),(19,22,28));d=ImageDraw.Draw(im)
d.text((24,18),'STAGE 67  /  SPELL TEXTURES + NOVA WAVE',font=font(26),fill=(239,237,226))
d.text((24,58),'Original CEL texture • native palette families • actual ROM captures below',font=font(16),fill=(165,175,191))
atlas=Image.open(ROOT/'generated/spell-icon-colors-preview.png')
ids=[1,3,6,8,13,18,21,30]
for col,(idx,name) in enumerate(zip(ids,['Firebolt','Lightning','Firewall','Stone','Guardian','Nova','Golem','Charged'])):
 x=176+col*94;d.text((x,96),name,font=font(12),fill=(190,197,210))
 for row in range(4):im.paste(atlas.crop((idx*48,row*48,idx*48+48,row*48+48)).resize((60,60),Image.Resampling.NEAREST),(x,120+row*65))
for y,label in enumerate(['Learned / blue','Scroll / red','Staff / orange','Unavailable']):d.text((24,138+y*65),label,font=font(15),fill=(224,225,227))
for x,name,label in [(0,'stage67-spell-18.png','NOVA — expanding wave'),(480,'stage67-spell-6.png','FIREWALL — separate persistent pool')]:
 d.text((x+16,392),label,font=font(17),fill=(239,237,226));im.paste(Image.open(OUT/'native'/name).resize((432,288),Image.Resampling.NEAREST),(x+24,416))
im.save(OUT/'stage67-spell-preview.png')
