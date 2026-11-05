#!/usr/bin/env python3
"""Original class-specific mana and failed-pickup voices, including variants."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import wave
from audio_pcm import guarded_pcm

ROOT = Path(__file__).resolve().parents[1]
CLASSES = [
    ('warrior', 'Warrior', ('Warior35', 'Warior14', 'Wario14b', 'Wario14c')),
    ('rogue', 'Rogue', ('Rogue35', 'Rogue14', 'Rogue15', 'Rogue16')),
    ('sorcerer', 'Sorceror', ('Mage35', 'Mage14', 'Mage15', 'Mage16')),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    audit = json.loads((ROOT / 'source_assets/hero_errors_stage53/hero-error-source-audit.json').read_text())
    for (_, folder, files), row in zip(CLASSES, audit['classes']):
        assert [row['mana']['mpq_path'], *[v['mpq_path'] for v in row['inventory_full']]] == [
            'Sfx\\' + folder + '\\' + name + '.wav' for name in files]
    header = ['/* Generated original hero error cues; class*4 + mana/space0/1/2. */',
              '#pragma once', '#include <stdint.h>',
              '#define HERO_ERROR_AUDIO_RATE 16384', '#define HERO_ERROR_AUDIO_COUNT 12']
    outputs, entries = {}, []
    report = {'scope': 'Original class-specific insufficient mana and failed ground-item pickup voices',
              'rate': 16384, 'source_gain': 0.85, 'format': 'Signed8 mono PCM with1024-byte silent DMA guard',
              'selection': 'Class*4: mana0, inventory variants1..3. Warrior uses original two-stage random selection (weights1:4:4); Rogue/Sorcerer uniform3 variants. Cosmetic RNG only.',
              'clips': []}
    for cls, (name, folder, files) in enumerate(CLASSES):
        for variant, filename in enumerate(files):
            source = ROOT / 'source_assets/hero_errors' / (filename + '.wav')
            with wave.open(str(source)) as wav:
                frames, rate = wav.getnframes(), wav.getframerate()
            pcm = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(source),
                                           '-ac', '1', '-ar', '16384', '-af', 'volume=0.85', '-f', 's8', '-'])
            payload = guarded_pcm(pcm)
            key = name + ('_mana' if not variant else '_space' + str(variant - 1))
            symbol = 'asset_audio_hero_error_' + key
            macro = 'AUDIO_HERO_ERROR_' + key.upper() + '_LENGTH'
            asset = 'audio_hero_error_' + key + '.lz'
            outputs[ROOT / 'generated' / asset] = payload
            header += ['extern const uint8_t ' + symbol + '[];', '#define ' + macro + ' ' + str(len(pcm))]
            entries.append('{' + symbol + ',' + macro + '}')
            report['clips'].append({'class': cls, 'class_name': name, 'variant': variant,
                'sample_index': cls * 4 + variant, 'mpq_path': 'Sfx\\' + folder + '\\' + filename + '.wav',
                'source': str(source.relative_to(ROOT)), 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                'native_samples': frames, 'native_rate': rate, 'native_seconds': frames / rate,
                'samples': len(pcm), 'seconds': len(pcm) / 16384, 'bytes': len(payload),
                'guard_bytes': len(payload) - len(pcm), 'symbol': symbol, 'filename': asset,
                'pcm_sha256': hashlib.sha256(pcm).hexdigest(), 'asset_sha256': hashlib.sha256(payload).hexdigest()})
    header += ['#define HERO_ERROR_AUDIO_SAMPLES ' + ','.join(entries), '']
    report['total_seconds'] = sum(c['seconds'] for c in report['clips'])
    report['total_asset_bytes'] = sum(c['bytes'] for c in report['clips'])
    report['metadata_rom_bytes'] = 12 * 8
    outputs[ROOT / 'generated/hero_error_audio.h'] = '\n'.join(header).encode()
    outputs[ROOT / 'generated/hero-error-audio-report.json'] = (json.dumps(report, indent=2) + '\n').encode()
    for path, data in outputs.items():
        if args.check:
            assert path.read_bytes() == data, str(path)
        else:
            path.write_bytes(data)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
