"""Reuse the bounded sprite codec for King and Water's native variants.
All directional frames remain byte-identical. No decoder or extra RAM needed.
"""
from pathlib import Path
import ctypes,hashlib,json,struct,subprocess,tempfile
import numpy as np
from lz77 import compress_actor
from refine_hero_pool import encode_nibbles
r=Path(__file__).resolve().parents[1];out=r/'generated';report_path=out/'monster-pool-report.json';pool_path=out/'hero_frame_pool.lz'
original=pool_path.read_bytes();base=len(original)
if report_path.exists():
    previous=json.loads(report_path.read_text());base=previous['hero_pool_bytes'];assert hashlib.sha256(original[8:base]).hexdigest()==previous['hero_pool_payload_sha256']
records=json.loads((r/'source_assets/monster_catalog/monsters.json').read_text())['monsters'][7:];clips={};before=len(original)-base
with tempfile.TemporaryDirectory() as td:
    lib=Path(td)/'decode.so';subprocess.run(['gcc','-shared','-fPIC','-O2',str(r/'src/actor_frames.c'),str(r/'src/lz.c'),'-o',str(lib)],check=True)
    dll=ctypes.CDLL(str(lib));dll.actor_frame.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_void_p]
    dst=(ctypes.c_uint32*512)();scratch=(ctypes.c_uint32*512)();source=ctypes.create_string_buffer(original)
    for d in records:
        for state,j in [('n',0),('w',1),('a',2),('h',3),('s',5)]:
            if not d['frames'][j] or (state=='s' and d['key'] not in ('skeleton_king','carver_sword','devil_kin_spear','advocate','lazarus','diablo','zhar')):continue
            name=d['key']+'_'+state;raw=(out/(name+'.lz')).read_bytes();before+=len(raw);frames=[]
            for i in range(d['frames'][j]*8):
                assert dll.actor_frame(raw,i,dst,2048,scratch,source)==2048
                frames.append(bytes(dst))
            clips[name]={'frames':frames,'offsets':[0]*len(frames)}
pool=bytearray(original[:base]);seen={};codecs=[0]*5
for d in records:
    ordered=[clips[d['key']+'_'+s] for s in 'nwahs' if d['key']+'_'+s in clips]
    for direction in range(8):
        candidates=[];refs=[];depths=[]
        for clip in ordered:
            n=len(clip['frames'])//8
            for i in range(direction*n,(direction+1)*n):
                frame=clip['frames'][i]
                if frame in seen:clip['offsets'][i]=seen[frame];continue
                value=np.frombuffer(frame,dtype=np.uint8);key=encode_nibbles(frame);lz=compress_actor(frame)
                kind,payload=(4,key) if len(key)<len(lz) else (0,lz);depth=0;score=4+len(payload)
                if candidates:
                    values=np.array(candidates);delta=values^value;distance=np.count_nonzero(delta&15,axis=1)+np.count_nonzero(delta>>4,axis=1)
                    for j in np.argsort(distance,kind='stable')[:8]:
                        body=encode_nibbles(bytes(delta[j]));cost=8+len(body)+4*(depths[j]+1)
                        if cost<score:score=cost;kind=3;depth=depths[j]+1;payload=struct.pack('<I',refs[j])+body
                offset=len(pool);pool.extend(struct.pack('<BBH',kind,depth,len(payload))+payload);clip['offsets'][i]=offset;seen[frame]=offset;codecs[kind]+=1
                if depth<3:candidates.append(value);refs.append(offset);depths.append(depth)
struct.pack_into('<I',pool,0,len(pool));pool_path.write_bytes(pool);after=len(pool)-base;hashes={}
fixture=r/'build/water-work/monster-fixtures.bin';fixture.parent.mkdir(exist_ok=True)
with fixture.open('wb') as f:
    f.write(struct.pack('<I',len(clips)))
    for name,clip in clips.items():
        raw=struct.pack('<II',0x31525053,len(clip['frames']))+struct.pack('<'+'I'*len(clip['offsets']),*clip['offsets']);(out/(name+'.lz')).write_bytes(raw);after+=len(raw)
        pixels=b''.join(clip['frames']);hashes[name]=hashlib.sha256(pixels).hexdigest();f.write(struct.pack('<II',len(raw),len(clip['frames']))+raw+pixels)
report={'hero_pool_bytes':base,'hero_pool_payload_sha256':hashlib.sha256(pool[8:base]).hexdigest(),'monster_pool_bytes':len(pool)-base,'clip_bytes':after-(len(pool)-base),'frames_by_codec':codecs,'decoded_sha256':hashes,'lossless':True,'max_patch_depth':3,'extra_RAM_bytes':0,'monster_kinds':[d['kind'] for d in records],'input_total_bytes':before,'output_total_bytes':after,'saved_this_run':before-after}
report_path.write_text(json.dumps(report,indent=2)+'\n');print('Monster pool',after,'bytes; conversion savings',before-after,'bytes')
