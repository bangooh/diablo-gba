"""Build Mode 0 background tiles and 4bpp OBJ animation data from decoded assets."""
from pathlib import Path
from PIL import Image,ImageDraw
from collections import Counter
import argparse,struct,json

def mask(im):return Image.frombytes('L',im.size,bytes(255 if v else 0 for v in im.tobytes()))
def tile_bytes(im,bpp=8):
    result=bytearray()
    for y in range(0,im.height,8):
        for x in range(0,im.width,8):
            b=im.crop((x,y,x+8,y+8)).tobytes()
            result.extend(b if bpp==8 else bytes(b[i]|(b[i+1]<<4) for i in range(0,64,2)))
    return bytes(result)
def rgb15(pal):return b''.join(struct.pack('<H',(r>>3)|((g>>3)<<5)|((b>>3)<<10)) for r,g,b in pal)
def obj_palette(images,source_palette):
    count=Counter()
    for im in images:count.update(im.tobytes())
    samples=[]
    for i,n in count.items():
        if i:samples.extend([source_palette[i]]*min(n,2048))
    sample=Image.new('RGB',(len(samples),1));sample.putdata(samples)
    q=sample.quantize(colors=15,method=Image.Quantize.MEDIANCUT)
    flat=q.getpalette()[:45];pal=[(0,0,0)]+[tuple(flat[i:i+3]) for i in range(0,45,3)]
    lut=[0]+[min(range(1,16),key=lambda j:sum((pal[j][k]-source_palette[i][k])**2 for k in range(3))) for i in range(1,256)]
    return pal,lut
