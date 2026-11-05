"""Losslessly repack existing 256-sample music blocks; retain seek boundaries.
Idempotent; verifies every decoded sample. Delta blocks use flag128, first
sample, signed16 minimum delta, then 255 bit-packed nonnegative deltas.
"""
from pathlib import Path
import struct,json,hashlib
root=Path(__file__).resolve().parents[1]
def decode(b):
 offsets=struct.unpack_from('<'+'I'*(int.from_bytes(b[:4],'little')//4),b);out=[]
 for a,z in zip(offsets,offsets[1:]):
  base,bits=b[a:a+2];base=base if base<128 else base-256
  if bits&128:
   bits&=127;low=struct.unpack_from('<h',b,a+2)[0];v=int.from_bytes(b[a+4:z],'little');out.append(base)
   for i in range(255):base+=low+((v>>(bits*i))&((1<<bits)-1));out.append(base)
  else:
   v=int.from_bytes(b[a+2:z],'little');out.extend(base+((v>>(bits*i))&((1<<bits)-1)) for i in range(256))
 return out
def pack(raw):
 blocks=[]
 for i in range(0,len(raw),256):
  a=raw[i:i+256];low=min(a);bits=(max(a)-low).bit_length();v=sum((x-low)<<(bits*j) for j,x in enumerate(a));block=bytes([low&255,bits])+v.to_bytes(32*bits,'little')
  d=[a[j]-a[j-1] for j in range(1,256)];low=min(d);bits=(max(d)-low).bit_length();v=sum((x-low)<<(bits*j) for j,x in enumerate(d));delta=bytes([a[0]&255,bits|128])+struct.pack('<h',low)+v.to_bytes((255*bits+7)//8,'little')
  blocks.append(delta if len(delta)<len(block) else block)
 offsets=[];offset=4*(len(blocks)+1)
 for b in blocks:offsets.append(offset);offset+=len(b)
 return struct.pack('<'+'I'*(len(blocks)+1),*offsets,offset)+b''.join(blocks)
if __name__=='__main__':
 p=root/'generated/audio_music_town_packed.lz';old=p.read_bytes();raw=decode(old);new=pack(raw);assert decode(new)==raw;p.write_bytes(new)
 h=root/'generated/music_town_packed.h';import re
 h.write_text(re.sub(r'MUSIC_TOWN_PACKED_BYTES \d+u',f'MUSIC_TOWN_PACKED_BYTES {len(new)}u',h.read_text()))
 result={'source_bytes':len(old),'packed_bytes':len(new),'saved_bytes':len(old)-len(new),'samples':len(raw),'decoded_pcm_sha256':hashlib.sha256(bytes(x&255 for x in raw)).hexdigest(),'lossless_roundtrip':True}
 (root/'generated/music_delta_check.json').write_text(json.dumps(result,indent=2)+'\n');print(result)
