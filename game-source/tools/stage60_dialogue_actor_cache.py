#!/usr/bin/env python3
"""Compare native post-dialogue actor caches against a fresh ROM-frame decode."""
import argparse,base64,ctypes,json,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('rom',type=Path);p.add_argument('snapshots',type=Path);a=p.parse_args();root=Path(__file__).resolve().parents[1];r=json.loads(a.snapshots.read_text());rom=ctypes.create_string_buffer(a.rom.read_bytes())
with tempfile.TemporaryDirectory(prefix='dialogue-cache-') as d:
 lib=Path(d)/'decode.so';subprocess.run(['gcc','-shared','-fPIC','-O2',str(root/'src/actor_frames.c'),str(root/'src/lz.c'),'-o',str(lib)],check=True);decoder=ctypes.CDLL(str(lib));decoder.actor_frame.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_void_p];decoder.actor_frame.restype=ctypes.c_uint
 for row in r['records']:
  dst=(ctypes.c_uint32*514)();scratch=(ctypes.c_uint32*514)();dst[0]=dst[513]=0xabcdef98;scratch[0]=scratch[513]=0x76543210
  n=decoder.actor_frame(ctypes.byref(rom,row['asset']-0x08000000),row['index'],ctypes.byref(dst,4),2048,ctypes.byref(scratch,4),ctypes.byref(rom,r['pool']-0x08000000));assert n==2048;assert dst[0]==dst[513]==0xabcdef98 and scratch[0]==scratch[513]==0x76543210
  assert ctypes.string_at(ctypes.byref(dst,4),2048)==base64.b64decode(row['pixels']),(row['asset'],row['index'],row['slot'])
print(json.dumps({'result':'PASS','verified_native_cache_frames':len(r['records']),'all_four_slots':sorted(set(x['slot'] for x in r['records']))}))
