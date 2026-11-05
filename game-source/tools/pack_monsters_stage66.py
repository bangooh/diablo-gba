#!/usr/bin/env python3
"""Append Fiend/Gloom and all four retail Snake variants (stable IDs58..63).

Full native directional N/W/A/H/D frame counts; shared family geometry and
native translated palettes. SPR2 tiles use the existing fast actor decoder.
The old floor stream is relocated, never requantized or recompressed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

from pack_monster_uniques import split_fields, stats
from pack_audio_stage66 import write, sha

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'source_assets/monster_stage66'
OUT=ROOT/'generated'
NAMES=('Fiend','Gloom','Cave Viper','Fire Drake','Gold Viper','Azure Drake')
KEYS=('fiend','gloom','cave_viper','fire_drake','gold_viper','azure_drake')

def array(text,name):
    return list(map(int,re.search(name+r'\[\]\s*=\s*\{([^}]+)',text)[1].split(',')))

def relocate(blob,start):
    count=struct.unpack_from('<I',blob)[0]
    old_header=4+4*count;delta=start-old_header
    body=bytearray(blob[old_header:])
    pointers=list(struct.unpack_from('<'+'I'*count,blob,4))
    for at in set(pointers):
        assert blob[at+3]==2
        for field in (4,8):
            value=struct.unpack_from('<I',blob,at+field)[0]
            struct.pack_into('<I',body,at-old_header+field,value+delta)
    return body,[p+delta for p in pointers]

def assemble(records):
    old=(SRC/'floor-baseline.rle').read_bytes()
    added=(OUT/'monster66_corpses.rle').read_bytes()
    death=(SRC/'monster_death_baseline.h').read_text()
    base=array(death,'monster_death_base');counts=array(death,'monster_death_frames');ticks=array(death,'monster_death_ticks')
    assert len(base)==len(counts)==len(ticks)==54
    header=(SRC/'floor_decals_baseline.h').read_text()
    item=int(re.search(r'FLOOR_ITEM_BASE\s*=\s*(\d+)',header)[1]);bright=int(re.search(r'FLOOR_BRIGHT_BASE\s*=\s*(\d+)',header)[1])
    old_count=struct.unpack_from('<I',old)[0];added_count=struct.unpack_from('<I',added)[0]
    new_count=old_count+32+added_count
    first=4+4*new_count;old_body,old_ptr=relocate(old,first)
    second=first+len(old_body);padding=bytes(-second%4);second+=len(padding)
    new_body,new_ptr=relocate(added,second)
    ground=(OUT/'ground_items66.rle').read_bytes()
    assert struct.unpack_from('<I',ground)[0]==old_count-item
    third=second+len(new_body);ground_pad=bytes(-third%4);third+=len(ground_pad)
    ground_body,ground_ptr=relocate(ground,third)
    # Corpse indices54..57 alias minion bodies, keeping kinds58..63 direct.
    minions=(9,7,24,26)
    table=old_ptr[:432]+[old_ptr[k*8+d] for k in minions for d in range(8)]+new_ptr[:48]
    assert len(table)==64*8
    table+=old_ptr[432:item]+new_ptr[48:]+ground_ptr
    assert len(table)==new_count
    blob=struct.pack('<I',new_count)+struct.pack('<'+'I'*new_count,*table)+old_body+padding+new_body+ground_pad+ground_body
    write(OUT/'floor_decals.rle',blob)
    shift=80+added_count-48
    write(OUT/'floor_decals.h',f'#pragma once\nextern const uint8_t asset_floor_decals[];\nenum {{ FLOOR_ITEM_BASE={item+shift}, FLOOR_BRIGHT_BASE={bright+shift} }};\n')
    bases=[v+80 for v in base]+[base[k]+80 for k in minions]
    new_base=item+80
    for r in records:
        bases.append(new_base);new_base+=8*r['frames'][4]
    counts+= [counts[k] for k in minions]+[r['frames'][4] for r in records]
    ticks+= [ticks[k] for k in minions]+[r['rates'][4]+1 for r in records]
    write(OUT/'monster_death.h','#pragma once\n#define MONSTER_DEATH_TYPES 64\n'+
          'static const uint16_t monster_death_base[]={'+','.join(map(str,bases))+'};\n'+
          'static const uint8_t monster_death_frames[]={'+','.join(map(str,counts))+'};\n'+
          'static const uint8_t monster_death_ticks[]={'+','.join(map(str,ticks))+'};\n')
    return {'old_frames':old_count,'new_frames':new_count,'static_corpse_frames':512,
            'old_item_base':item,'new_item_base':item+shift,'bytes_delta':len(blob)-len(old),
            'old_frame_mapping':{'static':'unchanged0..431','death':'old+80','items':f'old+{shift}'}}

def catalog_records():
    source=(ROOT/'source_assets/monster_catalog/monstdat.cpp').read_text()
    table=source[source.index('MonsterData monsterdata[]'):].split('\n};',1)[0]
    rows=[split_fields(l[l.index('{')+1:l.rfind('}')]) for l in table.splitlines() if re.match(r'\s*\{\s*\d+,',l)]
    records=[]
    for index,(name,key) in enumerate(zip(NAMES,KEYS)):
        sid=next(i for i,v in enumerate(rows) if json.loads(v[10])==name);v=rows[sid]
        arr=lambda t:list(map(int,t.strip('{} ').split(',')))
        family=0 if index<2 else 5
        r=dict(kind=58+index,source_id=sid,key=key,name=name,width=int(v[0]),image_cost=int(v[1]),graphic=json.loads(v[2]),translation=None if v[7]=='NULL' else json.loads(v[7]),
               frames=arr(v[8]),rates=arr(v[9]),min_floor=15*int(v[11])//30+1,max_floor=15*int(v[12])//30+1,level=int(v[13]),hp_min=int(v[14]),hp_max=int(v[15]),ai=v[16],
               family=family,intelligence=int(v[18]),hit=int(v[19]),impact=int(v[20]),damage_min=int(v[21]),damage_max=int(v[22]),armor=int(v[27]),xp=int(v[33]),undead=v[28]=='MC_UNDEAD',
               immune=sum(1<<i for i,n in enumerate(('MAGIC','FIRE','LIGHTNING')) if 'IMMUNE_'+n in v[29]),resist=sum(1<<i for i,n in enumerate(('MAGIC','FIRE','LIGHTNING')) if 'RESIST_'+n in v[29]),
               path_flags=(1 if 'MFLAG_SEARCH' in v[17] else 0)|(2 if 'MFLAG_CAN_OPEN_DOOR' in v[17] else 0))
        records.append(r)
    return records

def import_assets(mpq,baseline):
    from PIL import Image
    import numpy as np
    import mpyq
    from extract_assets import read_mpq,cl2_frames
    from pack_gba_assets import obj_palette,tile_bytes,rgb15
    from pack_hot_monsters import raw_tiles
    from lz77 import compress,decompress
    from floor_codec import encode as floor_encode
    archive=mpyq.MPQArchive(str(mpq),listfile=False)
    source=(ROOT/'source_assets/monster_catalog/monstdat.cpp').read_text()
    table=source[source.index('MonsterData monsterdata[]'):].split('\n};',1)[0]
    rows=[split_fields(l[l.index('{')+1:l.rfind('}')]) for l in table.splitlines() if re.match(r'\s*\{\s*\d+,',l)]
    native=read_mpq(archive,'Levels\\L1Data\\L1_1.PAL');write(SRC/'native.pal',native)
    colors=np.frombuffer(native,np.uint8).reshape(256,3)
    pal=[tuple(map(int,c)) for c in colors]
    records=[];groups={};family_luts={};geometry={};audio=[];sound_ids={};header=['#pragma once','#include <stdint.h>'];arts=[];hits=[];death_shapes=[];death_palettes=[];final_shapes=[];final_palettes=[]
    bg=struct.unpack('<256H',decompress((OUT/'bg_palette.lz').read_bytes()))
    bg_rgb=np.array([[(v>>s)&31 for s in (0,5,10)] for v in bg],dtype=np.int32)*255//31
    audit=json.loads((OUT/'background-palette-review/audit.json').read_text())
    static=np.array(audit['palette']['static_slots'],dtype=np.int32);static=static[static!=0]
    bg_map=np.array([0]+[int(static[np.argmin(((bg_rgb[static]-colors[i].astype(np.float64)*.80)**2).sum(axis=1))]) for i in range(1,256)],dtype=np.uint8)
    for index,(name,key) in enumerate(zip(NAMES,KEYS)):
        sid=next(i for i,v in enumerate(rows) if json.loads(v[10])==name);v=rows[sid]
        arr=lambda t:list(map(int,t.strip('{} ').split(',')))
        family=0 if index<2 else 5
        r=dict(kind=58+index,source_id=sid,key=key,name=name,width=int(v[0]),image_cost=int(v[1]),graphic=json.loads(v[2]),translation=None if v[7]=='NULL' else json.loads(v[7]),
               frames=arr(v[8]),rates=arr(v[9]),min_floor=15*int(v[11])//30+1,max_floor=15*int(v[12])//30+1,level=int(v[13]),hp_min=int(v[14]),hp_max=int(v[15]),ai=v[16],
               family=family,intelligence=int(v[18]),hit=int(v[19]),impact=int(v[20]),damage_min=int(v[21]),damage_max=int(v[22]),armor=int(v[27]),xp=int(v[33]),undead=v[28]=='MC_UNDEAD',
               immune=sum(1<<i for i,n in enumerate(('MAGIC','FIRE','LIGHTNING')) if 'IMMUNE_'+n in v[29]),resist=sum(1<<i for i,n in enumerate(('MAGIC','FIRE','LIGHTNING')) if 'RESIST_'+n in v[29]),
               path_flags=(1 if 'MFLAG_SEARCH' in v[17] else 0)|(2 if 'MFLAG_CAN_OPEN_DOOR' in v[17] else 0))
        records.append(r);family_key='bat66' if index<2 else 'snake66'
        trn=read_mpq(archive,r['translation']) if r['translation'] else bytes(range(256));write(SRC/(key+'.trn'),trn)
        if family_key not in groups:
            family_groups={}
            for state in 'nwahd':
                data=read_mpq(archive,r['graphic'].replace('%c',state));write(SRC/(family_key+'_'+state+'.cl2'),data)
                family_groups[state]=cl2_frames(data,r['width'])
            groups[family_key]=family_groups
            palette,lut=obj_palette([im for group in family_groups.values() for direction in group for im in direction],pal)
            family_luts[family_key]=lut;geometry[family_key]={}
            for state in 'nwah':
                frames=[]
                for direction in family_groups[state]:
                    for im in direction:
                        side=max(128,r['width'],im.height);canvas=Image.new('P',(side,side));canvas.paste(im,((side-im.width)//2,side-im.height))
                        frames.append(tile_bytes(canvas.resize((64,64),Image.Resampling.NEAREST).point(lut),4))
                blob=raw_tiles(frames);write(OUT/(family_key+'_'+state+'.lz'),blob)
                header.append('extern const uint8_t asset_'+family_key+'_'+state+'[];')
                geometry[family_key][state]={'frames':len(frames),'bytes':len(blob),'decoded_sha256':sha(b''.join(frames))}
        # Fit each native TRN to the same geometry's color clusters.
        lut=family_luts[family_key];weights=np.zeros(256,dtype=np.int64)
        for g in groups[family_key].values():
            for direction in g:
                for im in direction:weights+=np.bincount(np.frombuffer(im.tobytes(),np.uint8),minlength=256)
        mapped=colors[np.frombuffer(trn,np.uint8)].astype(np.int64);fitted=[(0,0,0)]
        for slot in range(1,16):
            mask=np.array(lut)==slot;total=weights[mask].sum()
            fitted.append(tuple(map(int,np.floor((weights[mask,None]*mapped[mask]).sum(axis=0)/max(1,total)+.5))))
        bright=[(0,0,0)]+[tuple(min(255,round(c+(255-c)*.18)) for c in rgb) for rgb in fitted[1:]]
        symbol='asset_'+key+'66_palette';write(OUT/(key+'66_palette.lz'),compress(rgb15(fitted+bright)));header.append('extern const uint8_t '+symbol+'[];')
        bank,bright_bank=(1,4) if index<2 else (10,14)
        arts.append('{'+','.join('asset_'+family_key+'_'+s for s in 'nwa')+','+symbol+','+','.join(map(str,r['frames'][:3]+[bank,bright_bank]))+',"'+name.upper()+'"},')
        # Snake CL2 contains six frames/direction, monstdat plays five.
        # Keep the full file stride distinct from the gameplay hit clock.
        hits.append('{asset_'+family_key+'_h,'+str(len(groups[family_key]['h'][0]))+'},')
        mapping=bytes([0]+[int(bg_map[trn[i]]) if trn[i] else int(static[np.argmin((bg_rgb[static]**2).sum(axis=1))]) for i in range(1,256)])
        for direction in groups[family_key]['d']:
            for im in direction:
                side=max(128,r['width'],im.height);canvas=Image.new('P',(side,side));canvas.paste(im,((side-im.width)//2,side-im.height));shape=canvas.resize((64,64),Image.Resampling.NEAREST).tobytes()
                death_shapes.append(shape);death_palettes.append(mapping)
            final_shapes.append(shape);final_palettes.append(mapping)
        template=json.loads(v[4]);ids=[]
        for state in 'ahd':
            variants=[]
            for variant in (1,2):
                path=template.replace('%c',state).replace('%i',str(variant))
                if path.lower() not in sound_ids:
                    effect=176+len(audio);stem='audio_monster66_'+str(effect);wav=SRC/(stem+'.wav');write(wav,read_mpq(archive,path));sound_ids[path.lower()]=effect
                    audio.append({'symbol':'asset_'+stem,'effect_id':effect,'source':str(wav.relative_to(ROOT)),'mpq_path':path})
                variants.append(sound_ids[path.lower()])
            ids.append(variants)
        r['audio_ids']=ids
    blob,report=floor_encode(final_shapes+death_shapes,final_palettes+death_palettes);write(OUT/'monster66_corpses.rle',blob)
    write(SRC/'catalog.json',json.dumps({'source_revision':'9f01757243412bb154d02d9af65c804159e6e0ea','records':records,'geometry':geometry},indent=2)+'\n')
    write(SRC/'audio.json',json.dumps({'clips':audio},indent=2)+'\n')
    header += ['static const uint8_t monster66_audio_ids[6][3][2]='+json.dumps([r['audio_ids'] for r in records]).replace('[','{').replace(']','}')+';']
    write(OUT/'monsters_stage66.h','\n'.join(header)+'\n')
    write(OUT/'monster66_art.inc','\n'.join(arts)+'\n');write(OUT/'monster66_hit.inc','\n'.join(hits)+'\n')
    write(OUT/'monster66_defs.inc',',\n'.join(stats(r) for r in records)+'\n')
    write(OUT/'monster66_undead.inc',','.join(str(int(r['undead'])) for r in records)+'\n')
    for name,target in [('floor_decals.rle','floor-baseline.rle'),('monster_death.h','monster_death_baseline.h'),('floor_decals.h','floor_decals_baseline.h')]:write(SRC/target,(baseline/'generated'/name).read_bytes())

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--mpq',type=Path);p.add_argument('--baseline',type=Path);args=p.parse_args()
    if args.mpq:
        assert args.baseline;import_assets(args.mpq,args.baseline)
    catalog=json.loads((SRC/'catalog.json').read_text());report=assemble(catalog['records'])
    hits=['{asset_'+('bat66' if r['kind']<60 else 'snake66')+'_h,'+str(catalog['geometry']['bat66' if r['kind']<60 else 'snake66']['h']['frames']//8)+'},' for r in catalog['records']]
    write(OUT/'monster66_hit.inc','\n'.join(hits)+'\n')
    defs=(OUT/'monster_defs.inc').read_text()
    if '#include "monster66_defs.inc"' not in defs:
        defs=defs.replace('#include "monster_unique_defs.inc"','#include "monster_unique_defs.inc"\n,\n#include "monster66_defs.inc"')
        defs=defs.replace('#include "monster_unique_undead.inc"','#include "monster_unique_undead.inc"\n,\n#include "monster66_undead.inc"')
        write(OUT/'monster_defs.inc',defs)
    report.update(names=NAMES,kind_ids=list(range(58,64)),geometry=catalog['geometry'],source_sha256=sha((SRC/'catalog.json').read_bytes()),floor_sha256=sha((OUT/'floor_decals.rle').read_bytes()))
    write(OUT/'monster-stage66-report.json',json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':main()
