"""Losslessly repack the four existing 6144Hz region masters; no resampling."""
import json
from pathlib import Path
from pack_music_town import pack, unpack, sha, write_or_check
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'generated'
records=[]
header=['#pragma once','#include <stdint.h>','extern const uint8_t asset_audio_music_town_packed[];']
for name in ('dungeon','dlvlb','dlvlc','dlvld'):
    source=out/('audio_music_'+name+'.lz'); original=source.read_bytes()
    assert not any(original[-65536:])
    pcm=original[:-65536]; packed,widths=pack(pcm)
    assert unpack(packed,len(pcm))==pcm
    symbol='asset_audio_music_'+name+'_packed'
    write_or_check(out/(symbol[6:]+'.lz'),packed,False)
    header.append('extern const uint8_t '+symbol+'[];')
    records.append(dict(name=name,samples=len(pcm),original_bytes=len(original),bytes=len(packed),saved=len(original)-len(packed),pcm_sha256=sha(pcm),decoded_sha256=sha(unpack(packed,len(pcm)))))
write_or_check(out/'music_regions_packed.h',('\n'.join(header)+'\n').encode(),False)
write_or_check(out/'music-regions-stage71.json',(json.dumps(records,indent=2)+'\n').encode(),False)
print('Lossless region music: saved',sum(r['saved'] for r in records),'bytes; all samples bit-exact')
