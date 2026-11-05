"""Losslessly optimize existing opening records; verify all 160 decoded frames.
Usage: python tools/repack_opening.py INPUT.lz OUTPUT.lz --report REPORT.json
Uses the existing decoder and its 38400-byte scratch limit. With --delta-candidates
it also selects among existing full/XOR/sparse representations of the same pixels.
"""
from pathlib import Path
import argparse,ctypes as C,hashlib,json,struct,subprocess,tempfile
from lz77 import decompress,indexed_chunks

def records(data):
    count=struct.unpack_from('<I',data)[0]
    offsets=struct.unpack_from('<'+'I'*count,data,4)
    return [data[x:min([v for v in offsets if v>x]+[len(data)])] for x in offsets]

def frames(chunks):
    pixels=bytearray(38400)
    for c in chunks:
        kind=c[0]
        if kind:
            raw=decompress(c[1:])
            if kind==1: pixels[:]=raw
            elif kind==2: pixels[:]=bytes(x^y for x,y in zip(pixels,raw))
            elif kind==3:
                at=0
                while at<len(raw):
                    offset,n=struct.unpack_from('<HH',raw,at);at+=4
                    assert n and not ((at|offset|n)&3) and offset+n<=38400 and at+n<=len(raw)
                    pixels[offset:offset+n]=raw[at:at+n];at+=n
            else: raise ValueError(kind)
        assert len(pixels)==38400
        yield bytes(pixels)

def main():
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);p.add_argument('--report',type=Path,required=True);p.add_argument('--indexed',action='store_true',help='Ordinary independent LZ10 frames, without opening delta tags');p.add_argument('--delta-candidates',action='store_true');a=p.parse_args()
    assert not (a.indexed and a.delta_candidates)
    before=a.source.read_bytes();old=records(before)
    with tempfile.TemporaryDirectory() as td:
        lib=Path(td)/'lz.so'
        subprocess.run(['gcc','-O3','-shared','-fPIC',str(Path(__file__).with_name('lz10_optimal.c')),'-o',str(lib)],check=True)
        fn=C.CDLL(str(lib)).lz10_optimal;fn.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p];fn.restype=C.c_size_t
        def pack(raw):
            out=C.create_string_buffer(4+len(raw)*2+16)
            n=fn(raw,len(raw),out);packed=out.raw[:n]
            assert decompress(packed)==raw
            return packed
        new=[];decoded=list(frames(old)) if a.delta_candidates else None
        for index,c in enumerate(old):
            if not c[0] and not a.indexed:new.append(c);continue
            prefix=b'' if a.indexed else c[:1]
            raw=decompress(c[len(prefix):]);packed=pack(raw)
            best=prefix+packed if len(packed)+len(prefix)<len(c) else c
            if a.delta_candidates:
                now=decoded[index];previous=decoded[index-1] if index else bytes(38400)
                candidates=[(1,now),(2,bytes(x^y for x,y in zip(previous,now)))]
                changed=[at for at in range(0,38400,4) if previous[at:at+4]!=now[at:at+4]]
                for gap in [0,4,16,32,64,256]:
                    spans=[]
                    for at in changed:
                        if spans and at-spans[-1][1]<=gap:spans[-1][1]=at+4
                        else:spans.append([at,at+4])
                    sparse=b''.join(struct.pack('<HH',start,end-start)+now[start:end] for start,end in spans)
                    if sparse and len(sparse)<=38400:candidates.append((3,sparse))
                for kind,raw in candidates:
                    candidate=bytes([kind])+pack(raw)
                    if len(candidate)<len(best):best=candidate
            new.append(best)
    after=indexed_chunks(new)
    old_frames=list(map(decompress,old)) if a.indexed else list(frames(old))
    new_frames=list(map(decompress,records(after))) if a.indexed else list(frames(records(after)))
    assert old_frames==new_frames
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(after)
    report={'passed':True,'old_bytes':len(before),'new_bytes':len(after),'saved_bytes':len(before)-len(after),'frames':len(old_frames),'all_pixels_identical':True,'record_kinds_unchanged':[c[0] for c in old]==[c[0] for c in new],'source_sha256':hashlib.sha256(before).hexdigest(),'output_sha256':hashlib.sha256(after).hexdigest(),'decoded_frames_sha256':[hashlib.sha256(f).hexdigest() for f in old_frames]}
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='decoded_frames_sha256'}))
if __name__=='__main__':main()