def main():
    ap=argparse.ArgumentParser();ap.add_argument('decoded');ap.add_argument('out');a=ap.parse_args()
    src=Path(a.decoded);out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
    raw=(src/'l1_1.pal').read_bytes();pal=[tuple(raw[i:i+3]) for i in range(0,768,3)]
    def piece(n):return Image.open(src/'pieces'/f'{n:03}.png').resize((32,80),Image.Resampling.NEAREST)
    world=Image.new('P',(512,512),0);world.putpalette(raw)
    placements=[]
    # Darken the complete background; actors and HUD palettes stay unchanged.
    floor_lut=[0]+[min(range(1,256),key=lambda j:sum((pal[j][k]-pal[i][k]*0.50)**2 for k in range(3))) for i in range(1,256)]
    mega=(src/'l1.til').read_bytes()
    for my in range(7):
        for mx in range(7):
            tile=4 if mx==0 and my==0 else 1 if mx==0 else 2 if my==0 else 13
            for i,n in enumerate(struct.unpack_from('<4H',mega,(tile-1)*8)):
                placements.append((mx*2+(i&1),my*2+(i>>1),n+1,tile==13))
    for x,y,n,is_floor in sorted(placements,key=lambda p:p[0]+p[1]):
        im=piece(n)
        im=im.point(floor_lut)
        world.paste(im,(256+(x-y)*16-16,72+(x+y)*8-80),mask(im))
    world.save(out/'cathedral-world.png',transparency=0)
    tiles=[bytes(64)];lookup={tiles[0]:0};grid=[]
    for y in range(64):
        for x in range(64):
            b=world.crop((x*8,y*8,x*8+8,y*8+8)).tobytes()
            if b not in lookup:lookup[b]=len(tiles);tiles.append(b)
            grid.append(lookup[b])
    background_tiles=len(tiles)
    # Small independent HUD font; original game UI is not included.
    glyphs='ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 /:-'
    fg=min(range(1,256),key=lambda i:sum((pal[i][k]-230)**2 for k in range(3)))
    hudbg=min(range(1,256),key=lambda i:sum((pal[i][k]-12)**2 for k in range(3)))
    for ch in glyphs:
        im=Image.new('P',(8,8),hudbg);d=ImageDraw.Draw(im);d.text((1,-2),ch,fill=fg,font_size=8)
        tiles.append(im.tobytes())
    assert len(tiles)<=864,('Too many background tiles',len(tiles))
    # GBA 512x512 map is four 32x32 screen blocks.
    bgmap=[]
    for by in range(2):
        for bx in range(2):
            for y in range(32):
                for x in range(32):bgmap.append(grid[(by*32+y)*64+bx*32+x])
    blobs={'bg_tiles':b''.join(tiles),'bg_map':struct.pack('<4096H',*bgmap),'bg_palette':rgb15(pal)}
    obj_pal=[];meta={}
    for actor,states in [('warrior',['as','aw','at']),('zombie',['n','w','a'])]:
        groups={state:[list(sorted((src/actor/state/str(d)).glob('*.png'))) for d in range(8)] for state in states}
        images=[Image.open(p) for group in groups.values() for direction in group for p in direction]
        palette,lut=obj_palette(images,pal);obj_pal.extend(palette)
        if actor=='zombie':zombie_palette=palette
        for state,group in groups.items():
            result=bytearray();count=len(group[0]);meta[actor+'_'+state]=count
            for direction in group:
                assert len(direction)==count
                for p in direction:
                    im=Image.open(p);canvas=Image.new('P',(128,128),0)
                    assert im.height<=128
                    canvas.paste(im,((128-im.width)//2,128-im.height))
                    canvas=canvas.resize((64,64),Image.Resampling.NEAREST).point(lut)
                    result.extend(tile_bytes(canvas,4))
            blobs[actor+'_'+state]=bytes(result)
        if actor=='warrior':
            im=Image.open(groups['as'][0][0]);im=im.resize((im.width//2,im.height//2),Image.Resampling.NEAREST).point(lut)
            im.putpalette([v for c in palette for v in c]+[0]*720);im.save(out/'warrior-gba.png',transparency=0)
    pillar=piece(150);pillar_palette,lut=obj_palette([pillar],pal);obj_pal.extend(pillar_palette)
    pillar=pillar.point(lut)
    blobs['pillar_top']=tile_bytes(pillar.crop((0,0,32,64)),4)
    blobs['pillar_bottom']=tile_bytes(pillar.crop((0,64,32,80)),4)
    # Tiny independently drawn pickup indicators; one palette bank.
    obj_pal.extend([(0,0,0),(255,210,60),(255,245,150),(220,30,35),(230,230,240),(30,90,255),(255,230,30)]+[(0,0,0)]*9)
    obj_pal.extend([(0,0,0)]+[tuple(min(255,round(c+(255-c)*0.18)) for c in rgb) for rgb in zombie_palette[1:]])
    drops=[]
    for kind in range(4):
        im=Image.new('P',(8,8),0);d=ImageDraw.Draw(im)
        if kind:d.rectangle((2,2,5,6),fill={1:3,2:5,3:6}[kind]);d.rectangle((3,0,4,2),fill=4)
        else:d.ellipse((1,3,6,6),fill=1);d.line((2,3,5,3),fill=2)
        drops.append(tile_bytes(im,4))
    blobs['drops']=b''.join(drops);blobs['obj_palette']=rgb15(obj_pal)
    stairs=Image.new('P',(16,16),0);pen=ImageDraw.Draw(stairs)
    for step in range(4):pen.line((2+step*2,13-step*3,13,13-step*3),fill=4)
    pen.line((3,5,3,1),fill=1);pen.line((1,3,3,1,5,3),fill=1)
    blobs['stairs']=tile_bytes(stairs,4)
    for obsolete in ('zombie_n_outline.bin','zombie_w_outline.bin'):
        (out/obsolete).unlink(missing_ok=True)
    for n,b in blobs.items():(out/(n+'.bin')).write_bytes(b)
    asm=['.section .rodata.assets,"a",%progbits']
    for n in blobs:asm += ['.balign 4',f'.global asset_{n}',f'asset_{n}:',f'.incbin "{(out/(n+".bin")).resolve()}"']
    (out/'assets.s').write_text('\n'.join(asm)+'\n')
    h=['#ifndef ASSETS_H','#define ASSETS_H','#include <stdint.h>']
    for n,b in blobs.items():h += [f'extern const uint8_t asset_{n}[];',f'#define ASSET_{n.upper()}_BYTES {len(b)}']
    for n,c in meta.items():h += [f'#define FRAMES_{n.upper()} {c}']
    h += [f'#define HUD_FIRST_TILE {background_tiles}',f'#define HUD_GLYPHS "{glyphs}"','#endif']
    (out/'assets.h').write_text('\n'.join(h)+'\n')
    report={'background_tiles':background_tiles,'total_bg_tiles':len(tiles),'bg_tile_bytes':len(blobs['bg_tiles']),
        'animation_frames':meta,'asset_bytes':sum(map(len,blobs.values())),
        'sprite_format':'64x64 4bpp, 16-color palette per actor type; source images scaled 50%',
        'room':'hand-built cathedral test room, not original level generation',
        'floor_brightness':0.50,'target_style':'18% brighter body, no outline'}
    (out/'asset-report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
if __name__=='__main__':main()
