"""All Diablo L/M/H player bodies; lossless shared frame pool, no ROM build.

Usage: python3 tools/pack_player_armor.py [DIABDAT.MPQ]
Requires Pillow and NumPy. No argument rebuilds from extracted source assets.
The 64x64 class-palette conversion and six selected cast frames are unchanged.
"""
from pathlib import Path
import hashlib,json,struct,sys
import numpy as np
from PIL import Image
from extract_assets import read_mpq,cl2_frames
from pack_gba_assets import tile_bytes
from lz77 import compress,compress_actor,decompress

ROOT=Path(__file__).resolve().parents[1]
WEAPONS='NUSDBMHTA'
STATES=('as','aw','at','ht','st','wl','fm','qm')
CLASSES=(('Warrior','W','obj_palette'),('Rogue','R','rogue_palette'),('Sorceror','S','sorcerer_palette'))

def sparse(data):
    result=bytearray();pos=0
    while pos<len(data):
        zero=not data[pos];n=1
        while pos+n<len(data) and n<128 and bool(data[pos+n])!=zero:n+=1
        result.append((128 if zero else 0)+n-1)
        if not zero:result.extend(data[pos:pos+n])
        pos+=n
    return bytes(result)

def converted(raw,width,lut,cls,state):
    groups=cl2_frames(raw,width);count=len(groups[0]);selected=list(range(count))
    assert len(groups)==8 and all(len(group)==count for group in groups)
    if state in ('fm','qm'):
        release=[14,12,8][cls]-1
        selected=[0,release//2,release,release+1,(release+count-1)//2,count-1]
    frames=[]
    for group in groups:
        for index in selected:
            im=group[index];canvas=Image.new('P',(128,128))
            canvas.paste(im,((128-im.width)//2,128-im.height))
            frames.append(tile_bytes(canvas.resize((64,64),Image.Resampling.NEAREST).point(lut),4))
    return frames,selected,count

def main():
    out=ROOT/'generated';archive=None
    if len(sys.argv)>1:
        import mpyq
        archive=mpyq.MPQArchive(sys.argv[1],listfile=False)
    pal_path=ROOT/'source_assets/weapon_heroes/l1_1.pal'
    if archive:pal_path.write_bytes(read_mpq(archive,'Levels\\L1Data\\L1_1.PAL'))
    rawpal=pal_path.read_bytes();pal=[tuple(rawpal[i:i+3]) for i in range(0,768,3)]
    clips={};sets=[];source_report={}
    for cls,(folder,letter,palfile) in enumerate(CLASSES):
        colors=struct.unpack('<16H',decompress((out/(palfile+'.lz')).read_bytes())[:32])
        rgb=[tuple(((color>>s)&31)*255//31 for s in (0,5,10)) for color in colors]
        lut=[0]+[min(range(1,16),key=lambda j:sum((rgb[j][k]-pal[i][k])**2 for k in range(3))) for i in range(1,256)]
        for armor in 'LMH':
            for weapon in WEAPONS:
                prefix=letter+armor+weapon;entry={'class':folder,'prefix':prefix,'states':{}}
                for state in STATES:
                    src=ROOT/'source_assets'/('spells' if state in ('fm','qm') else 'weapon_heroes')/(prefix+state.upper()+'.CL2')
                    mpqpath=f'PlrGFX\\{folder}\\{prefix}\\{prefix}{state.upper()}.CL2'
                    if archive:src.write_bytes(read_mpq(archive,mpqpath))
                    width=(128 if cls==2 else 96) if state in ('fm','qm') else (96 if state!='at' or (cls==0 and weapon=='B') else 128)
                    raw=src.read_bytes();frames,selected,original_count=converted(raw,width,lut,cls,state)
                    name=('spell_' if state in ('fm','qm') else 'hero_')+prefix.lower()+'_'+state
                    info={'asset':name,'frames':len(selected),'source_frames':original_count,'source_width':width,
                          'selected_frames':selected,'source_sha256':hashlib.sha256(raw).hexdigest(),
                          'decoded_sha256':hashlib.sha256(b''.join(frames)).hexdigest(),'mpq_path':mpqpath}
                    entry['states'][state]=info;source_report[name]=info
                    clips[name]={'class':cls,'frames':frames,'offsets':[0]*len(frames)}
                af=[9,10,12][cls]
                if weapon=='B':af=[11,7,16][cls]
                elif weapon=='A':af=[10,13,16][cls]
                elif weapon=='T' and cls<2:af=11
                elif weapon=='U' and cls==2:af=9
                assert 0<af<=entry['states']['at']['frames']
                entry['impact_frame']=af-1;sets.append(entry)
        print('converted',folder,flush=True)
    pool=bytearray(struct.pack('<II',0,0x31465053));seen={};depths=[0]*4;codecs=[0]*3
    # Same class and facing share reference candidates, across poses and weapons.
    # Every reference points backwards, and a chain is bounded at three patches.
    for cls,(folder,letter,_) in enumerate(CLASSES):
        ordered=[clips[('spell_' if state in ('fm','qm') else 'hero_')+(letter+armor+weapon).lower()+'_'+state]
                 for armor in 'LMH' for state in STATES for weapon in WEAPONS]
        max_frames=sum(len(c['frames'])//8 for c in ordered)
        for direction in range(8):
            candidates=np.empty((max_frames,2048),dtype=np.uint8);refs=[];refdepth=[]
            for clip in ordered:
                count=len(clip['frames'])//8
                for i in range(direction*count,(direction+1)*count):
                    frame=clip['frames'][i]
                    if frame in seen:clip['offsets'][i]=seen[frame][0];continue
                    value=np.frombuffer(frame,dtype=np.uint8)
                    payload=compress_actor(frame);kind=depth=0
                    best=4+len(payload)
                    if refs:
                        distance=np.count_nonzero(candidates[:len(refs)]!=value,axis=1)
                        nearest=np.argpartition(distance,min(3,len(refs)-1))[:4]
                        for j in nearest:
                            delta=sparse(bytes(np.bitwise_xor(value,candidates[j])))
                            encoded=compress(delta) if len(delta)<=2048 else delta
                            mode=2 if len(encoded)<len(delta) and len(delta)<=2048 else 1
                            body=encoded if mode==2 else delta
                            score=8+len(body)+(refdepth[j]+1)*4
                            if score<best:
                                best=score;payload=struct.pack('<I',refs[j])+body;kind=mode;depth=refdepth[j]+1
                    offset=len(pool);pool.extend(struct.pack('<BBH',kind,depth,len(payload))+payload)
                    clip['offsets'][i]=offset;seen[frame]=(offset,depth);depths[depth]+=1;codecs[kind]+=1
                    if depth<3:
                        candidates[len(refs)]=value;refs.append(offset);refdepth.append(depth)
            print('packed',folder,direction,'pool bytes',len(pool),flush=True)
    struct.pack_into('<I',pool,0,len(pool));(out/'hero_frame_pool.lz').write_bytes(pool)
    for name,clip in clips.items():
        (out/(name+'.lz')).write_bytes(struct.pack('<II',0x31525053,len(clip['offsets']))+struct.pack('<'+'I'*len(clip['offsets']),*clip['offsets']))
    header=['#pragma once'];cast=['#pragma once'];rows=[];spell_rows=[]
    for cls in range(3):
        variants=[];spell_variants=[]
        for entry in sets[cls*27:(cls+1)*27]:
            names=[];counts=[];spell=[]
            for state in STATES:
                info=entry['states'][state];name=info['asset']
                target=cast if state in ('fm','qm') else header
                target.append('extern const uint8_t asset_'+name+'[];')
                if state in ('fm','qm'):spell.append('{asset_'+name+','+str(info['frames'])+'}')
                else:names.append('asset_'+name);counts.append(str(info['frames']))
            variants.append('{'+','.join(names+counts+[str(entry['impact_frame']),str(int(entry['prefix'][-1]=='B'))])+'}')
            spell_variants.append('{'+','.join(spell)+'}')
        rows.append('{'+','.join(variants)+'}');spell_rows.append('{'+','.join(spell_variants)+'}')
    header.append('static const HeroArt hero_weapon_art[3][27]={'+',\n'.join(rows)+'};')
    cast.append('static const struct {const uint8_t *data;unsigned frames;} spell_hero_art[3][27][2]={'+',\n'.join(spell_rows)+'};')
    (out/'weapon_heroes.h').write_text('\n'.join(header)+'\n');(out/'player_spell_art.h').write_text('\n'.join(cast)+'\n')
    report={'source_revision':'devilution9f01757243412bb154d02d9af65c804159e6e0ea','families':list(WEAPONS),'armor_tiers':list('LMH'),
            'sets':sets,'unique_clips':len(clips),'packed_bytes':len(pool)+sum((out/(name+'.lz')).stat().st_size for name in clips),
            'frame_count':sum(len(c['frames']) for c in clips.values()),'unique_frames':len(seen),'frames_by_depth':depths,
            'frames_by_codec':codecs,'extra_frame_cache_bytes':0,'scratch_bytes':2048,'scratch_source':'existing load_buffer bytes 10240..12287'}
    (out/'weapon-heroes-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='sets'}),flush=True)
    from refine_hero_pool import refine_pool
    refine_pool(ROOT)

if __name__=='__main__':main()
