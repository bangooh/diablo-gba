#!/usr/bin/env python3
"""Pack exactly the eight native NPC contact greetings as 6144 Hz IMA4.

Quest prose, gossip and the wounded townsman remain text-only. Source voice
ordinals come from the complete dialogue catalog, not a reordered subset.
"""
from array import array
from pathlib import Path
import argparse
import hashlib
import json
import math
import subprocess
import sys
import wave
from pack_ending_audio import encode

ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mpq', type=Path, help='Extract original greeting WAVs')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    catalog = json.loads((ROOT / 'source_assets/town_dialogue_stage53/town-dialogue-catalog.json').read_text())
    messages = {row['speech']: row for row in catalog['dialogues']}
    archive = None
    if args.mpq:
        import mpyq
        from extract_assets import read_mpq
        archive = mpyq.MPQArchive(str(args.mpq), listfile=False)
    header = ['#pragma once', '#include <stdint.h>',
              '#define GREETING_AUDIO_RATE 6144', '#define GREETING_AUDIO_REPEAT 1',
              '#define GREETING_AUDIO_COUNT 8',
              'typedef struct {const uint8_t *data;unsigned samples,voice;} GreetingAudioClip;']
    rows = []
    report = {'scope': 'Eight initial NPC greetings only; all quest/gossip prose and wounded townsman are text-only',
              'codec': 'Continuous IMA4, initial predictor/index0, one nibble per PCM16 sample, low nibble first',
              'encoded_rate': 6144, 'output_rate': 6144, 'output_repeat': 1, 'clips': []}
    outputs = {}
    for npc in catalog['npcs']:
        if not npc['greeting']:
            continue
        message = messages[npc['greeting']]
        name = npc['name'].lower()
        source = ROOT / 'source_assets/npc_greetings' / message['mpq_path'].rsplit('\\', 1)[-1]
        if archive:
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_bytes(read_mpq(archive, message['mpq_path']))
        with wave.open(str(source)) as native:
            native_samples, native_rate = native.getnframes(), native.getframerate()
        raw = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(source),
                                      '-ac', '1', '-ar', '6144', '-af', 'volume=1', '-f', 's16le', '-'])
        samples = array('h', raw)
        if sys.byteorder != 'little':
            samples.byteswap()
        payload, decoded = encode(samples)
        pcm8 = bytes((v >> 8) & 255 for v in decoded)
        signal = sum(int(v) ** 2 for v in samples)
        error = sum((int(a) - int(b)) ** 2 for a, b in zip(samples, decoded))
        symbol = 'asset_audio_greeting_' + name
        filename = 'audio_greeting_' + name + '.lz'
        outputs[ROOT / 'generated' / filename] = payload
        header.append('extern const uint8_t ' + symbol + '[];')
        rows.append('{' + symbol + ',' + str(len(samples)) + ',' + str(message['voice_index']) + '}')
        report['clips'].append({
            'name': name, 'npc': npc['name'], 'voice_index': message['voice_index'],
            'speech_id': message['speech_id'], 'speech': message['speech'],
            'mpq_path': message['mpq_path'], 'source': str(source.relative_to(ROOT)),
            'source_sha256': sha(source.read_bytes()), 'native_rate': native_rate,
            'native_samples': native_samples, 'native_seconds': native_samples / native_rate,
            'input_samples': len(samples), 'output_samples': len(samples),
            'playback_seconds': len(samples) / 6144, 'bytes': len(payload),
            'symbol': symbol, 'filename': filename, 'encoded_sha256': sha(payload),
            'decoded_pcm8_sha256': sha(pcm8), 'adpcm_snr_db': round(10 * math.log10(signal / error), 2),
        })
    assert len(rows) == 8 and len({c['voice_index'] for c in report['clips']}) == 8
    header += ['static const GreetingAudioClip greeting_audio[GREETING_AUDIO_COUNT]={', ',\n'.join(rows), '};', '']
    report['total_seconds'] = sum(c['playback_seconds'] for c in report['clips'])
    report['aligned_rom_bytes'] = sum((c['bytes'] + 3) & ~3 for c in report['clips'])
    report['pcm8_bytes_at_6144'] = sum(c['input_samples'] for c in report['clips'])
    report['metadata_rom_bytes'] = len(rows) * 12
    outputs[ROOT / 'generated/greeting_audio.h'] = '\n'.join(header).encode()
    outputs[ROOT / 'generated/greeting-audio-report.json'] = (json.dumps(report, indent=2) + '\n').encode()
    for path, data in outputs.items():
        if args.check:
            assert path.read_bytes() == data, str(path)
        else:
            path.write_bytes(data)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
