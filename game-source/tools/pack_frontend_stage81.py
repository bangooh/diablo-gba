"""Original Diablo menu focus, portraits and base-game credits; no ROM build."""
from pathlib import Path
import argparse,struct,re,json,hashlib
from PIL import Image
from lz77 import decompress,indexed,compress
R=Path(__file__).resolve().parents[1];S=R/'source_assets/frontend_stage81';G=R/'generated'
# Existing port credits are project text, independent of Blizzard's credits.
PORT_CREDITS=[('',253),('VERy very speacial thanks to',253),('DevilutionX',252),('Team mupung',252),('',253),('VERy very very speacial thanks to',253),('Camilla',252),('Marie stella',252),('',253),('GBA PORTING',253),('Bangoo',252)]
def build(mpq=None):
 if mpq:
  import mpyq
  from extract_assets import read_mpq
  a=mpyq.MPQArchive(str(mpq),listfile=False)
  for n in ['focus42.pcx','credits.pcx']:(S/n).write_bytes(read_mpq(a,'ui_art\\'+n))
 pal=struct.unpack('<256H',decompress((G/'front_palette.lz').read_bytes()));rgb=[tuple(((c>>s)&31)*255//31 for s in (0,5,10)) for c in pal]
 q=Image.new('P',(1,1));q.putpalette([v for c in rgb for v in c])
 sheet=Image.open(S/'focus42.pcx');h=sheet.height//8;frames=[]
 for n in range(8):
  im=sheet.crop((0,n*h,sheet.width,(n+1)*h));mask=Image.frombytes('L',im.size,bytes(0 if v==250 else 255 for v in im.tobytes()))
  im=im.convert('RGBA');im.putalpha(mask);im=im.resize((10,10),Image.Resampling.LANCZOS)
  data=[]
  for p in im.getdata():data.append(0 if p[3]<96 else min(range(1,252),key=lambda i:sum((rgb[i][k]-p[k])**2 for k in range(3))))
  frames.append(data)
 # Original backdrop/portrait rectangles at640x480, scaled to240x160.
 bg=Image.open(R/'source_assets/frontend/selhero.pcx').convert('RGB').resize((240,160),Image.Resampling.LANCZOS)
 portraits=Image.open(R/'source_assets/frontend/heros.pcx').convert('RGB');choices=[]
 for n in range(4):
  im=bg.copy();im.paste(portraits.crop((0,n*76,180,(n+1)*76)).resize((67,26),Image.Resampling.LANCZOS),(12,70));choices.append(im.quantize(palette=q,dither=Image.Dither.NONE).tobytes())
 (G/'hero_choices.lz').write_bytes(indexed(b''.join(choices),38400))
 im=Image.open(S/'credits.pcx').convert('RGB').resize((240,160),Image.Resampling.LANCZOS).quantize(palette=q,dither=Image.Dither.NONE)
 # Reserve252 for white credit lettering; preserve reserved-black pixels as black0.
 pix=bytes(0 if v>=252 else v for v in im.tobytes());(G/'front_credits.lz').write_bytes(compress(pix))
 font=(R/'source_assets/exocet/exocet-8x8-1bpp.bin').read_bytes()
 def width(c):
  b=0
  for v in font[(ord(c)-32)*8:(ord(c)-31)*8]:b|=v
  return max((i for i in range(8) if b&(128>>i)),default=2)-min((i for i in range(8) if b&(128>>i)),default=0)+2 if b else 4
 # CreditLines starts with the original Diablo credits. Hellfire begins at
 # Synergistic Software; neither expansion nor later port credits are invented.
 source=(S/'credits_lines.cpp').read_text().split('const char *const CreditLines[] = {',1)[1].split('Synergistic Software',1)[0]
 lines=[];original=[]
 for line in source.splitlines():
  m=re.search(r'"((?:\\.|[^"\\])*)"',line)
  if not m:continue
  t=json.loads('"'+m[1]+'"',strict=False);original.append(t);color=252 if t.startswith('\t') else 253;t=t.strip();words=t.split();wrapped=''
  for word in words:
   candidate=wrapped+' '+word if wrapped else word
   if sum(width(c) for c in candidate)>218 and wrapped:lines.append((wrapped,color));wrapped=word
   else:wrapped=candidate
  lines.append((wrapped,color))
 while lines and not lines[-1][0]:lines.pop()
 header='#pragma once\n/* Generated from ui_art/focus42.pcx and original Diablo CreditLines. */\nextern const uint8_t asset_front_credits[];\n#define FRONT_FOCUS_WIDTH 10\n#define FRONT_FOCUS_HEIGHT 10\nstatic const uint8_t front_focus81[8][100]={\n'+',\n'.join('{'+','.join(map(str,f))+'}' for f in frames)+'\n};\n'
 header+='typedef struct {const char *text;uint8_t color;} FrontCreditLine;\nstatic const FrontCreditLine front_credits81[]={\n'+',\n'.join('{'+json.dumps(t)+','+str(c)+'}' for t,c in lines)+',\n'+''.join(' {'+json.dumps(t)+','+str(c)+'},\n' for t,c in PORT_CREDITS)+'};\n#define FRONT_CREDIT_LINES (sizeof(front_credits81)/sizeof(front_credits81[0]))\n'
 (G/'frontend_stage81.h').write_text(header)
 (S/'diablo-credits-original.txt').write_text('\n'.join(original).strip()+'\n')
 report={'focus_source':'ui_art/focus42.pcx','frames':8,'size':[10,10],'original_credit_lines':len(original),'wrapped_credit_lines':len(lines),'hero_frames':4,'source_hashes':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in S.iterdir() if p.is_file()}}
 (G/'frontend-stage81-assets.json').write_text(json.dumps(report,indent=2)+'\n');print({k:v for k,v in report.items() if k!='source_hashes'})
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--mpq');a=p.parse_args();build(a.mpq)
