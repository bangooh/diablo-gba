"""Rebuild the original menu move/select cues from the user's MPQ; no runtime decode.

Reconstructed for a local build: the 0.9.7 release references this tool but the
public repository does not include it. Mirrors pack_item_grab_rc7.py.
Order matches audio.h: AUDIO_UI_MOVE (Titlemov), AUDIO_UI_SELECT (Titlslct).
"""
import sys, subprocess, hashlib, json
from pathlib import Path
import mpyq
from extract_mpq134 import read_mpq

root = Path(__file__).resolve().parents[1]
cues = [('ui_move', r'Sfx\Items\Titlemov.wav'),
        ('ui_select', r'Sfx\Items\Titlslct.wav')]
archive = mpyq.MPQArchive(sys.argv[1], listfile=False)
header = ['#pragma once', '#include <stdint.h>']
entries, report = [], {'rate': 6144, 'cues': []}
for name, source in cues:
    wav = read_mpq(archive, source)
    pcm = subprocess.run(['ffmpeg', '-v', 'error', '-i', 'pipe:0', '-ac', '1',
                          '-ar', '6144', '-af', 'volume=0.85', '-f', 's8', 'pipe:1'],
                         input=wav, stdout=subprocess.PIPE, check=True).stdout
    payload = pcm + b'\0' * (1024 + (-len(pcm) % 4))
    symbol = 'audio_' + name
    (root / 'generated' / (symbol + '.lz')).write_bytes(payload)
    header.append('extern const uint8_t asset_%s[];' % symbol)
    entries.append('{asset_%s,%d}' % (symbol, len(pcm)))
    report['cues'].append({'source': source, 'samples': len(pcm),
                           'source_sha256': hashlib.sha256(wav).hexdigest(),
                           'asset_sha256': hashlib.sha256(payload).hexdigest()})
header.append('#define UI_AUDIO_SAMPLES ' + ','.join(entries))
(root / 'generated/ui_audio.h').write_text('\n'.join(header) + '\n')
(root / 'generated/ui_audio.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
