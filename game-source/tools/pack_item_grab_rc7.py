"""Rebuild the original item grab cue from the user's MPQ; no runtime decode.

Dependencies match pack_audio134.py: mpyq, dclimplode and ffmpeg.
"""
import sys, subprocess, hashlib, json
from pathlib import Path
import mpyq
from extract_mpq134 import read_mpq

root = Path(__file__).resolve().parents[1]
source = r'Sfx\Items\Invgrab.wav'
wav = read_mpq(mpyq.MPQArchive(sys.argv[1], listfile=False), source)
pcm = subprocess.run(['ffmpeg', '-v', 'error', '-i', 'pipe:0', '-ac', '1',
                      '-ar', '6144', '-af', 'volume=0.85', '-f', 's8', 'pipe:1'],
                     input=wav, stdout=subprocess.PIPE, check=True).stdout
payload = pcm + b'\0' * (1024 + (-len(pcm) % 4))
(root/'generated/audio_item_invgrab.lz').write_bytes(payload)
(root/'generated/item_grab_audio.h').write_text(
    '#pragma once\n#include <stdint.h>\n'
    'extern const uint8_t asset_audio_item_invgrab[];\n'
    '#define ITEM_GRAB_AUDIO_SAMPLE {asset_audio_item_invgrab,' + str(len(pcm)) + '}\n')
report = {'source': source, 'rate': 6144, 'samples': len(pcm),
          'source_sha256': hashlib.sha256(wav).hexdigest(),
          'asset_sha256': hashlib.sha256(payload).hexdigest()}
(root/'generated/item_grab_rc7.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report))
