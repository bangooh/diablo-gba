"""MPQ native frontend art and Smacker conversion for GBA Mode4.
Run PYTHONPATH=toolchain:dungeon-gba/tools python ... DIABDAT.MPQ.
Requires ffmpeg, Pillow, mpyq/dclimplode; offline video conversion, no SMK decoder on GBA.
"""
from pathlib import Path
import sys,struct,subprocess,tempfile,json
from PIL import Image
import mpyq
from extract_assets import read_mpq
from lz77 import compress,decompress,indexed_chunks
r=Path(__file__).resolve().parents[1];src=r/'source_assets/frontend';src.mkdir(exist_ok=True);out=r/'generated'
a=mpyq.MPQArchive(sys.argv[1],listfile=False)
for name in ['gendata\\logo.smk','ui_art\\title.pcx','ui_art\\mainmenu.pcx','ui_art\\logo.pcx','ui_art\\smlogo.pcx']:
 h=a.get_hash_table_entry(name);b=a.block_table[h.block_table_index]
 if not b.flags&0x300:
  assert not b.flags&0x10000
  a.file.seek(a.header['offset']+b.offset);data=a.file.read(b.size)
 else:data=read_mpq(a,name)
 target='opening.smk' if name.endswith('.smk') else name.split('\\')[-1]
 (src/target).write_bytes(data)
def pack(name,frames):
 blobs=[compress(x) for x in frames]
 for raw,z in zip(frames,blobs):assert decompress(z)==raw
 packed=indexed_chunks(blobs)
 (out/(name+'.lz')).write_bytes(packed)
 return len(packed)
def palette(frames,count=252):
 atlas=Image.new('RGB',(240,160*len(frames)))
 for i,im in enumerate(frames):atlas.paste(im,(0,160*i))
 p=atlas.quantize(colors=count,method=Image.Quantize.MEDIANCUT).getpalette()[:count*3]
 p+=bytes([0]*(768-len(p))) if isinstance(p,bytes) else [0]*(768-len(p))
 p[252*3:]=[0,0,0,220,180,105,255,232,168,98,91,76]
 im=Image.new('P',(1,1));im.putpalette(p);return im,p
def palbytes(p):return struct.pack('<256H',*[(p[i]>>3)|((p[i+1]>>3)<<5)|((p[i+2]>>3)<<10) for i in range(0,768,3)])
with tempfile.TemporaryDirectory() as temp:
 rgb=Path(temp)/'video.rgb';pcm=Path(temp)/'audio.pcm'
 subprocess.run(['ffmpeg','-v','error','-i',str(src/'opening.smk'),'-vf','scale=240:117:flags=lanczos,pad=240:160:0:21','-f','rawvideo','-pix_fmt','rgb24',str(rgb)],check=True)
 subprocess.run(['ffmpeg','-v','error','-i',str(src/'opening.smk'),'-vn','-ac','1','-ar','16384','-f','s8',str(pcm)],check=True)
 raw=rgb.read_bytes();frames=[Image.frombytes('RGB',(240,160),raw[i:i+115200]) for i in range(0,len(raw),115200)]
 pal,p=palette(frames[::8]);video=[im.quantize(palette=pal,dither=Image.Dither.NONE).tobytes() for im in frames]
 size=pack('front_video',video);(out/'front_video_palette.lz').write_bytes(compress(palbytes(p)))
 audio=pcm.read_bytes();audio+=bytes((-len(audio))%4+4096)
 # .lz suffix is the existing assembler blob-discovery convention; audio is raw PCM.
 (out/'front_audio.lz').write_bytes(audio)
 frames[90].save(src/'opening-preview.png')
frames=[]
for bg,logo,rect in [('title','logo',(17,55,206,81)),('mainmenu','smlogo',(47,6,146,58))]:
 background=Image.open(src/(bg+'.pcx')).convert('RGB').resize((240,160),Image.Resampling.LANCZOS)
 sheet=Image.open(src/(logo+'.pcx'));fh=sheet.height//15
 for i in range(15):
  im=background.copy();part=sheet.crop((0,i*fh,sheet.width,(i+1)*fh));mask=Image.frombytes('L',part.size,bytes(0 if v==250 else 255 for v in part.tobytes()))
  x,y,w,h=rect;part=part.convert('RGBA');part.putalpha(mask);part=part.resize((w,h),Image.Resampling.LANCZOS)
  im.paste(part,(x,y),part);frames.append(im)
 # Title's original copyright is too small at GBA resolution; retained in art.
pal,p=palette(frames);pack('front_screens',[im.quantize(palette=pal,dither=Image.Dither.NONE).tobytes() for im in frames]);(out/'front_palette.lz').write_bytes(compress(palbytes(p)))
for i,n in [(0,'title'),(15,'menu')]:frames[i].save(src/(n+'-preview.png'))
subprocess.run([sys.executable,str(r/'tools/pack_menu_font.py'),sys.argv[1]],check=True)
(out/'frontend.h').write_text('/* Generated MPQ frontend resources. */\n'+''.join('extern const uint8_t asset_'+n+'[];\n' for n in ['front_video','front_video_palette','front_audio','front_screens','front_palette','front_font'])+f'#define FRONT_VIDEO_FRAMES {len(video)}\n')
# Retain the complete indexed source above for reproducible resampling, while
# production references the compact10FPS opening and blue-only8192Hz PCM.
subprocess.run([sys.executable,str(r/'tools/pack_front_opening.py'),'--codec','delta'],check=True)
subprocess.run([sys.executable,str(r/'tools/pack_front_opening_audio.py')],check=True)
(out/'frontend-report.json').write_text(json.dumps({'video_frames':160,'fps':10,'video_bytes':(out/'front_opening_video.lz').stat().st_size,'audio_bytes':(out/'front_audio.lz').stat().st_size,'screen_frames':30,'source_video_frames':len(video),'source_fps':15,'video_detail':'front-opening-report.json','audio_detail':'front-opening-audio-report.json','source':['gendata/logo.smk','ui_art/title.pcx','ui_art/mainmenu.pcx','ui_art/logo.pcx','ui_art/smlogo.pcx','CtrlPan/SmalText.CEL']},indent=2))
print('frontend',160,(out/'front_opening_video.lz').stat().st_size,(out/'front_audio.lz').stat().st_size)
