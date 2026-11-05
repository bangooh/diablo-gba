"""Pack town PCM losslessly into independently seekable 256-sample blocks.

The little-endian offset table contains blocks+1 absolute uint32 offsets. Each
block is signed minimum (one byte), bit width (one byte), and LSB-first deltas.
The final block contains only its real samples. Original DMA guard silence is
not stored: playback decodes into the existing guarded music DMA buffer.
--verify compiles the production C helper and checks every sample at every gain.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BLOCK = 256


def sha(data):
    return hashlib.sha256(data).hexdigest()


def pack(pcm):
    count = (len(pcm) + BLOCK - 1) // BLOCK
    output = bytearray(4 * (count + 1))
    widths = Counter()
    for index, start in enumerate(range(0, len(pcm), BLOCK)):
        struct.pack_into('<I', output, index * 4, len(output))
        samples = [value if value < 128 else value - 256
                   for value in pcm[start:start + BLOCK]]
        minimum = min(samples)
        bits = (max(samples) - minimum).bit_length()
        widths[bits] += 1
        output.extend((minimum & 255, bits))
        pending = available = 0
        for sample in samples:
            pending |= (sample - minimum) << available
            available += bits
            while available >= 8:
                output.append(pending & 255)
                pending >>= 8
                available -= 8
        if available:
            output.append(pending)
    struct.pack_into('<I', output, count * 4, len(output))
    return bytes(output), dict(sorted(widths.items()))


def unpack(data, length):
    """Independent bit-position reference, including exact block extent checks."""
    count = (length + BLOCK - 1) // BLOCK
    offsets = struct.unpack_from('<' + 'I' * (count + 1), data)
    assert offsets[0] == 4 * (count + 1) and offsets[-1] == len(data)
    result = bytearray()
    for index, offset in enumerate(offsets[:-1]):
        base, bits = data[offset:offset + 2]
        samples = min(BLOCK, length - index * BLOCK)
        assert 0 <= bits <= 8
        assert offsets[index + 1] == offset + 2 + (samples * bits + 7) // 8
        payload = int.from_bytes(data[offset + 2:offsets[index + 1]], 'little')
        result.extend((base + ((payload >> (i * bits)) & ((1 << bits) - 1))) & 255
                      for i in range(samples))
    return bytes(result)


HOST_CHECK = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/music_packed.inc"
#include "generated/audio_gains.h"
static uint8_t *pcm;
static uint64_t checked;
static void span(unsigned position,unsigned count,const int8_t *gain){
 uint8_t output[258];memset(output,0xa5,sizeof(output));
 music_packed_map((int8_t*)output+1,position,gain,count);
 if(output[0]!=0xa5||output[count+1]!=0xa5){fprintf(stderr,"destination guard\n");exit(1);}
 for(unsigned i=0;i<count;i++)if(output[i+1]!=(uint8_t)gain[pcm[position+i]]){
  fprintf(stderr,"PCM mismatch at %u\n",position+i);exit(1);
 }
 checked+=count;
}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 FILE *input=fopen(argv[1],"rb");if(!input)return 2;
 pcm=malloc(MUSIC_TOWN_PACKED_SAMPLES);if(!pcm)return 2;
 if(fread(pcm,1,MUSIC_TOWN_PACKED_SAMPLES,input)!=MUSIC_TOWN_PACKED_SAMPLES)return 2;
 fclose(input);
 int8_t identity[256];for(unsigned i=0;i<256;i++)identity[i]=(int8_t)i;
 for(unsigned volume=0;volume<=10;volume++){
  const int8_t *gain=volume?music_gain[volume-1]:identity;
  for(unsigned position=0;position<MUSIC_TOWN_PACKED_SAMPLES;position+=256){
   unsigned count=MUSIC_TOWN_PACKED_SAMPLES-position;if(count>256)count=256;
   span(position,count,gain);
  }
 }
 /* Every block at several unaligned bit positions, crossing the next block. */
 const unsigned starts[]={1,7,31,127,255};
 for(unsigned block=0;block<MUSIC_TOWN_PACKED_BLOCKS;block++)for(unsigned s=0;s<5;s++){
  unsigned position=block*256+starts[s];if(position>=MUSIC_TOWN_PACKED_SAMPLES)continue;
  unsigned count=MUSIC_TOWN_PACKED_SAMPLES-position;if(count>256)count=256;
  span(position,count,music_gain[(block+s)%10]);
 }
 /* Exercise the same split-at-track-end contract used by music_fill. */
 for(unsigned volume=0;volume<10;volume++)for(unsigned tail=1;tail<=256;tail++){
  span(MUSIC_TOWN_PACKED_SAMPLES-tail,tail,music_gain[volume]);
  span(0,256-tail,music_gain[volume]);
 }
 printf("PASS: all %u PCM samples, all ten gain LUTs, unaligned seeks and wrap spans; %llu samples checked\n",
        MUSIC_TOWN_PACKED_SAMPLES,(unsigned long long)checked);
 free(pcm);return 0;
}
'''


def verify_host(source, packed):
    with tempfile.TemporaryDirectory(prefix='music-town-check-') as temporary:
        work = Path(temporary)
        (work / 'check.c').write_text(HOST_CHECK)
        # Host assembly defines the same aligned asset symbol as the GBA bundle.
        (work / 'asset.s').write_text(
            '.section .rodata\n.balign 4\n.global asset_audio_music_town_packed\n'
            'asset_audio_music_town_packed:\n.incbin "' + str(packed) + '"\n'
            '.section .note.GNU-stack,"",@progbits\n')
        executable = work / 'check'
        subprocess.run(['gcc', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                        '-I', str(ROOT), str(work / 'check.c'), str(work / 'asset.s'),
                        '-o', str(executable)], check=True)
        subprocess.run([str(executable), str(source)], check=True,
                       env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0',
                            'UBSAN_OPTIONS': 'halt_on_error=1'})


def write_or_check(path, expected, check):
    if check:
        assert path.read_bytes() == expected, f'{path}: stale generated file'
    elif not path.exists() or path.read_bytes() != expected:
        path.write_bytes(expected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    generated = ROOT / 'generated'
    source = generated / 'audio_music_town.lz'
    original = source.read_bytes()
    match = re.search(r'^#define AUDIO_MUSIC_TOWN_LENGTH (\d+)\s*$',
                      (generated / 'combat_assets.h').read_text(), re.MULTILINE)
    assert match, 'missing original town sample length'
    length = int(match.group(1))
    assert 0 < length <= len(original)
    assert not any(original[length:]), 'town DMA guard must contain only silence'
    pcm = original[:length]
    packed, widths = pack(pcm)
    decoded = unpack(packed, length)
    assert decoded == pcm, 'lossless town PCM roundtrip failed'
    blocks = (length + BLOCK - 1) // BLOCK
    header = ('/* Generated by tools/pack_music_town.py; lossless town PCM. */\n'
              '#pragma once\n#include <stdint.h>\n'
              'extern const uint8_t asset_audio_music_town_packed[];\n'
              f'#define MUSIC_TOWN_PACKED_SAMPLES {length}u\n'
              f'#define MUSIC_TOWN_PACKED_BLOCKS {blocks}u\n'
              f'#define MUSIC_TOWN_PACKED_BYTES {len(packed)}u\n')
    report = {'format_version': 1, 'block_samples': BLOCK, 'samples': length,
              'blocks': blocks, 'blocks_by_bit_width': widths,
              'source_file_bytes': len(original), 'source_guard_bytes': len(original) - length,
              'packed_bytes': len(packed), 'offset_table_bytes': (blocks + 1) * 4,
              'saved_bytes': len(original) - len(packed),
              'source_sha256': sha(original), 'pcm_sha256': sha(pcm),
              'packed_sha256': sha(packed), 'decoded_sha256': sha(decoded),
              'verification': 'PASS: every playable PCM sample is bit-exact',
              'persistent_decoder_ram_bytes': 0, 'decoder_iwram_bytes': 0}
    destination = generated / 'audio_music_town_packed.lz'
    write_or_check(destination, packed, args.check)
    write_or_check(generated / 'music_town_packed.h', header.encode(), args.check)
    write_or_check(generated / 'music-town-packed-report.json',
                   (json.dumps(report, indent=2) + '\n').encode(), args.check)
    if args.verify:
        verify_host(source, destination)
    print(f'PASS: town PCM {len(original):,} -> {len(packed):,} bytes; '
          f'{len(original) - len(packed):,} bytes saved, no runtime scratch')


if __name__ == '__main__':
    main()
