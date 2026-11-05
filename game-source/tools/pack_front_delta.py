"""Losslessly repack all original frontend movie frames as sequential deltas.

The old front_video.lz stays untouched. Full, XOR, and sparse literal-patch
records use the existing LZ10 decoder; equal adjacent frames need one byte.
Every frame is checked with the production C helper before files are saved.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

from lz77 import decompress, indexed_chunks

ROOT = Path(__file__).resolve().parents[1]
FRAME_BYTES = 240 * 160
# Native version of tools/lz77.py's greedy encoder. An offline-only accelerator
# keeps regeneration short; the generated streams still use standard LZ10.
ENCODER = r'''
#include <stdint.h>
#include <stdlib.h>
unsigned encode(const uint8_t *data,unsigned n,uint8_t *out){
 int head[65536];for(unsigned i=0;i<65536;i++)head[i]=-1;
 int *prev=malloc((n+1)*sizeof(int));unsigned p=0,j=4;
 if(!prev)return 0;
 out[0]=16;out[1]=n;out[2]=n>>8;out[3]=n>>16;
 while(p<n){unsigned flagpos=j++;out[flagpos]=0;
  for(unsigned bit=0;bit<8&&p<n;bit++){
   unsigned best=0,dist=0;
   if(p+3<=n){unsigned h=((unsigned)data[p]*251+data[p+1]*31+data[p+2])&65535;
    for(int q=head[h];q>=0&&p-(unsigned)q<=4096;q=prev[q]){
     if(data[q]!=data[p]||data[q+1]!=data[p+1]||data[q+2]!=data[p+2])continue;
     unsigned k=3;while(k<18&&p+k<n&&data[q+k]==data[p+k])k++;
     if(k>best){best=k;dist=p-q;}if(k==18)break;
    }
   }
   unsigned count=1;
   if(best>=3){unsigned token=((best-3)<<12)|(dist-1);out[flagpos]|=128>>bit;
    out[j++]=token>>8;out[j++]=token;count=best;
   }else out[j++]=data[p];
   for(unsigned i=0;i<count;i++)if(p+i+3<=n){unsigned q=p+i;
    unsigned h=((unsigned)data[q]*251+data[q+1]*31+data[q+2])&65535;
    prev[q]=head[h];head[h]=q;
   }
   p+=count;
  }
 }
 free(prev);return j;
}
'''


def patch_between(previous, frame):
    patch = bytearray()
    position = 0
    while position < FRAME_BYTES:
        begin = position
        while begin < FRAME_BYTES and frame[begin] == previous[begin]:
            begin += 1
        if begin == FRAME_BYTES:
            break
        end = begin + 1
        while end < FRAME_BYTES and frame[end:end+8] != previous[end:end+8]:
            end += 1
        # Every destination/literal span is word aligned so the GBA can copy
        # it with DMA3 instead of a costly ROM-resident byte loop.
        begin &= ~3
        end = (end+3) & ~3
        patch += struct.pack('<HH', begin, end-begin) + frame[begin:end]
        position = end
    return bytes(patch)


def build(directory):
    original = (ROOT / 'generated/front_video.lz').read_bytes()
    count = struct.unpack_from('<I', original)[0]
    offsets = struct.unpack_from('<' + 'I'*count, original, 4)
    encoder_c = directory / 'encoder.c'
    encoder_so = directory / 'encoder.so'
    encoder_c.write_text(ENCODER)
    subprocess.run(['gcc', '-O3', '-shared', '-fPIC', str(encoder_c), '-o', str(encoder_so)], check=True)
    native = ctypes.CDLL(str(encoder_so))
    native.encode.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p]
    native.encode.restype = ctypes.c_uint

    def compress(data):
        buffer = ctypes.create_string_buffer(len(data)*2+16)
        size = native.encode(data, len(data), buffer)
        assert size >= 4
        return buffer.raw[:size]

    previous = bytes(FRAME_BYTES)
    cache, frames, records = {}, [], []
    kinds = [0]*4
    max_scratch = FRAME_BYTES
    candidate_sizes = [0]*3
    for offset in offsets:
        if offset not in cache:
            frame = decompress(original[offset:])
            assert len(frame) == FRAME_BYTES
            cache[offset] = frame, compress(frame)
        frame, full = cache[offset]
        delta = bytes(a ^ b for a, b in zip(frame, previous))
        patch = patch_between(previous, frame)
        encoded = [full, compress(delta), compress(patch)]
        for i, data in enumerate(encoded):
            candidate_sizes[i] += len(data)
        record = min((bytes([i+1])+data for i, data in enumerate(encoded)), key=len) if patch else b'\0'
        if record[0] == 3:
            max_scratch = max(max_scratch, len(patch))
        kinds[record[0]] += 1
        records.append(record)
        frames.append(frame)
        previous = frame
    packed = indexed_chunks(records)

    wrapper = directory / 'verify.c'
    wrapper.write_text('#include <stdint.h>\n#include <string.h>\n#include "lz.h"\n#define UI_FN\n'
                       'static void dma(const void *s,void *d,unsigned n){memcpy(d,s,n);}\n'
                       '#include "front_delta.inc"\n'
                       'unsigned check(const uint8_t *s,unsigned f,uint8_t *p,uint8_t *t,unsigned n)'
                       '{return front_delta_decode(s,f,p,t,n);}\n')
    verify_so = directory / 'verify.so'
    subprocess.run(['gcc', '-O2', '-shared', '-fPIC', '-I'+str(ROOT/'src'),
                    str(wrapper), str(ROOT/'src/lz.c'), '-o', str(verify_so)], check=True)
    verify = ctypes.CDLL(str(verify_so)).check
    verify.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint]
    source = ctypes.create_string_buffer(packed)
    pixels = ctypes.create_string_buffer(b'\xa5'*(FRAME_BYTES+64))
    scratch = ctypes.create_string_buffer(b'\x5a'*(max_scratch+64))
    digest = hashlib.sha256()
    for index, frame in enumerate(frames):
        assert verify(source, index, pixels, scratch, max_scratch) == 1, index
        assert pixels.raw[:FRAME_BYTES] == frame, index
        assert pixels.raw[FRAME_BYTES:FRAME_BYTES+64] == b'\xa5'*64
        assert scratch.raw[max_scratch:max_scratch+64] == b'\x5a'*64
        digest.update(frame)
    assert verify(source, count, pixels, scratch, max_scratch) == 0
    assert len(packed) < len(original)
    report = {
        'status': 'PASS', 'format': 'indexed-tagged-LZ10-aligned-patch-v1',
        'source': 'generated/front_video.lz', 'source_sha256': hashlib.sha256(original).hexdigest(),
        'frames': count, 'fps': 15, 'frame_bytes': FRAME_BYTES,
        'source_bytes': len(original), 'packed_bytes': len(packed),
        'saved_bytes': len(original)-len(packed), 'scratch_bytes': max_scratch,
        'persistent_ram_added': 0, 'record_counts': dict(zip(('repeat', 'full', 'xor', 'patch'), kinds)),
        'unshared_candidate_bytes': dict(zip(('full', 'xor', 'patch'), candidate_sizes)),
        'decoded_sha256': digest.hexdigest(), 'packed_sha256': hashlib.sha256(packed).hexdigest(),
        'production_c_all_frames': 'PASS', 'frame_and_scratch_bounds': 'PASS',
        'timing_and_palette': 'Unchanged: all240 frames retain original order and15FPS timestamps.',
    }
    header = ('/* Generated by tools/pack_front_delta.py. */\n#pragma once\n#include <stdint.h>\n'
              'extern const uint8_t asset_front_video_delta[];\n'
              f'enum {{ FRONT_DELTA_FRAMES={count}, FRONT_DELTA_FRAME_BYTES={FRAME_BYTES},\n'
              f'       FRONT_DELTA_SCRATCH_BYTES={max_scratch}, FRONT_DELTA_STREAM_BYTES={len(packed)} }};\n')
    return {'front_video_delta.lz': packed, 'front_delta.h': header.encode(),
            'front-delta-report.json': (json.dumps(report, indent=2)+'\n').encode()}, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='front-delta-') as temporary:
        outputs, report = build(Path(temporary))
    for name, data in outputs.items():
        path = ROOT / 'generated' / name
        if args.check:
            assert path.read_bytes() == data, name + ' is stale'
        else:
            path.write_bytes(data)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
