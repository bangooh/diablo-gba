"""Read-only Stage41 asset checks for GBA hardware offload candidates.

Uses the shipping C decoders. Does not patch runtime assets or build a ROM.
Requires numpy. Run: python3 tools/audit_hardware_candidates.py --output PATH
"""
import argparse
import ctypes
import hashlib
import json
import re
import struct
import tempfile
from pathlib import Path

import numpy as np
from pack_hot_monsters import Decoder

ROOT = Path(__file__).resolve().parents[1]
GEN = ROOT / 'generated'


def pixels(raw, width):
    tiles = width//8
    data = np.frombuffer(raw, dtype=np.uint8).reshape(tiles,tiles,8,4)
    indices = np.empty((tiles,tiles,8,8),dtype=np.uint8)
    indices[:,:,:,::2] = data & 15
    indices[:,:,:,1::2] = data >> 4
    return indices.transpose(0,2,1,3).reshape(width,width)


def mirror_stats(frames, width):
    full, flipped, crops, crop_flips = set(),set(),set(),set()
    for raw in frames:
        image = pixels(raw,width)
        if not image.any():
            continue
        normal,flip = image.tobytes(), image[:,::-1].tobytes()
        full.add(normal);flipped.add(min(normal,flip))
        ys,xs = np.nonzero(image)
        crop = image[ys.min():ys.max()+1,xs.min():xs.max()+1]
        key=(crop.shape,crop.tobytes());other=(crop.shape,crop[:,::-1].tobytes())
        crops.add(key);crop_flips.add(min(key,other))
    return {'frames':len(frames),'unique_nonempty_full_frames':len(full),
            'additional_full_Hflip_merges':len(full)-len(flipped),
            'unique_nonempty_crops':len(crops),
            'additional_Hflip_merges_after_origin_adjustment':len(crops)-len(crop_flips)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    output=parser.parse_args().output;output.mkdir(parents=True,exist_ok=True)
    hashes={}
    def read(path):
        path=ROOT/path;data=path.read_bytes()
        hashes[str(path.relative_to(ROOT))]=hashlib.sha256(data).hexdigest()
        return data

    source=read('src/gba_mode0.c').decode()
    table=source.split('static const MonsterArt monster_art[MONSTER_TYPES]={',1)[1].split('\n};',1)[0]
    for name in ('cathedral_monster_art.inc','unique_monster_art.inc'):
        table=table.replace('#include "../generated/'+name+'"',read('generated/'+name).decode())
    rows=[row.split(',') for row in re.findall(r'\{([^{}]+)\}',table)]
    assert len(rows)==58
    clips=sorted({field.strip().removeprefix('asset_') for row in rows for field in row[:3]})
    report={'status':'read-only audit; no runtime implementation or new FPS benchmark',
            'source_sha256':hashes,'monster_mirror_scope':'58 registered kinds; unique idle/walk/attack clips only; no approximate color/geometry changes',
            'monster_clips':[],'HUD_globes':{},'effect_art':{}}
    with tempfile.TemporaryDirectory(prefix='hardware-audit-') as temporary:
        decoder=Decoder(temporary,read('generated/hero_frame_pool.lz'))
        for name in clips:
            blob=read('generated/'+name+'.lz')
            magic,count=struct.unpack_from('<II',blob)
            if magic not in (0x31525053,0x32525053):count=magic
            frames=decoder.frames(blob,count)
            stats=mirror_stats(frames,64)
            stats.update(asset=name,encoded_clip_bytes=len(blob))
            report['monster_clips'].append(stats)
        c=decoder.library
        c.lz_block.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint]
        buffer=ctypes.create_string_buffer(1024)
        for area in ('dungeon','town'):
            blob=read(f'generated/control_globes_{area}.lz')
            count=struct.unpack_from('<I',blob)[0];assert count==68
            frames=[]
            for index in range(count):
                assert c.lz_block(blob,index,buffer,1024)==1024
                frames.append(buffer.raw)
            unique={frame[t:t+64] for frame in frames for t in range(0,1024,64)}
            changes=[]
            for side in range(2):
                sequence=frames[side*34:(side+1)*34]
                changes.append([sum(a[t:t+64]!=b[t:t+64] for t in range(0,1024,64))
                                for a,b in zip(sequence,sequence[1:])])
            report['HUD_globes'][area]={'encoded_bytes':len(blob),'frames':68,
                'frame_pixels':1024,'unique_8bpp_tiles':len(unique),
                'all_tiles_resident_VRAM_bytes':len(unique)*64,
                'current_two_globes_VRAM_bytes':2048,
                'tile_reference_bytes_if_16bit':68*16*2,
                'adjacent_level_changed_tiles_mean':[sum(v)/len(v) for v in changes],
                'adjacent_level_changed_tiles_max':[max(v) for v in changes],
                'warning':'Tile count is not a compressed ROM saving estimate; all-resident hardware tilemap design does not fit current VRAM layout.'}

    header=read('generated/spell_expansion_assets.h').decode()
    for name,count in [('spell_effect_tiles',52),('spell_arrow_tiles',16)]:
        body=re.search(name+r'\[\d+\]\[32\]=\{(.*?)\};',header,re.S)[1]
        words=[int(v,0) for v in re.findall(r'0x[0-9a-fA-F]+|\b\d+\b',body)]
        assert len(words)==count*32
        frames=[struct.pack('<32I',*words[i*32:(i+1)*32]) for i in range(count)]
        stats=mirror_stats(frames,16)
        stats['exact_unique_full_frames_including_empty']=len(set(frames))
        report['effect_art'][name]=stats

    # Explicit byte-cost examples, not emulator results or FPS predictions.
    report['projectile_shared_OBJ_example']={'simultaneous_projectiles':8,
        'bytes_per_16x16_4bpp_frame':128,'current_per_draw_upload_bytes':8*128,
        'one_shared_frame_upload_bytes_if_all_eight_match':128,
        'saved_bytes_on_shared_frame_change':7*128,
        'cached_unchanged_frame_upload_bytes':0,
        'current_projectile_portal_shield_VRAM_bytes':40*32,
        'ten_cached_source_pointers_EWRAM_bytes_on_GBA':10*4,
        'actual_runtime_gain_measured':False}
    report['audio']={'sample_rate_Hz':16384,'current_timer2_sample_period':16,
        'active_effect_timer2_IRQ_rate':1024,
        'proposed_double_buffer_example_bytes':512,
        'proposed_half_buffer_samples':256,
        'proposed_half_buffer_service_rate_Hz':64,
        'deadline_ms':1000*256/16384,
        'warning':'Mixing remains on CPU. DMA does not enforce sample length or ring wrapping; existing Timer2/3 clock must be redesigned; service cannot depend on slow render frames.'}
    report['monster_mirror_totals']={
        'clips':len(clips),'decoded_frames':sum(s['frames'] for s in report['monster_clips']),
        'additional_full_Hflip_merges':sum(s['additional_full_Hflip_merges'] for s in report['monster_clips']),
        'additional_Hflip_merges_after_origin_adjustment':sum(s['additional_Hflip_merges_after_origin_adjustment'] for s in report['monster_clips']),
        'scope_note':'Per clip only. Existing shared frame-pool aliasing is not counted as extra ROM savings; hit/special/death and hero clips excluded.'}
    for name in ['src/hardware_light.inc','src/lighting.inc','src/audio.c','src/actor_frames.c',
                 'src/lz.c','src/death_sprites.inc','src/automap_raster.inc','src/frontend.inc',
                 'src/ending.inc','src/dungeon.c','src/world.c','src/render_coords.h',
                 'src/inventory.inc','src/infopanel.inc','src/start.s','link.ld','build.py']:
        read(name)
    report['stage39_link_budget']=json.loads(read('generated/stage39-validation/budgets.json'))
    (output/'asset-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('monster_mirror_totals','HUD_globes','effect_art')},indent=2))


if __name__=='__main__':main()
