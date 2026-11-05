"""Nintendo 0x10 streams and independently indexed blocks.
0x11 here is this project's sparse 64x64 actor wrapper, NOT Nintendo LZ11.
"""
from collections import defaultdict,deque
import struct

def compress(data):
 out=bytearray([0x10])+len(data).to_bytes(3,'little');positions=defaultdict(deque);p=0
 while p<len(data):
  flagpos=len(out);out.append(0);flags=0
  for bit in range(8):
   if p>=len(data):break
   best=0;distance=0;key=data[p:p+3];q=positions[key]
   while q and q[0]<p-4096:q.popleft()
   for candidate in reversed(q):
    n=3
    while n<18 and p+n<len(data) and data[candidate+n]==data[p+n]:n+=1
    if n>best:best=n;distance=p-candidate
    if n==18:break
   if best>=3:
    flags|=128>>bit;token=((best-3)<<12)|(distance-1);out.extend(token.to_bytes(2,'big'));count=best
   else:out.append(data[p]);count=1
   for i in range(count):
    if p+i+3<=len(data):positions[data[p+i:p+i+3]].append(p+i)
   p+=count
  out[flagpos]=flags
 return bytes(out)
def decompress(data):
 if data[0]==17:
  mask=int.from_bytes(data[1:9],'little');packed=decompress(data[9:]);out=bytearray();p=0
  for tile in range(64):
   if mask&(1<<tile):out.extend(packed[p:p+32]);p+=32
   else:out.extend(bytes(32))
  assert p==len(packed)
  return bytes(out)
 assert data[0]==16
 size=int.from_bytes(data[1:4],'little');out=bytearray();p=4
 while len(out)<size:
  flags=data[p];p+=1
  for bit in range(8):
   if len(out)>=size:break
   if flags&(128>>bit):
    token=int.from_bytes(data[p:p+2],'big');p+=2;distance=(token&4095)+1
    for _ in range((token>>12)+3):out.append(out[-distance])
   else:out.append(data[p]);p+=1
 return bytes(out)
def indexed(data,size):
 assert len(data)%size==0
 encoder=compress_actor if size==2048 else compress
 chunks=[encoder(data[i:i+size]) for i in range(0,len(data),size)]
 return indexed_chunks(chunks)
def compress_actor(data):
 assert len(data)==2048
 mask=0;packed=bytearray()
 for i in range(64):
  tile=data[i*32:i*32+32]
  if any(tile):mask|=1<<i;packed.extend(tile)
 compact=b'\x11'+mask.to_bytes(8,'little')+compress(bytes(packed))
 plain=compress(data)
 return compact if len(compact)<len(plain) else plain
def indexed_chunks(chunks):
 # Runtime reads independent offsets; repeated frames need no second payload.
 offsets=[];payload=bytearray();seen={};base=4+4*len(chunks)
 for c in chunks:
  if c not in seen:seen[c]=base+len(payload);payload.extend(c)
  offsets.append(seen[c])
 return struct.pack('<I',len(chunks))+struct.pack('<'+str(len(offsets))+'I',*offsets)+payload
