"""Use nibble-wise skip/XOR commands where smaller; keep the same reference DAG.
All pixels and frame order are lossless. This refinement needs no extra RAM.
"""
from pathlib import Path
import json,struct
from lz77 import decompress

def encode_nibbles(data):
    values=[]
    for b in data:values.extend((b&15,b>>4))
    result=[];pos=0
    while pos<len(values):
        if values[pos]:result.append(values[pos]);pos+=1;continue
        end=pos+1
        while end<len(values) and not values[end]:end+=1
        count=end-pos;result.append(0)
        if count<15:result.append(count)
        elif count<271:
            value=count-15;result.extend((15,value&15,value>>4))
        else:
            value=count-271;result.extend((0,value&15,(value>>4)&15,value>>8))
        pos=end
    if len(result)&1:result.append(0)
    return bytes(result[i]|result[i+1]<<4 for i in range(0,len(result),2))

def refine_pool(root=None):
    root=Path(root or Path(__file__).resolve().parents[1]);out=root/'generated';path=out/'hero_frame_pool.lz'
    original=path.read_bytes();assert struct.unpack_from('<II',original)==(len(original),0x31465053)
    pool=bytearray(original[:8]);remap={};offset=8;codecs=[0]*5
    while offset<len(original):
        kind,depth,size=struct.unpack_from('<BBH',original,offset);payload=original[offset+4:offset+4+size];assert len(payload)==size
        if kind==0:
            pixels=decompress(payload);assert len(pixels)==2048
            packed=encode_nibbles(pixels)
            if len(packed)<len(payload):payload=packed;kind=4
        elif kind in (1,2):
            data=decompress(payload[4:]) if kind==2 else payload[4:];delta=bytearray();pos=0
            while pos<len(data):
                token=data[pos];pos+=1;n=(token&127)+1
                if token&128:delta.extend(bytes(n))
                else:delta.extend(data[pos:pos+n]);pos+=n
            assert len(delta)==2048
            packed=encode_nibbles(delta)
            if len(packed)+4<len(payload):payload=payload[:4]+packed;kind=3
        if kind in (1,2,3):payload=struct.pack('<I',remap[struct.unpack_from('<I',payload)[0]])+payload[4:]
        remap[offset]=len(pool);pool.extend(struct.pack('<BBH',kind,depth,len(payload))+payload);codecs[kind]+=1
        offset+=4+size
    assert offset==len(original)
    struct.pack_into('<I',pool,0,len(pool));path.write_bytes(pool);clip_bytes=0
    for clip in sorted(out.glob('*.lz')):
        data=clip.read_bytes()
        if data[:4]!=b'SPR1':continue
        count=struct.unpack_from('<I',data,4)[0];assert len(data)==8+4*count
        offsets=[remap[struct.unpack_from('<I',data,8+i*4)[0]] for i in range(count)]
        clip.write_bytes(data[:8]+struct.pack('<'+'I'*count,*offsets));clip_bytes+=len(data)
    report_path=out/'weapon-heroes-report.json';report=json.loads(report_path.read_text())
    report.update(packed_bytes=len(pool)+clip_bytes,frames_by_codec=codecs,
                  codec_names=['key_lz','patch_bytes','patch_bytes_lz','patch_nibbles','key_nibbles'])
    if len(original)!=len(pool):report['nibble_refinement_saved_bytes']=len(original)-len(pool)
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    print('nibble refinement saved',len(original)-len(pool),'bytes; player assets',len(pool)+clip_bytes,flush=True)

if __name__=='__main__':refine_pool()
