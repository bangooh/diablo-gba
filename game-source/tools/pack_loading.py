"""Extract native loading art; resize to GBA Mode4 and retain its own palette."""
from pathlib import Path
import sys,struct
from PIL import Image
from lz77 import compress
import mpyq
from extract_assets import read_mpq
archive=mpyq.MPQArchive(sys.argv[1],listfile=False)
root=Path(__file__).resolve().parents[1];src=root/'source_assets/loading';src.mkdir(exist_ok=True)
for name in ['Cuttt','Cutl1d','Cut2','Cut3','Cut4']:
 raw=read_mpq(archive,'Gendata\\'+name+'.CEL');pal=read_mpq(archive,'Gendata\\'+name+'.pal')
 (src/(name+'.CEL')).write_bytes(raw);(src/(name+'.pal')).write_bytes(pal)
 count=struct.unpack_from('<I',raw)[0];offsets=struct.unpack_from('<'+str(count+1)+'I',raw,4)
 data=raw[offsets[0]:offsets[1]];pixels=bytearray();p=0
 while p<len(data):
  run=data[p];p+=1
  if run>=128:pixels.extend(bytes(256-run))
  else:pixels.extend(data[p:p+run]);p+=run
 assert len(pixels)==640*480,(name,len(pixels))
 im=Image.frombytes('P',(640,480),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM);im.putpalette(pal)
 im=im.resize((240,160),Image.Resampling.NEAREST);im.save(src/(name+'.png'))
 (root/'generated'/('loading_'+name.lower()+'.bin')).write_bytes(im.tobytes())
 colors=[(pal[i]>>3)|((pal[i+1]>>3)<<5)|((pal[i+2]>>3)<<10) for i in range(0,768,3)]
 (root/'generated'/('loading_'+name.lower()+'_palette.bin')).write_bytes(struct.pack('<256H',*colors))
 for suffix in ['', '_palette']:
  target=root/'generated'/('loading_'+name.lower()+suffix);target.with_suffix('.lz').write_bytes(compress(target.with_suffix('.bin').read_bytes()))
 print(name,count,im.size)
