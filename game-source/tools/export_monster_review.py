"""Export registered monster sprites with their actual current RGB555 palettes.
Read-only assets; no ROM and no palette edits. Pillow only lays out decoded pixels.
"""
from pathlib import Path
import argparse, hashlib, json, re, struct, tempfile, zipfile
from PIL import Image, ImageDraw, ImageFont
from pack_hot_monsters import Decoder
from lz77 import decompress
ROOT=Path(__file__).resolve().parents[1]
FONT='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
def font(n):return ImageFont.truetype(FONT,n)
def rgb(word):return tuple(((word>>s)&31)*255//31 for s in (0,5,10))
def picture(raw,palette):
    im=Image.new('RGBA',(64,64));pix=im.load()
    for y in range(64):
        for x in range(64):
            b=raw[((y//8)*8+x//8)*32+(y%8)*4+(x%8)//2];v=(b>>((x&1)*4))&15
            pix[x,y]=(*rgb(palette[v]),255 if v else 0)
    return im

def main():
    ap=argparse.ArgumentParser();ap.add_argument('output',type=Path);args=ap.parse_args();out=args.output;out.mkdir(parents=True,exist_ok=True)
    g=ROOT/'generated';s=(ROOT/'src/gba_mode0.c').read_text().split('static const MonsterArt monster_art[MONSTER_TYPES]={',1)[1].split('\n};',1)[0]
    for inc in ('cathedral_monster_art.inc','unique_monster_art.inc'):s=s.replace('#include "../generated/'+inc+'"',(g/inc).read_text())
    rows=[]
    for row in re.findall(r'\{([^{}]+)\}',s):
        fields=[f.strip() for f in row.split(',')];assert len(fields)==10
        rows.append({'kind':len(rows),'idle':fields[0].removeprefix('asset_'),'palette':fields[3].removeprefix('asset_'),'bank':int(fields[7]),'bright':int(fields[8]),'name':fields[9].strip('"')})
    assert len(rows)==58
    samples={};source_hashes={};dest=out/'monsters';dest.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        decoder=Decoder(tmp,(g/'hero_frame_pool.lz').read_bytes())
        for r in rows:
            clip=(g/(r['idle']+'.lz')).read_bytes();magic,count=struct.unpack_from('<II',clip);count=count if magic in (0x31525053,0x32525053) else magic
            assert count%8==0;r['idle_frames_per_direction']=count//8
            pn='obj_palette' if r['palette']=='0' else r['palette'];p=(g/(pn+'.lz')).read_bytes();pal=struct.unpack('<'+'H'*(len(decompress(p))//2),decompress(p))
            normal=pal[r['bank']*16:r['bank']*16+16] if pn=='obj_palette' else pal[:16]
            selected=pal[r['bright']*16:r['bright']*16+16] if pn=='obj_palette' else pal[16:32]
            assert len(normal)==len(selected)==16 and normal[0]==selected[0]==0
            raw=decoder.frames(clip,count);normal_images=[picture(raw[d*r['idle_frames_per_direction']],normal) for d in range(8)];selected_images=[picture(raw[d*r['idle_frames_per_direction']],selected) for d in range(8)]
            r['normal_rgb555']=list(normal);r['selected_rgb555']=list(selected);key=r['idle'][:-2];r['asset_key']=key
            slug=f"{r['kind']:02d}-{key}";normal_images[0].save(dest/(slug+'-native.png'))
            sheet=Image.new('RGB',(1056,368),(28,31,37));d=ImageDraw.Draw(sheet);d.text((16,10),f"{r['kind']:02d}  {r['name']}  |  {key}",font=font(20),fill='white');d.text((16,38),'Idle frame 0 / all 8 directions / 2x nearest-neighbor / normal + selected',font=font(15),fill=(176,188,200))
            for row,imgs in enumerate((normal_images,selected_images)):
                for i,im in enumerate(imgs):sheet.paste(im.resize((128,128),Image.Resampling.NEAREST),(16+i*128,68+row*144),im.resize((128,128),Image.Resampling.NEAREST))
            for i,c in enumerate(normal):d.rectangle((16+i*30,348,44+i*30,363),fill=rgb(c))
            for i,c in enumerate(selected):d.rectangle((548+i*30,348,576+i*30,363),fill=rgb(c))
            sheet.save(dest/(slug+'-8-directions.png'))
            samples[r['kind']]=(normal_images,selected_images,raw,normal,selected)
            source_hashes[r['idle']]=hashlib.sha256(clip).hexdigest();source_hashes[pn]=hashlib.sha256(p).hexdigest()
    pages=[]
    for page in range(3):
        subset=rows[page*20:(page+1)*20];im=Image.new('RGB',(1168,1068),(20,23,29));d=ImageDraw.Draw(im)
        d.text((24,15),f'MONSTER PALETTE REVIEW  {page+1}/3',font=font(28),fill='white');d.text((24,51),'Current game assets | left: normal / right: selected | idle direction 0 | 2x nearest',font=font(16),fill=(171,185,200))
        for j,r in enumerate(subset):
            x=16+(j%4)*288;y=88+(j//4)*194;d.rounded_rectangle((x,y,x+278,y+184),radius=8,fill=(32,37,45))
            name=f"{r['kind']:02d}  {r['name']}";size=16
            while d.textlength(name,font=font(size))>260:size-=1
            d.text((x+10,y+8),name,font=font(size),fill='white');d.text((x+10,y+30),r['asset_key']+(' [pack member]' if r['kind']>=54 else ''),font=font(11),fill=(151,175,198))
            for k in range(2):
                sprite=samples[r['kind']][k][0].resize((128,128),Image.Resampling.NEAREST);im.paste(sprite,(x+10+k*132,y+51),sprite)
        name=f'monsters-{page+1:02d}.png';im.save(out/name);pages.append(name)
    # Both ambiguous names shown explicitly; exporting never changes assets.
    carver=rows[39];assert carver['name']=='CARVER' and carver['asset_key']=='devil_kin_spear'
    panel=Image.new('RGB',(1120,630),(24,28,35));d=ImageDraw.Draw(panel);d.text((22,15),'CARVER / FALLEN FAMILY - CURRENT PALETTES',font=font(25),fill='white');d.text((22,52),'In-game CARVER = devil_kin_spear. carver_sword is displayed as DEVIL KIN.',font=font(17),fill=(203,212,222));d.text((22,75),'Cropped pixel zoom | top: normal / bottom: selected | swatches: normal palette',font=font(14),fill=(160,183,206))
    family=[2,39,30,38,31]
    for col,kind in enumerate(family):
        r=rows[kind];x=16+col*220;d.text((x,97),r['name'],font=font(17),fill='white');d.text((x,121),r['asset_key'],font=font(12),fill=(166,187,210))
        for row in range(2):
            sprite=samples[kind][row][0];bbox=sprite.getbbox();sprite=sprite.crop(bbox);scale=min(8,192//max(sprite.size));sprite=sprite.resize((sprite.width*scale,sprite.height*scale),Image.Resampling.NEAREST);panel.paste(sprite,(x+(192-sprite.width)//2,151+row*205+(192-sprite.height)//2),sprite)
        pal=samples[kind][3]
        for i,c in enumerate(pal):
            xx=x+(i%8)*24;yy=566+(i//8)*24;d.rectangle((xx,yy,xx+21,yy+21),fill=rgb(c))
    panel.save(out/'carver-palette-review.png')
    report={'registered_monster_kinds':58,'distinct_body_catalog_entries':50,'unique_leaders':4,'pack_member_kinds':4,'poses':'idle frame0, all8 directions','render':'actual actor_frame C decoder; current normal and selected RGB555 palettes; full light, no terrain/occlusion','export_does_not_modify_assets':True,'rom_generated':False,'carver_display_kind':39,'carver_asset_key':'devil_kin_spear','naming_note':'Internal carver_sword is displayed as DEVIL KIN; not CARVER.','pages':pages,'source_sha256':source_hashes,'monsters':rows}
    (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    (out/'README.txt').write_text('Current registered monsters: 58 kinds (50 body catalog +4 unique leaders +4 pack members).\nNormal and selected palettes shown at full brightness. Each individual sheet contains idle frame0 in all8 directions, normal above selected. Native PNGs have transparent backgrounds. Animation sequences, death sprites, and lighting attenuation are not part of this palette review.\nCARVER uses the internal key devil_kin_spear; carver_sword is displayed as DEVIL KIN. Exporting does not change assets or generate a ROM.\n')
    zip_path=out.parent/'diablo-gba-all-monsters-review.zip'
    with zipfile.ZipFile(zip_path,'w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted(out.rglob('*')):
            if p.is_file():z.write(p,p.relative_to(out))
    print(json.dumps({'output':str(out),'zip':str(zip_path),'registered_kinds':58,'pngs':len(list(out.rglob('*.png')))}))
if __name__=='__main__':main()
