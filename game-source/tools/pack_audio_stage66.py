#!/usr/bin/env python3
"""Reproducible 6144-Hz assets; retain codecs and stable effect IDs.

The approved PCM8 master samples are preserved once from Stage65, before any
resampling, so repeated builds never resample an already converted asset.
IMA clips use the retained original WAV/SMK sources. New regional tracks and
monster sounds are extracted once from the user's MPQ, not required to build.
"""
import argparse
from array import array
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

from pack_music_town import pack, unpack
from pack_ending_audio import encode

ROOT = Path(__file__).resolve().parents[1]
RATE = 6144
SOURCE = ROOT / 'source_assets/audio_stage66'
OUT = ROOT / 'generated'
REPORT = OUT / 'audio-stage66-report.json'

def sha(data):
    return hashlib.sha256(data).hexdigest()

def write(path, data):
    if isinstance(data, str):
        data = data.encode()
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)

def bootstrap(baseline):
    target = SOURCE / 'masters.json'
    if target.exists():
        return
    text = subprocess.check_output(['gcc', '-E', '-P', str(baseline/'src/audio.c')], text=True)
    body = re.search(r'static const Sample effects\[\]\s*=\s*\{(.*?)\n\};', text, re.S)[1]
    clips = [{'symbol': n, 'samples': int(length), 'effect_id': i}
             for i, (n, length) in enumerate(re.findall(r'\{\s*(asset_\w+)\s*,\s*(\d+)\s*\}', body))]
    assert len(clips) == 176
    clips += [{'symbol': 'asset_audio_music_town', 'samples': 4729010},
              {'symbol': 'asset_audio_music_dungeon', 'samples': 4345149},
              {'symbol': 'asset_front_audio', 'samples': 90112, 'rate': 8192}]
    for clip in clips:
        name = clip['symbol'][6:]
        data = (baseline/'generated'/(name+'.lz')).read_bytes()
        assert not any(data[clip['samples']:])
        master = data[:clip['samples']]
        write(SOURCE/(name+'.pcm8'), master)
        clip.update(rate=clip.get('rate', 16384), sha256=sha(master), source=name+'.pcm8')
    write(target, json.dumps({'baseline': 'Stage65 approved PCM8 masters', 'clips': clips}, indent=2)+'\n')

