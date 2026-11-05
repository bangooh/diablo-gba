"""Losslessly share repeated opening frames; preserve every frame and its order."""
from pathlib import Path
import struct,json,hashlib,ctypes,subprocess,tempfile
from lz77 import indexed_chunks
r=Path(__file__).resolve().parents[1];p=r/'generated/front_video.lz';original=p.read_bytes()
n=struct.unpack_from('<I',original)[0];offsets=struct.unpack_from('<'+str(n)+'I',original,4)
ends=dict(zip(sorted(set(offsets)),[*sorted(set(offsets))[1:],len(original)]))
chunks=[original[o:ends[o]] for o in offsets]
assert all(c[0]==16 for c in chunks)
packed=indexed_chunks(chunks)
# Independently run the production C decoder for every old and new frame.
with tempfile.TemporaryDirectory() as td:
 lib=Path(td)/'lz.so';subprocess.run(['gcc','-shared','-fPIC','-O2',str(r/'src/lz.c'),'-o',str(lib)],check=True)
 dll=ctypes.CDLL(str(lib));dll.lz_block.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint]
 before=ctypes.create_string_buffer(original);after=ctypes.create_string_buffer(packed)
 a=(ctypes.c_uint32*(38400//4))();b=(ctypes.c_uint32*(38400//4))();digest=hashlib.sha256()
 for i in range(n):
  assert dll.lz_block(before,i,a,38400)==38400 and dll.lz_block(after,i,b,38400)==38400
  assert bytes(a)==bytes(b);digest.update(bytes(a))
p.write_bytes(packed)
report={'frames':n,'before_bytes':len(original),'after_bytes':len(packed),'saved_bytes':len(original)-len(packed),'decoded_sha256':digest.hexdigest(),'production_c_decoder_all_frames':'PASS'}
(r/'generated/front-video-dedup-report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
