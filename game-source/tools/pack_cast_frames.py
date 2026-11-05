#!/usr/bin/env python3
"""Restore intermediate native FM caster/fire-ring frames without changing pixels.

The 32MiB build keeps each selected pose for at most two 20Hz game ticks.
Existing six poses, original release pose and last pose are retained. QM healing
assets and all other hero/monster assets stay byte-identical. No new RAM or codec.
Usage: python tools/pack_cast_frames.py [--root PROJECT] [--output GENERATED]
       python tools/pack_cast_frames.py --check
Rebuild the base pool with pack_player_armor.py first only when native source art
changes, then remove the obsolete generated/cast-frames-report.json and rerun
this tool. Keep the other existing asset-pipeline steps when rebuilding that
base. Never remove the manifest alone from an already restored pool.
A current manifest makes repeated invocations read-only and fast.
"""
from pathlib import Path
import argparse,hashlib,json,struct,sys

ROOT=Path(__file__).resolve().parents[1]
SELECTED=(tuple(sorted(set(range(0,20,2))|{0,6,13,14,16,19})),
          tuple(sorted(set(range(0,16,2))|{0,5,11,12,13,15})),
          tuple(sorted(set(range(0,12,2))|{0,3,7,8,9,11})))
TOTAL=(20,16,12)
RELEASE=(13,11,7)
def sha(data):return hashlib.sha256(data).hexdigest()
def atomic(path,data):
    temporary=path.with_name(path.name+'.tmp');temporary.write_bytes(data);temporary.replace(path)
def check(root,out):
    report=json.loads((out/'cast-frames-report.json').read_text())
    assert report['selection']==[list(x) for x in SELECTED]
    for name,digest in report['generated_sha256'].items():
        assert sha((out/name).read_bytes())==digest,name+': restored output changed; rebuild base assets before removing the old restoration manifest'
    for name,digest in report['source_sha256'].items():
        assert sha((root/name).read_bytes())==digest,name+': native input changed; rebuild base assets before removing the old restoration manifest'
    print('PASS: native FM frames/palettes, packed pool, clips, headers and frame schedule hashes',flush=True)
    return report
