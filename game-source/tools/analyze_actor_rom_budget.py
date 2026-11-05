"""Measure a staged palette-sharing overlay against the immutable Stage20 ELF.

No generated files are changed. Includes linker byte aliases, four-byte stream
alignment, all live SPR1 dependencies, and source pixel-exact raw conversions.
"""
from pathlib import Path
import argparse
import hashlib
import itertools
import json
import struct
import tempfile

from pack_hot_monsters import Decoder, raw_tiles

ROOT = Path(__file__).resolve().parents[1]
SPR1, SPR2 = 0x31525053, 0x32525053


def u32(data, pos=0):
    return struct.unpack_from('<I', data, pos)[0]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def aligned(size):
    return (size + 3) & ~3


def elf_assets(path):
    data = path.read_bytes()
    assert data[:7] == b'\x7fELF\x01\x01\x01'
    shoff = u32(data, 32)
    shsize, shcount = struct.unpack_from('<HH', data, 46)
    sections = [struct.unpack_from('<10I', data, shoff + i * shsize) for i in range(shcount)]
    result = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        for pos in range(section[4], section[4] + section[5], section[9]):
            name, value, _, _, _, index = struct.unpack_from('<IIIBBH', data, pos)
            name = names[name:names.find(b'\0', name)].decode()
            if name.startswith('asset_') and index:
                source = sections[index]
                result[name[6:]] = source[4] + value - source[3]
    return data, result