def extract_regions(mpq):
    from extract_assets import read_mpq
    import mpyq
    archive = mpyq.MPQArchive(str(mpq), listfile=False)
    for name in ('DLvlB', 'DLvlC', 'DLvlD'):
        path = SOURCE/(name+'.wav')
        if not path.exists():
            write(path, read_mpq(archive, 'Music\\'+name+'.wav'))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bootstrap', type=Path)
    parser.add_argument('--mpq', type=Path)
    args = parser.parse_args()
    if args.bootstrap:
        bootstrap(args.bootstrap)
    if args.mpq:
        extract_regions(args.mpq)
    masters = json.loads((SOURCE/'masters.json').read_text())['clips']
    previous = json.loads(REPORT.read_text()) if REPORT.exists() else {}
    cache = {c['symbol']: c for c in previous.get('clips', [])}
    jobs = []
    for c in masters:
        name = c['symbol'][6:]
        codec = 'packed8' if name == 'audio_music_town' else 'pcm8'
        symbol = c['symbol']+'_packed' if codec == 'packed8' else c['symbol']
        jobs.append(dict(c, symbol=symbol, path=SOURCE/c['source'], codec=codec,
                         guard=65536 if name=='audio_music_dungeon' else 4096 if name=='front_audio' else 0 if codec=='packed8' else 1024,
                         source_format='pcm8', gain=1))
    jobs.append(dict(symbol='asset_audio_music_menu_adpcm', path=ROOT/'source_assets/combat/music_menu.wav', codec='ima4', gain=.60, guard=0))
    for c in json.loads((OUT/'ending-audio-report.json').read_text())['clips']:
        jobs.append(dict(symbol='asset_audio_ending_'+c['name'], path=ROOT/c['source'], codec='ima4', gain=1, guard=0))
    for c in json.loads((OUT/'greeting-audio-report.json').read_text())['clips']:
        jobs.append(dict(symbol=c['symbol'], path=ROOT/c['source'], codec='ima4', gain=1, guard=0, voice=c['voice_index']))
    for name in ('DLvlB', 'DLvlC', 'DLvlD'):
        jobs.append(dict(symbol='asset_audio_music_'+name.lower(), path=SOURCE/(name+'.wav'), codec='pcm8', gain=.60, guard=65536))
    additions = ROOT/'source_assets/monster_stage66/audio.json'
    if additions.exists():
        for c in json.loads(additions.read_text())['clips']:
            jobs.append(dict(c, path=ROOT/c['source'], codec='pcm8', gain=.85, guard=1024))

    for c in json.loads((ROOT/'source_assets/monster_stage71/audio.json').read_text())['clips']:
        jobs.append(dict(c,path=ROOT/c['source'],codec='pcm8',gain=.85,guard=1024))

    def convert(job):
        path, symbol, codec = job['path'], job['symbol'], job['codec']
        original = path.read_bytes()
        if 'sha256' in job:
            assert sha(original) == job['sha256'], path
        signature = sha(original + json.dumps({k:v for k,v in job.items() if k!='path'}, sort_keys=True).encode()+b'6144-v2')
        output = OUT/(symbol[6:]+'.lz')
        old = cache.get(symbol)
        if old and old['signature']==signature and output.exists() and sha(output.read_bytes())==old['sha256']:
            return old
        command = ['ffmpeg', '-v', 'error']
        if job.get('source_format') == 'pcm8':
            command += ['-f','s8','-ar',str(job['rate']),'-ac','1']
        command += ['-i',str(path),'-map','0:a:0','-vn','-ac','1','-ar',str(RATE),'-af','volume='+str(job['gain']),'-f','s16le' if codec=='ima4' else 's8','-']
        raw = subprocess.check_output(command)
        count = len(raw)//2 if codec=='ima4' else len(raw)
        if codec=='ima4':
            samples = array('h',raw)
            if sys.byteorder!='little': samples.byteswap()
            payload, decoded = encode(samples)
            verification = 'continuous IMA4; source duration retained'
        elif codec=='packed8':
            payload, widths = pack(raw)
            assert unpack(payload,count)==raw
            verification = 'every resampled PCM8 sample restores bit-exactly'
        else:
            payload = raw + bytes((-count)%4+job['guard'])
            verification = 'PCM8; aligned complete silent guard retained'
        write(output,payload)
        record = dict(symbol=symbol, source=str(path.relative_to(ROOT)), source_sha256=sha(original),
                      signature=signature, codec=codec, samples=count, seconds=count/RATE,
                      bytes=len(payload), guard_bytes=job['guard'], sha256=sha(payload), verification=verification)
        if codec=='ima4':record['decoded_pcm8_sha256']=sha(bytes((v//256)&255 for v in decoded))
        for key in ('effect_id','voice'):
            if key in job: record[key]=job[key]
        return record
    with ThreadPoolExecutor(max_workers=6) as executor:
        clips = list(executor.map(convert,jobs))
    by = {c['symbol']:c for c in clips}
    assert len(by)==len(clips)
    effects = sorted((c for c in clips if 'effect_id' in c), key=lambda c:c['effect_id'])
    assert [c['effect_id'] for c in effects]==list(range(len(effects)))
    lines = ['/* Stage66: approved codecs, all sources at6144Hz. */','#pragma once','#include <stdint.h>']
    lines += ['extern const uint8_t '+c['symbol']+'[];' for c in clips]
    lines += ['#define AUDIO_EFFECT_SAMPLES '+','.join('{'+c['symbol']+','+str(c['samples'])+'}' for c in effects),
              '#define AUDIO_EFFECT_COUNT '+str(len(effects))]
    for key,symbol in [('TOWN','asset_audio_music_town_packed'),('DUNGEON','asset_audio_music_dungeon'),('MENU','asset_audio_music_menu_adpcm'),('CATACOMBS','asset_audio_music_dlvlb'),('CAVES','asset_audio_music_dlvlc'),('HELL','asset_audio_music_dlvld')]:
        lines += ['#define AUDIO_TRACK_'+key+'_SAMPLES '+str(by[symbol]['samples'])]
    write(OUT/'audio_6k.h','\n'.join(lines)+'\n')
    town = by['asset_audio_music_town_packed']
    write(OUT/'music_town_packed.h', '#pragma once\n#include <stdint.h>\nextern const uint8_t asset_audio_music_town_packed[];\n'+
          f'#define MUSIC_TOWN_PACKED_SAMPLES {town["samples"]}u\n#define MUSIC_TOWN_PACKED_BLOCKS {(town["samples"]+255)//256}u\n#define MUSIC_TOWN_PACKED_BYTES {town["bytes"]}u\n')
    menu = by['asset_audio_music_menu_adpcm']
    write(OUT/'menu_audio.h', '#pragma once\n#include <stdint.h>\nextern const uint8_t asset_audio_music_menu_adpcm[];\n'+
          f'#define MENU_AUDIO_RATE {RATE}\n#define MENU_AUDIO_REPEAT 1\n#define MENU_AUDIO_SAMPLES {menu["samples"]}\n#define MENU_AUDIO_OUTPUT_SAMPLES {menu["samples"]}\n#define MENU_AUDIO_BYTES {menu["bytes"]}\n')
    header = ['#pragma once','#include <stdint.h>',f'#define ENDING_AUDIO_RATE {RATE}',f'#define ENDING_AUDIO_OUTPUT_RATE {RATE}','#define ENDING_AUDIO_REPEAT 1']
    for key in ('sorcerer','common'):
        clip=by['asset_audio_ending_'+key];tag='ENDING_AUDIO_'+key.upper()
        header += ['extern const uint8_t '+clip['symbol']+'[];',f'#define {tag}_SAMPLES {clip["samples"]}',f'#define {tag}_OUTPUT_SAMPLES {clip["samples"]}',f'#define {tag}_BYTES {clip["bytes"]}']
    write(OUT/'ending_audio.h','\n'.join(header)+'\n')
    greeting = [c for c in clips if 'voice' in c]
    header = ['#pragma once','#include <stdint.h>',f'#define GREETING_AUDIO_RATE {RATE}','#define GREETING_AUDIO_REPEAT 1','#define GREETING_AUDIO_COUNT 8',
              'typedef struct {const uint8_t *data;unsigned samples,voice;} GreetingAudioClip;']
    header += ['extern const uint8_t '+c['symbol']+'[];' for c in greeting]
    header += ['static const GreetingAudioClip greeting_audio[GREETING_AUDIO_COUNT]={',
               ',\n'.join('{'+c['symbol']+','+str(c['samples'])+','+str(c['voice'])+'}' for c in greeting),'};']
    write(OUT/'greeting_audio.h','\n'.join(header)+'\n')
    write(OUT/'front_opening_audio.h',f'#pragma once\n#define FRONT_OPENING_AUDIO_RATE {RATE}\n#define FRONT_OPENING_AUDIO_TIMER_RELOAD 0xf555\n#define FRONT_OPENING_AUDIO_SAMPLES {11*RATE}\n#define FRONT_OPENING_AUDIO_GUARD_BYTES 4096\n#define FRONT_OPENING_AUDIO_BYTES {by["asset_front_audio"]["bytes"]}\n')
    report = dict(rate=RATE, hardware_timer_period=2731, actual_hardware_rate=16777216/2731,
                  original_effect_ids_preserved=176, additional_effects=len(effects)-176,
                  scope='All linked audio; video pixels/FPS unchanged; full regional music; retain codecs', clips=clips)
    write(REPORT,json.dumps(report,indent=2)+'\n')
    # Keep consumer-facing metadata consistent with the actual linked assets.
    # Remove old quality/space measurements that refer to the prior8k encode.
    def refresh(record,clip):
        record.update(encoded_rate=RATE,output_rate=RATE,output_repeat=1,
                      input_samples=clip['samples'],output_samples=clip['samples'],
                      playback_seconds=clip['seconds'],encoded_bytes=clip['bytes'],
                      bytes=clip['bytes'],encoded_sha256=clip['sha256'],
                      decoded_pcm8_sha256=clip['decoded_pcm8_sha256'])
        for key in ('adpcm_snr_db','source_peak','decoded_peak','previous_asset_bytes','previous_asset_sha256','raw_aligned_bytes_saved','quality_measurement_limit'):
            record.pop(key,None)
    for filename in ('ending-audio-report.json','greeting-audio-report.json','menu-audio-report.json'):
        old=json.loads((OUT/filename).read_text())
        old.update(encoded_rate=RATE,output_rate=RATE,output_repeat=1,stage66_report='audio-stage66-report.json')
        if 'clips' in old:
            for c in old['clips']:refresh(c,by[c.get('symbol','asset_audio_ending_'+c['name'])])
            old['aligned_rom_bytes']=sum((c['bytes']+3)&~3 for c in old['clips'])
        else:refresh(old,menu);old['aligned_rom_bytes']=(menu['bytes']+3)&~3
        write(OUT/filename,json.dumps(old,indent=2)+'\n')
    print('Stage66 audio:',len(clips),'clips;',sum(c['bytes'] for c in clips),'bytes before alias sharing;',RATE,'Hz')

if __name__=='__main__': main()
