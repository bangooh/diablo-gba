"""Complete native IS_TRAP/IS_BARLFIRE cues in the FIFO-B PCM format.

Usage: python3 tools/pack_hazard_audio.py DIABDAT.MPQ
       python3 tools/pack_hazard_audio.py --check

Despite the existing .lz asset naming convention these files are uncompressed
signed8 mono PCM at16384Hz. Their true lengths exclude a1024-sample silent DMA
guard and padding to16 bytes. Neither native sound is shortened or faded.
"""
from pathlib import Path
import argparse
import hashlib
import io
import json
import math
import re
import struct
import subprocess
import wave

from audio_pcm import guarded_pcm, EFFECT_GUARD
from pack_difficulty import retail

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'generated'
SOURCE=ROOT/'source_assets/hazard_audio'
RATE=16384
SOUNDS=(('trap','IS_TRAP','Trap.wav'),('barrel_fire','IS_BARLFIRE','Barlfire.wav'))


def sha(data):
    return hashlib.sha256(data).hexdigest()


def source_paths():
    effects=(ROOT/'source_assets/item_catalog/effects.cpp').read_text()
    table=effects.split('TSFX sgSFX[] = {',1)[1].split('\n};',1)[0]
    table=retail(table)
    paths=[json.loads(text) for text in re.findall(r'\{\s*sfx_\w+,\s*("(?:[^"\\]|\\.)*")\s*,',table)]
    enums=(ROOT/'source_assets/item_catalog/enums.h').read_text()
    enum=retail(enums.split('typedef enum _sfx_id {',1)[1].split('}',1)[0])
    names=re.findall(r'^\s*(\w+)\s*,',enum,re.M)
    result={}
    for symbol,native,filename in SOUNDS:
        index=names.index(native)
        expected='Sfx\\Items\\'+filename
        assert paths[index]==expected,(native,index,paths[index])
        result[symbol]=(native,index,expected)
    return result


def riff_pcm(raw):
    assert raw[:4]==b'RIFF' and raw[8:12]==b'WAVE'
    assert struct.unpack_from('<I',raw,4)[0]+8==len(raw)
    pos, chunks=12,{}
    while pos<len(raw):
        assert pos+8<=len(raw)
        tag,length=raw[pos:pos+4],struct.unpack_from('<I',raw,pos+4)[0]
        assert pos+8+length<=len(raw)
        assert tag not in chunks
        chunks[tag]=raw[pos+8:pos+8+length]
        pos+=8+length+(length&1)
    assert pos==len(raw) and 'fmt '.encode() in chunks and b'data' in chunks
    kind,channels,rate,byte_rate,alignment,bits=struct.unpack_from('<HHIIHH',chunks[b'fmt '])
    assert (kind,channels,rate,byte_rate,alignment,bits)==(1,1,22050,22050,1,8)
    samples=chunks[b'data']
    # Independent Python RIFF reader checks every actual source PCM sample.
    with wave.open(io.BytesIO(raw),'rb') as stream:
        assert stream.getparams()[:4]==(1,1,22050,len(samples))
        assert stream.readframes(stream.getnframes())==samples
        assert stream.readframes(1)==b''
    # FFmpeg must decode the same original samples before any resampling.
    decoded=subprocess.run(['ffmpeg','-v','error','-i','pipe:0','-ac','1','-ar',str(rate),
                            '-f','u8','-'],input=raw,stdout=subprocess.PIPE,check=True).stdout
    assert decoded==samples
    return samples,rate


