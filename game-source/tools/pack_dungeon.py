"""Keep original L1.TIL 2x2 pieces intact, including the complete L5STAIRSUP miniset."""
from pathlib import Path
from PIL import Image,ImageDraw
import sys,re,json,struct
src,out=map(Path,sys.argv[1:]);pal=list(zip(*[iter((src/'l1_1.pal').read_bytes())]*3));sol=(src/'l1.sol').read_bytes();til=(src/'l1.til').read_bytes()
macros=list(range(1,25))+list(range(57,69))+[98,100,101,103,104,116,142,145,166,167,168,169,170,171,172,173,174,35,37,147,193,110,111,112,195,196,202,200,84,199,203,83,85,206,80,81,134,135]
original={i:[v+1 for v in struct.unpack_from('<4H',til,(i-1)*8)] for i in macros}
door_changes={43:392,45:394,54:397,55:398,61:399,67:400,68:401,69:403,70:404,72:406,212:407,354:409,355:410}
pieces=sorted(set(sum(original.values(),[])+[44,51,46,56,50,150,393,395,408,396,411,412]+list(door_changes)+list(door_changes.values())))
# Decode missing native pieces directly; do not fabricate seams from cropped art.
from extract_assets import pieces as decode_pieces
native=decode_pieces((src.parent/"butcher/L1.CEL").read_bytes(),(src.parent/"butcher/L1.MIN").read_bytes())
for piece in pieces:
 im=native[piece-1];im.putpalette([v for c in pal for v in c]);im.save(src/"pieces"/f"{piece:03}.png");indices={v:i for i,v in enumerate(pieces)}
lut=[0]+[min(range(1,256),key=lambda j:sum((pal[j][k]-pal[i][k]*.50)**2 for k in range(3))) for i in range(1,256)]
art=bytearray();atlas={};tile_ids=[];flags=[];manifest=[];sheet=Image.new('RGB',(8*64,((len(pieces)+7)//8)*180),(18,18,20));pen=ImageDraw.Draw(sheet)
for n,piece in enumerate(pieces):
 im=Image.open(src/'pieces'/f'{piece:03}.png');occludes=bool(im.getbbox() and im.getbbox()[1]<128)
 im=im.resize((32,80),Image.Resampling.NEAREST).point(lut);raw=im.tobytes()
 for ty in range(10):
  for tx in range(4):
   block=bytes(raw[(ty*8+y)*32+tx*8+x] for y in range(8) for x in range(8));
   if block not in atlas:atlas[block]=len(art)//64;art.extend(block)
   tile_ids.append(atlas[block]);flags.append(0 if not any(block) else 1 if all(block) else 2)
 im.putpalette([v for c in pal for v in c]);sheet.paste(im.convert('RGB').resize((64,160),Image.Resampling.NEAREST),((n%8)*64,(n//8)*180+20));pen.text(((n%8)*64+3,(n//8)*180+4),f'{n}:{piece}',fill='white')
 manifest.append({'index':n,'source_piece':piece,'brightness':.50,'sol':sol[piece-1],'occludes':occludes})
ram_bytes=min(len(art),(120000-80*80*2-13*2048)//64*64)
(out/"dungeon_door_spill.rle").write_bytes(art[ram_bytes:] or bytes(64))
(out/'dungeon_art.bin').write_bytes(art);sheet.save(out/'dungeon-assets.png')
(out/'dungeon_art_raw.rle').write_bytes(art) # Direct ROM tiles free RAM for the unlit composition cache.
(out/'dungeon-art-manifest.json').write_text(json.dumps({'source':'DIABDAT.MPQ levels/l1data/L1.TIL+CEL+MIN+SOL, L1_1.PAL','macros':original,'pieces':manifest,'raw_bytes':len(art),'tile_ids':tile_ids},indent=2))
h=['extern const uint8_t asset_dungeon_door_spill[];',f'#define DART_RAM_TILES {ram_bytes//64}','#ifndef DUNGEON_ART_H','#define DUNGEON_ART_H',f'enum {{ DART_COUNT={len(pieces)} }};', 'static const uint8_t dart_macro_ids[]={'+','.join(map(str,macros))+'};','static const uint8_t dart_macros[][4]={']
h+=['{'+','.join(str(indices[p]) for p in original[i])+'},' for i in macros];h+=['};','static const uint8_t dart_solid[]={'+','.join(str(sol[p-1]&1) for p in pieces)+'};','static const uint8_t dart_occludes[]={'+','.join(str(int(m['occludes'])) for m in manifest)+'};']
h+=['static const uint8_t dart_door[]={'+','.join(str(int(p in (44,51,214,46,56))) for p in pieces)+'};']
h += ['static const uint16_t dart_source_piece[]={'+','.join(map(str,pieces))+'};']
for side in (0,1):
 mapped=[door_changes.get(p,411+side if p==50 else 396 if p in (411,412) else p) for p in pieces]
 h += ['static const uint8_t dart_door_seam_'+str(side)+'[]={'+','.join(str(indices[p]) for p in mapped)+'};']
for p in [1,2,3,4,5,6,7,9,23,150,214,393,395,408]:h+=[f'#define DART_P{p} {indices[p]}']
h+=['static const uint16_t dart_tile_ids[]={'+','.join(map(str,tile_ids))+'};','static const uint8_t dart_tile_flags[]={'+','.join(map(str,flags))+'};','#endif'];(out/'dungeon_art.h').write_text('\n'.join(h)+'\n')
from lz77 import compress
(out/'dungeon_art.lz').write_bytes(compress(bytes(art[:ram_bytes])))
p=out/'packed_assets.h';p.write_text(re.sub(r'#define RAW_DUNGEON_ART \d+',f'#define RAW_DUNGEON_ART {ram_bytes}',p.read_text()))
print('native dungeon pieces:',len(pieces),'bytes:',len(art))
