"""Offline Stage40 BG-palette prototype and lossless artwork reuse audit.

Does not overwrite shipping assets or change the game. Run from any directory:
  python3 tools/audit_background_palettes.py --output generated/background-palette-review
Requires Pillow and a host C compiler. Uses the shipping indexed-LZ decoder.
"""
import argparse
import collections
import ctypes
import hashlib
import json
import re
import struct
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont
from floor_palette_style import palette_size

ROOT = Path(__file__).resolve().parents[1]
GEN = ROOT / 'generated'


def array(text, name):
    return list(map(int, re.search(r'\b' + name + r'\[[^;=]*\]=\{([^}]+)', text)[1].split(',')))


def canonical(data):
    """Exact color-label isomorphism; transparent zero is never relabelled."""
    colors = {0: 0}
    return bytes(colors.setdefault(v, len(colors)) for v in data)


def light_index(palette, i):
    if not i:
        return 0
    rgb = lambda c: tuple((c >> k) & 31 for k in (0, 5, 10))
    source = rgb(palette[i])
    target = tuple(v * 10 // 16 for v in source)
    return min((j for j in range(1, 256) if sum(rgb(palette[j])) <= sum(source)),
               key=lambda j: sum((a-b)**2 for a, b in zip(rgb(palette[j]), target)))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    out = ap.parse_args().output
    out.mkdir(parents=True, exist_ok=True)
    sources = {}

    def read(name):
        data = (GEN / name).read_bytes()
        sources[name] = hashlib.sha256(data).hexdigest()
        return data

    palette = struct.unpack('<256H', read('bg_palette.bin'))
    word_rgb = lambda c: tuple(((c >> k) & 31)*255//31 for k in (0, 5, 10))
    assets = {n: read(n) for n in (
        'control_panel_dungeon.bin', 'control_globes_dungeon.bin',
        'dungeon_font.bin', 'dungeon_font_large.bin', 'dungeon_overlay_font.bin',
        'dungeon_art_raw.rle', 'floor_variants.rle',
        'world_l1.rle', 'world_l2.rle', 'world_l4.rle')}
    props = []
    prop_report = []
    with tempfile.TemporaryDirectory(prefix='bg-palette-audit-') as temporary:
        shared = Path(temporary) / 'lz.so'
        subprocess.run(['cc', '-O2', '-shared', '-fPIC', str(ROOT/'src/lz.c'),
                        '-o', str(shared)], check=True)
        decoder = ctypes.CDLL(str(shared))
        decoder.lz_block.argtypes = [ctypes.c_void_p, ctypes.c_uint,
                                    ctypes.c_void_p, ctypes.c_uint]
        decoder.lz_decode.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint]
        buffer = ctypes.create_string_buffer(8192)
        assert decoder.lz_decode(read('bg_palette.lz'),buffer,len(buffer)) == 512
        assert buffer.raw[:512] == struct.pack('<256H',*palette)
        # The shipping compressed HUD has later edits not mirrored in all .bin
        # intermediates. Inspect decoded ROM bytes, not a stale packer output.
        hud_source_checks = {}
        for stem in ('control_panel_dungeon', 'dungeon_font',
                     'dungeon_font_large', 'dungeon_overlay_font'):
            blob = read(stem+'.lz')
            size = decoder.lz_decode(blob, buffer, len(buffer))
            assert size == len(assets[stem+'.bin'])
            actual = buffer.raw[:size]
            hud_source_checks[stem] = {'intermediate_matches_ROM_asset': actual == assets[stem+'.bin']}
            assets[stem+'.bin'] = actual
        blob = read('control_globes_dungeon.lz')
        globe_frames = []
        for i in range(struct.unpack_from('<I',blob)[0]):
            assert decoder.lz_block(blob,i,buffer,len(buffer)) == 1024
            globe_frames.append(buffer.raw[:1024])
        actual = b''.join(globe_frames)
        assert len(actual) == len(assets['control_globes_dungeon.bin'])
        hud_source_checks['control_globes_dungeon'] = {'intermediate_matches_ROM_asset': actual == assets['control_globes_dungeon.bin']}
        assets['control_globes_dungeon.bin'] = actual
        for name in ('prop_frames.lz', 'door_frames.lz',
                     'explosive_barrels.lz', 'wall_traps.lz'):
            blob = read(name)
            count = struct.unpack_from('<I', blob)[0]
            frames = []
            for i in range(count):
                assert decoder.lz_block(blob, i, buffer, 4096) == 4096
                frame = buffer.raw[:4096]
                props.append((name, i, frame))
                frames.append(frame)
            if name == 'prop_frames.lz':
                assert b''.join(frames) == read('prop_frames.bin')
            assets[name + '.decoded'] = b''.join(frames)
            prop_report.append({'asset': name, 'frames': count, 'encoded_bytes': len(blob)})

    # v4 floor frames contain a local palette mapping into the shared BG bank.
    # Inspect every distinct frame header; include all its palette entries.
    floor = read('floor_decals.rle')
    floor_count = struct.unpack_from('<I', floor)[0]
    offsets = set(struct.unpack_from(f'<{floor_count}I', floor, 4))
    floor_indices = set()
    for offset in offsets:
        n = palette_size(floor, offset)
        start = struct.unpack_from('<I', floor, offset+4)[0]
        floor_indices.update(floor[start:start+n])

    static_indices = set(floor_indices)
    for data in assets.values():
        static_indices.update(data)
    highlight = array(read('prop_highlight.h').decode(), 'prop_highlight')
    prop_indices = set().union(*(set(data) for _, _, data in props))
    static_indices.update(highlight[i] for i in prop_indices)
    # Runtime-generated info-font ink and automap line selection.
    nearest = lambda target: min(range(1, 256), key=lambda i: sum(
        (((palette[i] >> k) & 31)-target[j])**2 for j, k in enumerate((0, 5, 10))))
    static_indices.update(nearest((v, v, v)) for v in (8, 31))
    map_index = min((i for i in range(1, 256) if i & 15), key=lambda i: sum(
        (((palette[i] >> k) & 31)-v)**2 for k, v in zip((0, 5, 10), (26, 23, 14))))
    static_indices.add(map_index)
    # Include existing software-shading output colors as a conservative budget.
    # Runtime integration must still restrict nearest-color searches to static slots.
    static_indices.update(light_index(palette, i) for i in list(static_indices))
    static_indices.discard(0)

    quest = read('water_quest.h').decode()
    caves, foul, clean = [array(quest, name+'_lut') for name in ('caves', 'water_foul', 'water_clean')]
    state_names = ['caves', 'water_foul'] + [f'water_step_{i:02d}' for i in range(32)]
    luts = [caves, foul] + [clean[:i+1]+foul[i+1:] for i in range(32)]
    l3 = read('world_l3.rle')
    trajectories = {i: tuple(palette[lut[i]] for lut in luts) for i in set(l3) if i}
    dynamic = sorted({v for v in trajectories.values() if len(set(v)) != 1})
    fixed_colors = {palette[i] for i in static_indices}
    fixed_colors.update(v[0] for v in trajectories.values() if len(set(v)) == 1)
    assert 1 + len(fixed_colors) + len(dynamic) <= 256

    # Retain original slot numbers for one representative of each fixed color.
    # Preserve the chosen automap index, whose 4bpp low nibble must be nonzero.
    fixed_slots = {}
    for color in sorted(fixed_colors):
        candidates = [i for i in range(1, 256) if palette[i] == color]
        fixed_slots[color] = map_index if color == palette[map_index] else candidates[0]
    free = [i for i in range(1, 256) if i not in fixed_slots.values()]
    dynamic_slots = dict(zip(dynamic, free))
    static_map = bytes([0] + [fixed_slots.get(palette[i], 0) for i in range(1, 256)])
    terrain_map = [0]*256
    for i, trajectory in trajectories.items():
        terrain_map[i] = dynamic_slots[trajectory] if trajectory in dynamic_slots else fixed_slots[trajectory[0]]
    remapped = l3.translate(bytes(terrain_map))
    palettes = []
    for state in range(len(luts)):
        new = [0]*256
        for color, slot in fixed_slots.items():
            new[slot] = color
        for trajectory, slot in dynamic_slots.items():
            new[slot] = trajectory[state]
        palettes.append(new)
        (out / (state_names[state]+'.rgb555.bin')).write_bytes(struct.pack('<256H', *new))
    (out/'static_index_map.bin').write_bytes(static_map)
    (out/'l3_native_index_map.bin').write_bytes(bytes(terrain_map))
    (out/'world_l3.candidate.bin').write_bytes(remapped)
    canonical_atlases = []
    for name in ('world_l1.rle','world_l2.rle','world_l3.rle','world_l4.rle',
                 'dungeon_art_raw.rle','floor_variants.rle'):
        original = l3 if name == 'world_l3.rle' else assets[name]
        converted = remapped if name == 'world_l3.rle' else original.translate(static_map)
        before = len(original)//64
        after = len({converted[i:i+64] for i in range(0,len(converted),64)})
        canonical_atlases.append({'asset':name,'before_tiles':before,'after_tiles':after,
                                  'tile_bytes_saved_if_repacked':(before-after)*64})
    # Compact prospective runtime input: keep full 34-state palettes only for
    # review. One source-index slot list + cave/foul/clean RGB rows supports the
    # original prefix transition without a 34 x 512-byte ROM table.
    water_indices=sorted(set(l3)&set(range(1,32)))
    compact = bytes(terrain_map[i] for i in water_indices)
    for lut in (caves,foul,clean):
        compact += struct.pack('<'+str(len(water_indices))+'H',*(palette[lut[i]] for i in water_indices))
    (out/'water_compact_candidate.bin').write_bytes(compact)

    # Verify exact RGB555 AND opacity for all current L3 atlas bytes, all 34
    # states, every audited static pixel, and every floor palette reference.
    mismatches = 0
    for state, lut in enumerate(luts):
        new = palettes[state]
        for i in set(l3):
            assert bool(i) == bool(terrain_map[i])
            mismatches += new[terrain_map[i]] != palette[lut[i]]
        for i in static_indices:
            assert static_map[i] and static_map[i] not in dynamic_slots.values()
            mismatches += new[static_map[i]] != palette[i]
    assert mismatches == 0

    tile_reports, all_tiles, examples = [], [], []
    for region in range(1, 5):
        raw = l3 if region == 3 else assets[f'world_l{region}.rle']
        groups = collections.defaultdict(list)
        for i in range(len(raw)//64):
            block = raw[i*64:(i+1)*64]
            groups[canonical(block)].append((i, block))
            all_tiles.append((region, i, block))
        duplicate_groups = [g for g in groups.values() if len({b for _, b in g}) > 1]
        extra = sum(len({b for _, b in g})-1 for g in duplicate_groups)
        # Sparse dictionary model: each alternate tile needs a 16-bit canonical
        # tile reference, one byte of color-count, and a color-label map.
        metadata = sum(3+len(set(block)-{0}) for g in duplicate_groups for _, block in g[1:])
        tile_reports.append({'region': region, 'atlas_bytes': len(raw),
            'atlas_tiles': len(raw)//64, 'existing_exact_duplicates': len(raw)//64-len({raw[i:i+64] for i in range(0,len(raw),64)}),
            'recolor_groups': len(duplicate_groups), 'recolor_extra_tiles': extra,
            'gross_eliminable_tile_bytes': extra*64, 'sparse_mapping_bytes_estimate': metadata,
            'net_before_dispatch_code_and_lookup_bytes': extra*64-metadata,
            'direct_hardware_per_tile_palette_swap_available_in_current_8bpp_BG': False})
        useful = sorted((g for g in duplicate_groups if len(set(g[0][1])-{0}) >= 3
                         and sum(bool(v) for v in g[0][1]) >= 24),
                        key=lambda g: len(set(g[0][1])), reverse=True)
        if useful:
            examples.append((region, useful[0][0], useful[0][1]))

    # Compare whole prop frames, never confuse an open pose with a recolor.
    prop_groups = collections.defaultdict(list)
    for name, index, frame in props:
        prop_groups[canonical(frame)].append((name, index, frame))
    recolors, identical = [], []
    for group in prop_groups.values():
        if len(group) > 1:
            row = [{'asset': n, 'frame': i} for n, i, _ in group]
            (recolors if len({b for _, _, b in group}) > 1 else identical).append(row)
    cross = collections.defaultdict(list)
    for region, i, block in all_tiles:
        if any(block):
            cross[canonical(block)].append((region, i))
    cross_groups = [group for group in cross.values() if len({r for r, _ in group}) > 1]

    report = {'status': 'offline prototype only; shipping game and ROM unchanged',
        'source_sha256': sources,
        'HUD_shipping_compressed_assets_checked': hud_source_checks,
        'same_RGB_canonical_atlas_repack': canonical_atlases,
        'palette': {'format': 'GBA RGB555, 256 entries, zero is transparency/backdrop',
            'fixed_colors': len(fixed_colors), 'dynamic_L3_slots': len(dynamic),
            'occupied_including_zero': 1+len(fixed_colors)+len(dynamic),
            'spare_slots': 255-len(fixed_colors)-len(dynamic),
            'static_slots': sorted(fixed_slots.values()), 'dynamic_slots': sorted(dynamic_slots.values()),
            'map_line_old_and_new_index': map_index, 'preserves_32_step_purification': True,
            'instant_water_endpoint_pair_count': len({(palette[foul[i]],palette[clean[i]]) for i in range(1,32)}),
            'used_native_water_indices': sorted(set(l3)&set(range(1,32))),
            'states': state_names, 'base_RGB555_and_opacity_mismatches': mismatches,
            'compact_water_candidate_bytes': len(compact),
            'full_state_palettes_are_review_outputs_not_a_runtime_table': True,
            'L3_pixel_state_checks': len(l3)*len(luts),
            'static_asset_pixels': sum(map(len,assets.values())), 'floor_frames': floor_count,
            'all_256_original_colors_preserved': False,
            'scope': 'audited dungeon assets and generated color outputs only; town/loading/menu palettes excluded'},
        'terrain': tile_reports, 'cross_region_nonempty_pattern_groups': len(cross_groups),
        'props': prop_report, 'prop_whole_frame_recolor_groups': recolors,
        'prop_whole_frame_identical_groups': identical,
        'runtime_requirements': [
            'Offline-reindex all fixed BG assets and floor local-palette references using static_index_map.',
            'Use candidate L3 atlas directly; remove game_world_colors pixel remapping for L3.',
            'Upload dynamic RGB555 entries on scene/purification phase change and stop water-only terrain invalidation.',
            'Rebuild prop_highlight and remap hardcoded/nearest-color BG outputs; restrict static searches to fixed slots.',
            'Keep apply_gamma, base_palette and hardware brightness in sync; UI and automap must never select dynamic water slots.',
            'Retain software-light fallback semantics if enabled; its water colors need separate review.',
            'Build ROM and verify water completion/reload, caves, HUD, highlights, corpses/items, automap and performance before adoption.'],
        'not_measured': ['runtime FPS gain', 'final linked ROM/RAM delta', 'all room-macro palette equivalences',
                         'geometry-changing/approximate tile merges', 'whole-campaign visual regression']}
    (out/'audit.json').write_text(json.dumps(report,indent=2)+'\n')

    # Exact source-pixel review, no generated artwork, gamma or scene lighting.
    font_path='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
    font=lambda size: ImageFont.truetype(font_path,size)
    sheet=Image.new('RGB',(1120,800),(20,23,29));draw=ImageDraw.Draw(sheet)
    draw.text((24,18),'BACKGROUND PALETTE / ACTUAL ASSET REVIEW',font=font(25),fill='white')
    draw.text((24,56),'Same L3 terrain pixels; only palette changes. Native 32-step cleanup remains possible.',font=font(15),fill=(180,193,207))
    text=read('world_l3.inc').decode();tiles=array(text,'l3_tiles')
    macro=array(text,'l3_macros')
    map_macros=array(quest,'water_macros')
    relevant={macro[(v-1)*4+j] for v in map_macros if v for j in range(4)}
    pictures=[]
    for piece in sorted(relevant):
        img=Image.new('P',(32,80))
        for tile in range(40):
            idx=tiles[piece*40+tile]>>2
            img.paste(Image.frombytes('P',(8,8),l3[idx*64:(idx+1)*64]),((tile%4)*8,(tile//4)*8))
        score=sum(0<v<32 for v in img.tobytes())
        if score:
            pictures.append((score,piece,img))
    selected=sorted(pictures,reverse=True,key=lambda x:x[:2])[:3]
    for col,(label,state) in enumerate([('CAVES',0),('FOUL WATER',1),('CLEAN WATER',33)]):
        x=24+col*366;draw.text((x,96),label,font=font(20),fill=(230,237,246))
        for row,(_,piece,img) in enumerate(selected):
            rgb=Image.frombytes('RGB',img.size,bytes(c for i in img.tobytes() for c in word_rgb(palette[luts[state][i]])))
            crop=rgb.crop((0,48,32,80)).resize((112,112),Image.Resampling.NEAREST)
            sheet.paste(crop,(x+row*116,132))
            draw.text((x+row*116,250),f'piece {piece}',font=font(12),fill=(180,193,207))
    draw.text((24,294),'Exact 8 x 8 recolor candidates (magnified; per-tile remap would still need CPU work)',font=font(17),fill='white')
    for col,(region,(ia,a),(ib,b)) in enumerate(examples):
        x=24+col*274
        draw.text((x,334),f'L{region}  #{ia} / #{ib}',font=font(16),fill=(210,220,234))
        for n,block in enumerate((a,b)):
            data=block.translate(bytes(caves)) if region==3 else block
            im=Image.frombytes('RGB',(8,8),bytes(c for i in data for c in word_rgb(palette[i])))
            sheet.paste(im.resize((112,112),Image.Resampling.NEAREST),(x+n*120,365))
    draw.text((24,506),f'Palette prototype: {len(fixed_colors)} fixed + {len(dynamic)} water + 1 transparent; '
              f'{255-len(fixed_colors)-len(dynamic)} spare slots',font=font(19),fill='white')
    draw.text((24,539),'All audited base RGB555 values and transparency preserved. Not integrated into the ROM.',font=font(16),fill=(180,193,207))
    # Palette is displayed as numerical slots; water-owned entries have yellow borders.
    for i,c in enumerate(palettes[1]):
        x=24+(i%64)*16;y=586+(i//64)*34
        draw.rectangle((x,y,x+13,y+25),fill=word_rgb(c))
        if i in dynamic_slots.values():
            draw.rectangle((x,y,x+13,y+25),outline=(255,211,70),width=1)
    draw.text((24,741),'Yellow border = water-owned slot. Preview uses current base colors before gamma / hardware darkness.',font=font(15),fill=(180,193,207))
    sheet.save(out/'background-palette-review.png')
    # Include final preview input in report hashes too.
    report['source_sha256']=sources
    (out/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'palette':report['palette'],'terrain':tile_reports,
                     'props_recolor':recolors,'props_identical':identical},indent=2))


if __name__ == '__main__':
    main()
