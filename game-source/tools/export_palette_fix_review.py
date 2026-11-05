"""Exact before/after Stage37 palette review, using the shipping C decoder."""
import argparse, json, struct, tempfile
from pathlib import Path
from PIL import Image, ImageDraw
from export_monster_review import ROOT, Decoder, decompress, picture, font, rgb

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('review',type=Path,help='Directory containing the current monster manifest')
    ap.add_argument('output',type=Path)
    args=ap.parse_args()
    rows=json.loads((args.review/'manifest.json').read_text())['monsters']
    out=Image.new('RGB',(1104,1110),(20,23,29));draw=ImageDraw.Draw(out)
    draw.text((24,18),'26 / 39 / 57  -  PALETTE CORRECTION',font=font(28),fill='white')
    draw.text((24,58),'Actual sprite pixels | idle direction 0 | integer nearest-neighbor zoom',font=font(16),fill=(167,181,198))
    labels=['NORMAL / BEFORE','NORMAL / AFTER','SELECTED / BEFORE','SELECTED / AFTER']
    for col,label in enumerate(labels):
        draw.text((24+col*272,101),label,font=font(17),fill=(202,214,228))
    with tempfile.TemporaryDirectory() as tmp:
        decoder=Decoder(tmp,(ROOT/'generated/hero_frame_pool.lz').read_bytes())
        for row,kind in enumerate((26,39,57)):
            r=rows[kind];key='devil_kin_spear' if kind==39 else 'obsidian_lord'
            clip=(ROOT/'generated'/(r['idle']+'.lz')).read_bytes()
            magic,count=struct.unpack_from('<II',clip)
            count=count if magic in (0x31525053,0x32525053) else magic
            raw=decoder.frames(clip,count)[0]
            palettes=[]
            for folder in ('source_assets/palette_style/stage36','generated'):
                palettes.append(struct.unpack('<32H',decompress((ROOT/folder/(key+'_palette.lz')).read_bytes())))
            y=141+row*308
            title=f"{kind:02d}  {r['name']}"+('  / PACK MEMBER' if kind==57 else '')
            draw.text((24,y),title,font=font(21),fill='white')
            for col,(bank,version) in enumerate(((0,0),(0,1),(1,0),(1,1))):
                pal=palettes[version][bank*16:bank*16+16]
                sprite=picture(raw,pal);sprite=sprite.crop(sprite.getbbox())
                scale=min(8,220//max(sprite.size))
                sprite=sprite.resize((sprite.width*scale,sprite.height*scale),Image.Resampling.NEAREST)
                x=16+col*272
                draw.rectangle((x,y+35,x+259,y+271),fill=(32,37,45))
                out.paste(sprite,(x+(260-sprite.width)//2,y+41+(220-sprite.height)//2),sprite)
                changed_index=1 if kind==39 else 15
                c=rgb(pal[changed_index])
                draw.rectangle((x+10,y+277,x+31,y+295),fill=c)
                draw.text((x+40,y+276),'highlight '+str(c),font=font(12),fill=(178,191,207))
    draw.text((24,1080),'Only the isolated white highlight changes. Normal + selected palettes are corrected.',font=font(15),fill=(178,191,207))
    args.output.parent.mkdir(parents=True,exist_ok=True);out.save(args.output)
    print(args.output)

if __name__=='__main__':main()
