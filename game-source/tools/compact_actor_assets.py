"""Losslessly compact existing indexed 2048-byte frames; no MPQ/ROM needed.
New indexed(..., 2048) producers already use the same sparse-tile codec.
"""
from pathlib import Path
import json,struct,hashlib
from lz77 import decompress,indexed
r=Path(__file__).resolve().parents[1];report=[]
for path in sorted((r/'generated').glob('*.lz')):
    raw=path.read_bytes();count=int.from_bytes(raw[:4],'little')
    if not 0<count<50000 or len(raw)<4+count*4:continue
    offset=int.from_bytes(raw[4:8],'little')
    if offset<4+count*4 or offset>=len(raw):continue
    if raw[offset]!=16 or int.from_bytes(raw[offset+1:offset+4],'little')!=2048:continue
    frames=[]
    for i in range(count):
        start=struct.unpack_from('<I',raw,4+i*4)[0];frame=decompress(raw[start:]);assert len(frame)==2048
        frames.append(frame)
    pixels=b''.join(frames);packed=indexed(pixels,2048)
    for i,expected in enumerate(frames):
        start=struct.unpack_from('<I',packed,4+i*4)[0];assert decompress(packed[start:])==expected
    if len(packed)<len(raw):
        path.write_bytes(packed);report.append({'asset':path.stem,'frames':count,'before':len(raw),'after':len(packed),'decoded_sha256':hashlib.sha256(pixels).hexdigest()})
(r/'generated/compact-actors-report.json').write_text(json.dumps(report,indent=2)+'\n')
print('compacted',len(report),'clips; saved',sum(x['before']-x['after'] for x in report),'bytes',flush=True)