def analyze(staged, output, quality_exclusions=()):
    staged = staged.resolve()
    baseline = ROOT / 'build/stage20/baseline'
    snapshot = ROOT / 'build/stage20/analysis-snapshot'
    palettes = ROOT / 'source_assets/actor_sources/stage20/palettes'
    original_elf, offsets = elf_assets(baseline / 'diablo-gba-assets.elf')
    original = {}
    for name, offset in offsets.items():
        paths = (snapshot / (name + '.lz'), baseline / 'generated' / (name + '.lz'),
                 palettes / (name + '.lz'), ROOT / 'generated' / (name + '.lz'),
                 ROOT / 'generated' / (name + '.rle'))
        path = next(path for path in paths if path.exists())
        data = path.read_bytes()
        assert data == original_elf[offset:offset + len(data)], name + ': differs from baseline ELF'
        original[name] = data
    pool = original.pop('hero_frame_pool')
    report_path = staged / 'palette-sharing-report.json'
    report_bytes = report_path.read_bytes()
    sharing = json.loads(report_bytes)
    overlay = {}
    for name, record in sharing['outputs'].items():
        data = (staged / (name + '.lz')).read_bytes()
        assert sha(data) == record['sha256'] and len(data) == record['bytes'], name
        assert name in original, name + ': output is not a referenced baseline asset'
        overlay[name] = data

    children, lengths = {}, {}
    def closure(clips):
        seen, pending = set(), []
        for data in clips.values():
            if len(data) >= 8 and u32(data) == SPR1:
                pending.extend(struct.unpack_from('<' + 'I' * u32(data, 4), data, 8))
        while pending:
            offset = pending.pop()
            if offset in seen:
                continue
            seen.add(offset)
            if offset not in lengths:
                kind, depth, size = struct.unpack_from('<BBH', pool, offset)
                assert kind <= 4 and depth <= 3 and offset + 4 + size <= len(pool)
                lengths[offset] = size + 4
                children[offset] = None if kind in (0, 4) else u32(pool, offset + 4)
            if children[offset] is not None:
                pending.append(children[offset])
        return seen

    original_stored = sum(aligned(len(data)) for data in set(original.values()))
    original_closure = closure(original)
    pool_live_original = 8 + sum(lengths[node] for node in original_closure)

    def budget(label, replacements):
        current = dict(original)
        current.update(replacements)
        live = closure(current)
        new_pool = 8 + sum(lengths[node] for node in live)
        stored = sum(aligned(len(data)) for data in set(current.values()))
        delta = stored - original_stored + aligned(new_pool) - aligned(len(pool))
        return {'label': label, 'replaced_asset_count': len(replacements),
                'unique_stream_bytes_excluding_pool': stored,
                'unique_stream_delta_excluding_pool': stored - original_stored,
                'pool_live_records': len(live), 'compacted_pool_bytes': new_pool,
                'pool_reclaimed_bytes': len(pool) - new_pool,
                'net_rom_delta_bytes': delta, 'estimated_rom_free_bytes': 217780 - delta}

    candidates = json.loads((ROOT / 'build/stage20/candidates.json').read_text())
    raw = {}
    checked = 0
    with tempfile.TemporaryDirectory(prefix='actor-budget-') as temporary:
        decoder = Decoder(temporary, pool)
        for row in candidates['clips']:
            name, count = row['asset'], row['frames']
            frames = decoder.frames(original[name], count)
            assert sha(b''.join(frames)) == row['decoded_sha256'], name
            encoded = raw_tiles(frames)
            assert decoder.frames(encoded, count) == frames, name + ': pixels differ'
            assert len(encoded) == row['raw_tile_bytes'], name + ': raw encoder size changed'
            raw[name] = encoded
            checked += count

    scenarios = [budget('baseline pool-only compaction', {}),
                 budget('staged shared species only', overlay),
                 budget('all species SPR2 without sharing', raw)]
    all_shared = dict(raw)
    all_shared.update(overlay)
    scenarios.append(budget('all species SPR2 plus staged sharing', all_shared))
    quality_options = []
    for count in range(1, len(quality_exclusions) + 1):
        for excluded in itertools.combinations(quality_exclusions, count):
            selected = dict(all_shared)
            for monster in excluded:
                names = {row['asset'] for row in candidates['clips'] if row['monster'] == monster}
                assert names and monster + '_palette' in original, monster
                selected.update({name: raw[name] for name in names})
                selected[monster + '_palette'] = original[monster + '_palette']
            row = budget('preserve original colors: ' + ', '.join(excluded), selected)
            row['excluded_shared_monsters'] = list(excluded)
            row['additional_rom_bytes'] = row['net_rom_delta_bytes'] - scenarios[-1]['net_rom_delta_bytes']
            quality_options.append(row)
    raw_monster_bytes = sum(aligned(len(data)) for data in set(raw.values()))
    shared_monster_streams = {all_shared[name] for name in raw}
    shared_monster_bytes = sum(aligned(len(data)) for data in shared_monster_streams)
    remaining = []
    selected_keys = {row['key'] for row in sharing['monsters'] if row['accepted']}
    for group in candidates['monsters']:
        if group['monster'] in selected_keys:
            continue
        names = {row['asset'] for row in candidates['clips'] if row['monster'] == group['monster']}
        keep_source = {name: data for name, data in all_shared.items() if name not in names}
        row = budget('all shared except ' + group['monster'], keep_source)
        row['monster'] = group['monster']
        row['additional_bytes_to_make_raw'] = scenarios[-1]['net_rom_delta_bytes'] - row['net_rom_delta_bytes']
        row['mean_nibble_reads'] = group['mean_nibble_reads']
        row['mean_lz_byte_writes'] = group['mean_lz_byte_writes']
        remaining.append(row)
    result = {'scope': 'read-only exact stream/alignment/alias and SPR1 dependency accounting; source/generated unchanged',
              'note': 'ROM estimate uses measured 217780 baseline free bytes. Excludes future code changes. Injective pool offset remapping preserves SPR1 byte alias equivalence.',
              'baseline_elf': str((baseline / 'diablo-gba-assets.elf').relative_to(ROOT)),
              'baseline_pool_bytes': len(pool), 'baseline_referenced_asset_count': len(original) + 1,
              'baseline_unique_stream_bytes_excluding_pool': original_stored,
              'baseline_pool_live_bytes': pool_live_original,
              'sharing_report': str(report_path.relative_to(ROOT)), 'sharing_report_sha256': sha(report_bytes),
              'shared_species': sorted(selected_keys), 'source_frames_pixel_verified': checked,
              'monster_stream_comparison': {
                  'raw_all_species_logical_bytes': sum(map(len, raw.values())),
                  'raw_all_species_unique_aligned_bytes': raw_monster_bytes,
                  'raw_with_staged_sharing_unique_aligned_bytes': shared_monster_bytes,
                  'unique_monster_stream_bytes_saved_by_sharing': raw_monster_bytes - shared_monster_bytes,
                  'note': 'Monster clips only; palettes and retained hero assets are included in scenario totals.'},
              'scenarios': scenarios, 'preserve_original_color_options': quality_options,
              'nonshared_species_keep_original_options': remaining}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + '\n')
    for row in scenarios:
        print(row['label'], 'ROM delta', row['net_rom_delta_bytes'], 'free', row['estimated_rom_free_bytes'])
    for row in quality_options:
        print(row['label'], 'additional', row['additional_rom_bytes'], 'free', row['estimated_rom_free_bytes'])
    print('Saved', output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--staged', type=Path, default=ROOT / 'build/stage20/palette/staged')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/stage20/rom-budget.json')
    parser.add_argument('--quality-exclusions', default='', help='comma-separated shared species to measure with original pixels and palettes')
    args = parser.parse_args()
    analyze(args.staged, args.output, tuple(filter(None, args.quality_exclusions.split(','))))
