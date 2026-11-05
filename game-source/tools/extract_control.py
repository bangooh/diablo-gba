import sys,struct,argparse
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import mpyq
from extract_assets import read_mpq
from PIL import Image
parser=argparse.ArgumentParser();parser.add_argument('mpq');parser.add_argument('output');args=parser.parse_args()
out=Path(args.output);out.mkdir(parents=True,exist_ok=True)
a=mpyq.MPQArchive(args.mpq,listfile=False)
pal=read_mpq(a,'levels\\towndata\\town.pal');(out/'town.pal').write_bytes(pal)
for name,width in [('Panel8',640),('P8Bulbs',88),('Panel8bu',71),('SmalText',13)]:
 raw=read_mpq(a,'CtrlPan\\'+name+'.CEL');(out/(name+'.CEL')).write_bytes(raw)
 count=struct.unpack_from('<I',raw)[0];ofs=struct.unpack_from('<'+str(count+1)+'I',raw,4)
 print(name,count)
 for n,(start,end) in enumerate(zip(ofs,ofs[1:])):
  data=raw[start:end];p=0;pixels=[]
  while p<len(data):
   run=data[p];p+=1
   if run>=128:pixels.extend([0]*(256-run))
   else:pixels.extend(data[p:p+run]);p+=run
  assert len(pixels)%width==0
  im=Image.frombytes('P',(width,len(pixels)//width),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM);im.putpalette(pal);im.save(out/f'{name}-{n}.png');print(im.size)
