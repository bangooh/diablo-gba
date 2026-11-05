"""Retail CharBut frame2 and DevilutionX NextPlrLevel audio, adapted for GBA."""
from audio_pcm import guarded_pcm
from pathlib import Path
import sys, struct, subprocess, json
from PIL import Image
from extract_assets import read_mpq
from pack_gba_assets import tile_bytes
import mpyq

root=Path(__file__).resolve().parents[1]
src=root/'source_assets/levelup';src.mkdir(exist_ok=True)
out=root/'generated'
names={'CharBut.CEL':'Data\\CharBut.CEL','town.pal':'Levels\\TownData\\Town.PAL',
       'Invharm.wav':'Sfx\\Items\\Invharm.wav','Invsign.wav':'Sfx\\Items\\Invsign.wav'}
if len(sys.argv)>1:
    archive=mpyq.MPQArchive(sys.argv[1],listfile=False)
    for name,path in names.items():(src/name).write_bytes(read_mpq(archive,path))
raw=(src/'CharBut.CEL').read_bytes();start,end=struct.unpack_from('<II',raw,8)
data=raw[start:end];pixels=bytearray();p=0
while p<len(data):
    n=data[p];p+=1
    if n>=128:pixels.extend(bytes(256-n))
    else:pixels.extend(data[p:p+n]);p+=n
assert len(pixels)==41*22
im=Image.frombytes('P',(41,22),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
pal=(src/'town.pal').read_bytes();im.putpalette(pal);im.save(src/'button.png')
# Existing UI bank5: no new palette bank or persistent render buffer.
colors=[(0,0,0),(4,4,5),(10,10,11),(20,19,15),(8,0,0),(18,0,0),
        (31,3,0),(0,0,4),(0,0,18),(3,8,31),(31,26,0),(31,30,20),
        (2,2,3),(31,31,31),(20,20,20),(0,0,0)]
rgb=[tuple(c*255//31 for c in color) for color in colors]
lut=[0]+[min(range(1,16),key=lambda j:sum((pal[i*3+k]-rgb[j][k])**2 for k in range(3))) for i in range(1,256)]
small=im.resize((24,13),Image.Resampling.NEAREST).point(lut)
canvas=Image.new('P',(32,16));canvas.paste(small,(0,0));canvas.putpalette([c for color in rgb for c in color]+[0]*720)
canvas.save(src/'button-gba.png')
tiles=tile_bytes(canvas,4)
# GBA has one effects PCM channel: mix the two simultaneous original cues
# offline. Both start at sample0; half gain keeps their sum unclipped.
pcm=subprocess.run(['ffmpeg','-v','error','-i',str(src/'Invharm.wav'),'-i',str(src/'Invsign.wav'),
 '-filter_complex','[0:a][1:a]amix=inputs=2:duration=longest:dropout_transition=0,volume=0.85',
 '-ac','1','-ar','16384','-f','s8','-'],stdout=subprocess.PIPE,check=True).stdout
(out/'audio_levelup.lz').write_bytes(guarded_pcm(pcm))
(out/'levelup.h').write_text('#pragma once\nextern const uint8_t asset_audio_levelup[];\n'
 +f'#define AUDIO_LEVELUP_LENGTH {len(pcm)}\n'
 +'static const uint32_t levelup_tiles[64]={'+','.join(hex(v) for v in struct.unpack('<64I',tiles))+'};\n')
(out/'levelup-report.json').write_text(json.dumps({'icon':'Data/CharBut.CEL frame2','native_size':[41,22],
 'gba_size':[24,13],'sound':['Sfx/Items/Invharm.wav','Sfx/Items/Invsign.wav'],'samples':len(pcm)},indent=2)+'\n')
