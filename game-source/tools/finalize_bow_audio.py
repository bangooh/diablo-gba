"""Recreate Stage66's two-pass bow PCM conversion from its native WAV.

Read the original source each time; never resample a previous generated stream.
"""
from pathlib import Path
import subprocess
from audio_pcm import guarded_pcm
root = Path(__file__).resolve().parents[1]
source = root / 'source_assets/bow/BFire.wav'
master = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(source),
                                 '-ac', '1', '-ar', '16384', '-f', 's8', '-'])
raw = subprocess.run(['ffmpeg', '-v', 'error', '-f', 's8', '-ar', '16384', '-ac', '1',
                      '-i', 'pipe:0', '-ac', '1', '-ar', '6144', '-af', 'volume=1',
                      '-f', 's8', 'pipe:1'], input=master, stdout=subprocess.PIPE, check=True).stdout
(root / 'generated/audio_bow.lz').write_bytes(guarded_pcm(raw))
print('Final bow audio:', len(raw), 'samples at 6144 Hz')
