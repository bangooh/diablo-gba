"""Extract the specific Diablo files needed by the GBA graphics test.
Requires mpyq==0.2.5, dclimplode==0.0.1.0, Pillow.
MPQ sector decryption uses mpyq; PKWARE implode uses dclimplode.
CL2/tile formats were checked against Devilution engine.cpp/render.cpp/gendung.cpp.
"""
from pathlib import Path
import argparse,struct,json,hashlib
from PIL import Image,ImageDraw

def read_mpq(archive,name):
    import dclimplode
    h=archive.get_hash_table_entry(name)
    if h is None: raise FileNotFoundError(name)
    b=archive.block_table[h.block_table_index]
    archive.file.seek(archive.header['offset']+b.offset)
    raw=archive.file.read(b.archived_size)
    key=archive._hash(name.rsplit('\\',1)[-1],'TABLE')
    if b.flags&0x20000:key=((key+b.offset)^b.size)&0xffffffff
    def decrypt(data,k):
        end=len(data)&~3
        return archive._decrypt(data[:end],k&0xffffffff)+data[end:]
    def unpack(data,size):
        if len(data)==size:return data
        if b.flags&0x100:return dclimplode.decompressobj().decompress(data)
        raise ValueError('Unexpected compression flags')
    if not b.flags&0x300:
        # Uncompressed MPQ files have no sector-offset table, even encrypted WAVs.
        sector_size=512<<archive.header['sector_size_shift']
        result=b''.join(decrypt(raw[i:i+sector_size],key+i//sector_size) if b.flags&0x10000 else raw[i:i+sector_size] for i in range(0,b.size,sector_size))
    elif b.flags&0x1000000:
        if b.flags&0x10000:raw=decrypt(raw,key)
        result=unpack(raw,b.size)
    else:
        sector_size=512<<archive.header['sector_size_shift']
        count=(b.size+sector_size-1)//sector_size
        table=raw[:4*(count+1)]
        if b.flags&0x10000:table=decrypt(table,key-1)
        offsets=struct.unpack('<'+'I'*(count+1),table)
        assert offsets[0]==len(table) and offsets[-1]<=len(raw),(name,offsets[:3])
        parts=[]
        for i in range(count):
            data=raw[offsets[i]:offsets[i+1]]
            if b.flags&0x10000:data=decrypt(data,key+i)
            size=min(sector_size,b.size-i*sector_size)
            data=unpack(data,size)
            assert len(data)==size,(name,i,len(data),size)
            parts.append(data)
        result=b''.join(parts)
    assert len(result)==b.size
    return result

def cl2_frames(data,width,opaque_zero=0):
    result=[]
    for offset in struct.unpack_from('<8I',data):
        count=struct.unpack_from('<I',data,offset)[0]
        frame_offsets=struct.unpack_from('<'+'I'*(count+1),data,offset+4)
        frames=[]
        for a,b in zip(frame_offsets,frame_offsets[1:]):
            frame=data[offset+a:offset+b]
            start=struct.unpack_from('<H',frame)[0]
            src=frame[start:];pos=0;pixels=[]
            while pos<len(src):
                run=src[pos];pos+=1
                if run<128:pixels.extend([0]*run)
                else:
                    run=256-run
                    if run<=65:pixels.extend(v if v else opaque_zero for v in src[pos:pos+run]);pos+=run
                    else:pixels.extend([src[pos] or opaque_zero]*(run-65));pos+=1
            assert len(pixels)%width==0
            h=len(pixels)//width
            im=Image.frombytes('P',(width,h),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            frames.append(im)
        result.append(frames)
    return result

def tile_frame(data,word):
    im=Image.new('P',(32,32),0)
    if not word:return im
    index=word&4095;kind=(word>>12)&7
    a,b=struct.unpack_from('<II',data,index*4)
    src=data[a:b];pos=0
    if kind==0:
        assert len(src)==1024
        return Image.frombytes('P',(32,32),src).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    if kind==1:
        pixels=[]
        while pos<len(src):
            r=src[pos];pos+=1
            if r>=128:pixels.extend([0]*(256-r))
            else:pixels.extend(src[pos:pos+r]);pos+=r
        assert len(pixels)==1024
        return Image.frombytes('P',(32,32),bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    assert kind in (2,3,4,5),kind
    # Packed left/right triangles: each source row occupies a multiple of four bytes.
    widths=list(range(2,33,2))+(list(range(30,0,-2)) if kind in (2,3) else [32]*16)
    for row,w in enumerate(widths):
        pad=(-w)%4
        left=kind in (2,4)
        if left:pos+=pad
        line=src[pos:pos+w];pos+=w
        assert len(line)==w
        if not left:pos+=pad
        for x,c in enumerate(line):im.putpixel(((32-w if left else 0)+x,31-row),c)
    assert pos==len(src),(kind,pos,len(src))
    return im

def pieces(cel,mini):
    result=[]
    for offset in range(0,len(mini),20):
        words=struct.unpack_from('<10H',mini,offset)
        im=Image.new('P',(64,160),0)
        for j,word in enumerate(words):
            t=tile_frame(cel,word)
            im.paste(t,((j&1)*32,(j//2)*32),Image.frombytes('L',t.size,bytes(255 if v else 0 for v in t.tobytes())))
        result.append(im)
    return result

def main():
    ap=argparse.ArgumentParser();ap.add_argument('mpq');ap.add_argument('out');args=ap.parse_args()
    out=Path(args.out);out.mkdir(parents=True,exist_ok=True)
    import mpyq
    mpq=mpyq.MPQArchive(args.mpq,listfile=False)
    (out/'l1.sol').write_bytes(read_mpq(mpq,r'levels\l1data\l1.sol'))
    names=[r'levels\l1data\l1.cel',r'levels\l1data\l1.min',r'levels\l1data\l1.til',r'levels\l1data\l1_1.pal']
    names += [fr'plrgfx\warrior\wls\wls{state}.cl2' for state in ('as','aw','at')]
    names += [fr'monsters\zombie\zombie{state}.cl2' for state in ('n','w','a')]
    extracted={};manifest=[]
    for name in names:
        data=read_mpq(mpq,name);file=name.rsplit('\\',1)[-1]
        (out/file).write_bytes(data);extracted[file]=data
        manifest.append({'name':name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
    pal=extracted['l1_1.pal']
    allframes={}
    for state,width in [('as',96),('aw',96),('at',128)]:
        frames=cl2_frames(extracted['wls'+state+'.cl2'],width)
        allframes[state]=[len(x) for x in frames]
        for direction,group in enumerate(frames):
            d=out/'warrior'/state/str(direction);d.mkdir(parents=True,exist_ok=True)
            for n,im in enumerate(group):
                im.putpalette(pal);im.save(d/f'{n:02}.png',transparency=0)
        sheet=Image.new('RGB',(128*8,128),(30,30,35))
        for d,group in enumerate(frames):
            im=group[0];im.putpalette(pal)
            sheet.paste(im.convert('RGB'),(128*d+(128-im.width)//2,128-im.height),Image.frombytes('L',im.size,bytes(255 if v else 0 for v in im.tobytes())))
        sheet.save(out/f'warrior-{state}.png')
    ps=pieces(extracted['l1.cel'],extracted['l1.min'])
    for state in ('n','w','a'):
        frames=cl2_frames(extracted['zombie'+state+'.cl2'],128)
        allframes['zombie_'+state]=[len(x) for x in frames]
        for direction,group in enumerate(frames):
            d=out/'zombie'/state/str(direction);d.mkdir(parents=True,exist_ok=True)
            for n,im in enumerate(group):
                im.putpalette(pal);im.save(d/f'{n:02}.png',transparency=0)
    d=out/'pieces';d.mkdir(exist_ok=True)
    sheet=Image.new('RGB',(64*16,176*((len(ps)+15)//16)),(30,30,35));draw=ImageDraw.Draw(sheet)
    for i,im in enumerate(ps):
        im.putpalette(pal);im.save(d/f'{i+1:03}.png',transparency=0)
        x=(i%16)*64;y=(i//16)*176
        sheet.paste(im.convert('RGB'),(x,y),Image.frombytes('L',im.size,bytes(255 if v else 0 for v in im.tobytes())));draw.text((x,y+160),str(i+1),fill='white')
    sheet.save(out/'pieces-contact.png')
    (out/'manifest.json').write_text(json.dumps({'files':manifest,'frames':allframes,'pieces':len(ps)},indent=2))
    print(json.dumps({'extracted':len(manifest),'frames':allframes,'pieces':len(ps)}))
if __name__=='__main__':main()