def pack(root,out):
    out.mkdir(parents=True,exist_ok=True)
    if (out/'cast-frames-report.json').exists():return check(root,out)
    import numpy as np
    from PIL import Image
    sys.path.insert(0,str(root/'tools'))
    from extract_assets import cl2_frames
    from pack_gba_assets import tile_bytes
    from refine_hero_pool import encode_nibbles
    from lz77 import decompress,compress_actor
    src=root/'generated'
    old=(src/'hero_frame_pool.lz').read_bytes();pool=bytearray(old)
    assert struct.unpack_from('<II',old)==(len(old),0x31465053)
    report=json.loads((src/'weapon-heroes-report.json').read_text())
    assert 'native_fm_frame_restoration' not in report, 'Already restored pool without its manifest: restore the matching manifest or rebuild the base assets first'
    rawpal=(root/'source_assets/weapon_heroes/l1_1.pal').read_bytes();pal=[tuple(rawpal[i:i+3]) for i in range(0,768,3)]
    source_hashes={'source_assets/weapon_heroes/l1_1.pal':sha(rawpal)}
    clips={};seen={};candidates={};stats=[]
    for cls,palfile in enumerate(('obj_palette','rogue_palette','sorcerer_palette')):
        palette=(src/(palfile+'.lz')).read_bytes();source_hashes['generated/'+palfile+'.lz']=sha(palette)
        colors=struct.unpack('<16H',decompress(palette)[:32]);rgb=[tuple(((c>>s)&31)*255//31 for s in (0,5,10)) for c in colors]
        lut=[0]+[min(range(1,16),key=lambda j:sum((rgb[j][k]-pal[i][k])**2 for k in range(3))) for i in range(1,256)]
        for entry in report['sets'][cls*27:(cls+1)*27]:
            for state in ('fm','qm'):
                info=entry['states'][state];name=info['asset'];path=root/'source_assets/spells'/(entry['prefix']+state.upper()+'.CL2');raw=path.read_bytes()
                assert sha(raw)==info['source_sha256'],str(path)+': rebuild the base assets after changing native art'
                source_hashes[str(path.relative_to(root))]=sha(raw)
                groups=cl2_frames(raw,128 if cls==2 else 96);count=len(groups[0]);assert len(groups)==8 and all(len(g)==count for g in groups)
                if state=='fm':assert count==TOTAL[cls]
                needed=set(range(count)) if state=='fm' else set(info['selected_frames']);frames={}
                for direction,group in enumerate(groups):
                    for i in needed:
                        im=group[i];canvas=Image.new('P',(128,128));canvas.paste(im,((128-im.width)//2,128-im.height))
                        frames[direction*count+i]=tile_bytes(canvas.resize((64,64),Image.Resampling.NEAREST).point(lut),4)
                clip=(src/(name+'.lz')).read_bytes();n=8*info['frames'];assert struct.unpack_from('<II',clip)==(0x31525053,n)
                refs=struct.unpack_from('<'+'I'*n,clip,8)
                for d in range(8):
                    for i,original in enumerate(info['selected_frames']):
                        frame=frames[d*count+original];offset=refs[d*info['frames']+i]
                        seen[frame]=(offset,pool[offset+1]);candidates.setdefault((cls,d),{})[frame]=(offset,pool[offset+1])
                if state=='fm':clips[name]={'frames':frames,'count':count,'cls':cls,'offsets':[0]*(8*count),'info':info}
        print('converted native FM',cls,flush=True)
    for cls in range(3):
        start=len(pool);new=0
        for d in range(8):
            group=[(n,c) for n,c in clips.items() if c['cls']==cls];initial=candidates[(cls,d)];refs=[];depths=[]
            array=np.zeros((len(initial)+sum(c['count'] for n,c in group),2048),dtype=np.uint8)
            for f,(offset,depth) in initial.items():
                if depth<3:array[len(refs)]=np.frombuffer(f,dtype=np.uint8);refs.append(offset);depths.append(depth)
            for i in SELECTED[cls]:
                for name,clip in group:
                    count=clip['count'];frame=clip['frames'][d*count+i]
                    if frame in seen:clip['offsets'][d*count+i]=seen[frame][0];continue
                    value=np.frombuffer(frame,dtype=np.uint8);raw=encode_nibbles(frame);lz=compress_actor(frame)
                    kind,depth,payload=(4,0,raw) if len(raw)<len(lz) else (0,0,lz);best=4+len(payload)
                    if refs:
                        delta=np.bitwise_xor(array[:len(refs)],value);dist=np.count_nonzero(delta,axis=1);near=np.argpartition(dist,min(3,len(refs)-1))[:4]
                        for j in near:
                            body=encode_nibbles(bytes(delta[j]));score=8+len(body)+(depths[j]+1)*4
                            if score<best:best=score;kind=3;depth=depths[j]+1;payload=struct.pack('<I',refs[j])+body
                    offset=len(pool);pool.extend(struct.pack('<BBH',kind,depth,len(payload))+payload)
                    seen[frame]=(offset,depth);clip['offsets'][d*count+i]=offset;new+=1
                    if depth<3:array[len(refs)]=value;refs.append(offset);depths.append(depth)
            print('packed FM',cls,d,'growth',len(pool)-start,flush=True)
        stats.append({'class':cls,'newFrames':new,'newBytes':len(pool)-start})
    struct.pack_into('<I',pool,0,len(pool));assert pool[4:len(old)]==old[4:]
    outputs={'hero_frame_pool.lz':bytes(pool)};clip_growth=0;frame_growth=0
    header=(src/'player_spell_art.h').read_text()
    for name,clip in clips.items():
        cls=clip['cls'];count=clip['count'];indices=SELECTED[cls];refs=[clip['offsets'][d*count+i] for d in range(8) for i in indices];assert all(refs)
        packed=struct.pack('<II',0x31525053,len(refs))+struct.pack('<'+'I'*len(refs),*refs);outputs[name+'.lz']=packed
        info=clip['info'];previous=info['frames'];clip_growth+=len(packed)-(src/(name+'.lz')).stat().st_size;frame_growth+=(len(indices)-previous)*8
        original_token='{asset_'+name+','+str(previous)+'}';assert header.count(original_token)==1;header=header.replace(original_token,'{asset_'+name+','+str(len(indices))+'}')
        info.update(frames=len(indices),selected_frames=list(indices),decoded_sha256=sha(b''.join(clip['frames'][d*count+i] for d in range(8) for i in indices)))
    schedules=[]
    for cls,indices in enumerate(SELECTED):
        schedule=[max(i for i,original in enumerate(indices) if original<=tick) for tick in range(TOTAL[cls])]
        assert indices[schedule[RELEASE[cls]]]==RELEASE[cls]
        assert max(b-a for a,b in zip(indices,indices[1:]))<=2
        schedules.append(schedule+[schedule[-1]]*(20-len(schedule)))
    header+='/* Original FM source tick -> resident pose; gameplay remains20Hz. */\nstatic const uint8_t spell_hero_cast_frame[3][20]={'+','.join('{'+','.join(map(str,row))+'}' for row in schedules)+'};\n'
    outputs['player_spell_art.h']=header.encode();report['frame_count']+=frame_growth;report['packed_bytes']+=len(pool)-len(old)+clip_growth
    report['unique_frames']+=sum(s['newFrames'] for s in stats)
    cursor=len(old)
    while cursor<len(pool):
        kind,depth,size=struct.unpack_from('<BBH',pool,cursor)
        report['frames_by_depth'][depth]+=1;report['frames_by_codec'][kind]+=1;cursor+=4+size
    assert cursor==len(pool)
    report['native_fm_frame_restoration']='cast-frames-report.json'
    outputs['weapon-heroes-report.json']=(json.dumps(report,indent=2)+'\n').encode()
    result={'format_version':1,'selection':[list(x) for x in SELECTED],'original_fm_frames':list(TOTAL),'release_frame_zero_based':list(RELEASE),'maximum_pose_ticks':2,'simulation_tick_rate':16777216/280896/3,'pool_before_bytes':len(old),'pool_after_bytes':len(pool),'pool_growth_bytes':len(pool)-len(old),'clip_growth_bytes':clip_growth,'ROM_asset_growth_bytes':len(pool)-len(old)+clip_growth,'base_pool_sha256':sha(old),'base_records_preserved_sha256':sha(old[4:]),'stats':stats,'source_sha256':source_hashes,'generated_sha256':{name:sha(data) for name,data in outputs.items()},'scope':'FM native fire-ring/player casting art only. QM healing clips and noncasting asset records unchanged. Existing palettes, 64x64 conversion, bounded3patch decoder and gameplay timings unchanged.'}
    for name,data in outputs.items():atomic(out/name,data)
    atomic(out/'cast-frames-report.json',(json.dumps(result,indent=2)+'\n').encode())
    print(json.dumps({k:result[k] for k in ('selection','ROM_asset_growth_bytes','maximum_pose_ticks')},indent=2),flush=True)
    return result
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=ROOT);p.add_argument('--output',type=Path);p.add_argument('--check',action='store_true');a=p.parse_args();root=a.root.resolve();out=(a.output or root/'generated').resolve()
    if a.check:check(root,out)
    else:pack(root,out)
if __name__=='__main__':main()