def convert(raw):
    command=['ffmpeg','-v','error','-i','pipe:0','-ac','1','-ar',str(RATE),
             '-af','volume=0.85','-f','s8','-']
    pcm=subprocess.run(command,input=raw,stdout=subprocess.PIPE,check=True).stdout
    assert pcm==subprocess.run(command,input=raw,stdout=subprocess.PIPE,check=True).stdout
    return pcm


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mpq',type=Path,nargs='?')
    parser.add_argument('--check',action='store_true')
    parser.add_argument('--intermediate-master',action='store_true',help='Generate 16384-Hz input for the later 6144-Hz conversion')
    args=parser.parse_args()
    paths=source_paths()
    runtime=(ROOT/'src/audio.c').read_text()
    if not args.intermediate_master:
        assert re.search(r'R16\(0x04000104\)=0xfc00;',runtime)
    assert RATE==16777216//(65536-0xfc00) and EFFECT_GUARD==1024
    if not args.check:
        if not args.mpq:parser.error('DIABDAT.MPQ is required unless --check is used')
        import mpyq
        from extract_assets import read_mpq
        archive=mpyq.MPQArchive(str(args.mpq),listfile=False)
        SOURCE.mkdir(exist_ok=True)
        for symbol,native,filename in SOUNDS:
            (SOURCE/filename).write_bytes(read_mpq(archive,paths[symbol][2]))
    header=['/* Complete native cues; raw signed8 mono PCM, not LZ compressed. */',
            '#pragma once','#include <stdint.h>',f'#define HAZARD_AUDIO_RATE {RATE}',
            '#define HAZARD_AUDIO_COUNT 2','#define HAZARD_AUDIO_TRAP 0',
            '#define HAZARD_AUDIO_BARREL_FIRE 1']
    report={'source_revision':'9f01757243412bb154d02d9af65c804159e6e0ea',
            'playback_hz':RATE,'format':'raw signed8 mono PCM','volume':0.85,
            'guard_samples':EFFECT_GUARD,'dma_padding_alignment':16,
            'truncated':False,'faded':False,'samples':{},'total_runtime_bytes':0}
    outputs={}
    for symbol,native,filename in SOUNDS:
        raw=(SOURCE/filename).read_bytes()
        original,rate=riff_pcm(raw)
        pcm=convert(raw)
        assert abs(len(pcm)-len(original)*RATE/rate)<=1
        assert pcm and any(pcm)
        guarded=guarded_pcm(pcm)
        guarded+=bytes((-len(guarded))%16)
        assert len(guarded)%16==0 and guarded[:len(pcm)]==pcm
        assert len(guarded)-len(pcm)>=EFFECT_GUARD and not any(guarded[len(pcm):])
        # At stop time the FIFO may have fetched the containing16-byte DMA
        # block. Every subsequent request through the full IRQ guard is silent.
        assert guarded[len(pcm):((len(pcm)+15)//16)*16]==bytes((-len(pcm))%16)
        assert len(guarded)-len(pcm)>math.ceil(RATE*3/59.7275)+32
        outputs[f'audio_{symbol}.lz']=guarded
        header.extend([f'extern const uint8_t asset_audio_{symbol}[];',
                       f'#define AUDIO_{symbol.upper()}_LENGTH {len(pcm)}'])
        signed=[b if b<128 else b-256 for b in pcm]
        report['samples'][symbol]={'source':paths[symbol][2],'source_id':native,
             'native_sfx_index':paths[symbol][1],'source_file':str((SOURCE/filename).relative_to(ROOT)),
             'source_sha256':sha(raw),'source_bytes':len(raw),'source_rate':rate,
             'source_pcm_samples':len(original),'source_pcm_sha256':sha(original),
             'source_seconds':len(original)/rate,'pcm_samples':len(pcm),'pcm_seconds':len(pcm)/RATE,
             'pcm_sha256':sha(pcm),'guarded_bytes':len(guarded),'guarded_sha256':sha(guarded),
             'silent_guard_and_alignment_bytes':len(guarded)-len(pcm),
             'signed_peak':max(abs(v) for v in signed),'rms':math.sqrt(sum(v*v for v in signed)/len(signed)),
             'riff_and_independent_decoder_agree':True,'resample_repeat_identical':True,
             'full_source_duration_retained':True}
        report['total_runtime_bytes']+=len(guarded)
    header.extend(['#define HAZARD_AUDIO_SAMPLES {asset_audio_trap,AUDIO_TRAP_LENGTH},{asset_audio_barrel_fire,AUDIO_BARREL_FIRE_LENGTH}',''])
    outputs['hazard_audio.h']='\n'.join(header).encode()
    outputs['hazard_audio-report.json']=(json.dumps(report,indent=2)+'\n').encode()
    for filename,data in outputs.items():
        path=OUT/filename
        if args.check:assert path.read_bytes()==data,filename+' differs'
        else:path.write_bytes(data)
    print(f'PASS full native hazard audio: {report["total_runtime_bytes"]} bytes; RIFF/PCM, deterministic resampling, full duration and16-byte DMA/IRQ guard verified')


if __name__=='__main__':
    main()
