"""Rebuild static frontend screens and focus from MPQ, without video or fonts."""
from pathlib import Path
import sys,struct,json,ctypes,subprocess,tempfile,os
from PIL import Image
import mpyq
from extract_assets import read_mpq
from lz77 import compress,decompress,indexed_chunks,indexed
r=Path(__file__).resolve().parents[1];src=r/'source_assets/frontend';src.mkdir(parents=True,exist_ok=True);out=r/'generated'
a=mpyq.MPQArchive(sys.argv[1],listfile=False)
for name in ['title','mainmenu','logo','smlogo','focus42','selhero','heros','credits']:
 (src/(name+'.pcx')).write_bytes(read_mpq(a,'ui_art\\'+name+'.pcx'))
a.file.close()
def pack(name,frames):
 # Release screens use the byte-cost LZ10 parser, not the historical greedy one.
 with tempfile.TemporaryDirectory() as temp:
  lib=Path(temp)/'optimal.so'
  subprocess.run([os.environ.get('CC','cc'),'-O2','-shared','-fPIC',str(r/'tools/lz10_optimal.c'),'-o',str(lib)],check=True)
  dll=ctypes.CDLL(str(lib));encoder=dll.lz10_optimal
  encoder.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p];encoder.restype=ctypes.c_size_t
  blobs=[]
  for raw in frames:
   buffer=ctypes.create_string_buffer(len(raw)*2+16)
   size=encoder(raw,len(raw),buffer);blobs.append(buffer.raw[:size])
 for raw,z in zip(frames,blobs):assert decompress(z)==raw
 packed=indexed_chunks(blobs)
 (out/(name+'.lz')).write_bytes(packed)
 return len(packed)
def palette(frames,count=252):
 atlas=Image.new('RGB',(240,160*len(frames)))
 for i,im in enumerate(frames):atlas.paste(im,(0,160*i))
 p=atlas.quantize(colors=count,method=Image.Quantize.MEDIANCUT).getpalette()[:count*3]
 p+=bytes([0]*(768-len(p))) if isinstance(p,bytes) else [0]*(768-len(p))
 p[252*3:]=[0,0,0,220,180,105,255,232,168,98,91,76]
 im=Image.new('P',(1,1));im.putpalette(p);return im,p
def palbytes(p):return struct.pack('<256H',*[(p[i]>>3)|((p[i+1]>>3)<<5)|((p[i+2]>>3)<<10) for i in range(0,768,3)])
frames=[]
for bg,logo,rect in [('title','logo',(17,55,206,81)),('mainmenu','smlogo',(47,6,146,58))]:
 background=Image.open(src/(bg+'.pcx')).convert('RGB').resize((240,160),Image.Resampling.LANCZOS)
 sheet=Image.open(src/(logo+'.pcx'));fh=sheet.height//15
 for i in range(15):
  im=background.copy();part=sheet.crop((0,i*fh,sheet.width,(i+1)*fh));mask=Image.frombytes('L',part.size,bytes(0 if v==250 else 255 for v in part.tobytes()))
  x,y,w,h=rect;part=part.convert('RGBA');part.putalpha(mask);part=part.resize((w,h),Image.Resampling.LANCZOS)
  im.paste(part,(x,y),part);frames.append(im)
 # Title's original copyright is too small at GBA resolution; retained in art.
pal,p=palette(frames);pack('front_screens',[im.quantize(palette=pal,dither=Image.Dither.NONE).tobytes() for im in frames]);(out/'front_palette.lz').write_bytes(compress(palbytes(p)))
for i,n in [(0,'title'),(15,'menu')]:frames[i].save(src/(n+'-preview.png'))

# Current compact 10x10 selector, independent of credit text/font generation.
pal=struct.unpack('<256H',decompress((out/'front_palette.lz').read_bytes()))
rgb=[tuple(((c>>s)&31)*255//31 for s in (0,5,10)) for c in pal]
sheet=Image.open(src/'focus42.pcx');height=sheet.height//8;focus=[]
for n in range(8):
 im=sheet.crop((0,n*height,sheet.width,(n+1)*height))
 mask=Image.frombytes('L',im.size,bytes(0 if v==250 else 255 for v in im.tobytes()))
 im=im.convert('RGBA');im.putalpha(mask);im=im.resize((10,10),Image.Resampling.LANCZOS)
 focus.append([0 if p[3]<96 else min(range(1,252),key=lambda i:sum((rgb[i][k]-p[k])**2 for k in range(3))) for p in im.getdata()])
(out/'frontend_focus.h').write_text('/* MPQ focus42 selector; no credit/font dependency. */\nstatic const uint8_t front_focus81[8][100]={'+','.join('{'+','.join(map(str,f))+'}' for f in focus)+'};\n')

q=Image.new('P',(1,1));q.putpalette([v for c in rgb for v in c])
bg=Image.open(src/'selhero.pcx').convert('RGB').resize((240,160),Image.Resampling.LANCZOS)
portraits=Image.open(src/'heros.pcx').convert('RGB');choices=[]
for n in range(4):
 im=bg.copy();im.paste(portraits.crop((0,n*76,180,(n+1)*76)).resize((67,26),Image.Resampling.LANCZOS),(12,70));choices.append(im.quantize(palette=q,dither=Image.Dither.NONE).tobytes())
(out/'hero_choices.lz').write_bytes(indexed(b''.join(choices),38400))
im=Image.open(src/'credits.pcx').convert('RGB').resize((240,160),Image.Resampling.LANCZOS).quantize(palette=q,dither=Image.Dither.NONE)
(out/'front_credits.lz').write_bytes(compress(bytes(0 if v>=252 else v for v in im.tobytes())))
