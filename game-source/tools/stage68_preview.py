"""Compose actual emulator screenshots and measured figures; no generated art."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import json
r=Path(__file__).resolve().parents[1];v=r/'generated/stage68-validation'
f=lambda n:ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',n)
im=Image.new('RGB',(1024,536),(18,21,27));d=ImageDraw.Draw(im)
d.text((24,18),'STAGE 68  /  MAGIC LIGHTING COMPARISON',font=f(24),fill='#efede6')
d.text((24,55),'16 lightning flight states • minimap ON • actual ROM captures',font=f(17),fill='#aeb9ca')
for x,folder,title in [(24,'lights-two-preview','ON — two representative lights per cast'),(520,'native','OFF — player and environment lights kept')]:
 report=json.loads((v/folder/'report.json').read_text());row=next(t for t in report['performance'] if t.get('requestedBolts')==16)
 d.text((x,97),title,font=f(16),fill='#eeeeee')
 shot=Image.open(v/folder/'stage68-flight-16.png').convert('RGB').resize((480,320),Image.Resampling.NEAREST);im.paste(shot,(x,125))
 d.text((x,458),f"Average {row['averageFps']:.2f} FPS  /  Lowest 1s {row['lowestOneSecondFps']:.2f}",font=f(18),fill='#f1c86b')
d.text((24,503),'Same seeded fixture; captures are not frame-matched. Hardware-emulated timing, not a physical GBA test.',font=f(14),fill='#aeb9ca')
im.save(v/'stage68-lighting-comparison.png')
