"""Stage reproducible monster SPR2/palette sharing and compact the runtime pool.

The immutable actor source archive is kept outside the ROM. Existing hero and
other asset bytes are retained. This tool writes only --output, never generated/.
It can be rerun using an already optimized input directory: archived monster
records are relocated into the temporary pool before unreachable records vanish.
"""
from pathlib import Path
import argparse
import base64
import hashlib
import json
import shutil
import struct
import tempfile

from compact_actor_pool import compact, parse_pool, clip_offsets, compare_pixels
from pack_hot_monsters import Decoder, raw_tiles
from lz77 import compress, decompress

ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def append_source_pool(runtime, source, source_clips):
    """Relocate the immutable source's dependencies; do not guess offset ranges."""
    parse_pool(runtime)
    records = parse_pool(source)
    delta = len(runtime) - 8
    joined = bytearray(runtime)
    for offset, record in records.items():
        block = bytearray(source[offset:offset + 4 + record['size']])
        if record['parent'] is not None:
            struct.pack_into('<I', block, 4, record['parent'] + delta)
        joined.extend(block)
    struct.pack_into('<I', joined, 0, len(joined))
    rebased = {}
    for name, data in source_clips.items():
        if data[:4] == b'SPR1':
            offsets = clip_offsets(data)
            data = data[:8] + struct.pack('<' + 'I' * len(offsets),
                                         *(offset + delta for offset in offsets))
        rebased[name] = data
    return bytes(joined), rebased


def stage(source_dir, archive_path, palette_dir, output, keep_pooled):
    source_dir, output = source_dir.resolve(), output.resolve()
    archive_path, palette_dir = archive_path.resolve(), palette_dir.resolve()
    if output == source_dir or source_dir in output.parents:
        raise ValueError('Output must be outside the input asset directory')
    if output == (ROOT / 'generated').resolve():
        raise ValueError('Stage outside generated/; install a verified result separately')
    archive = json.loads(archive_path.read_text())
    source_pool = (archive_path.parent / archive['pool_file']).read_bytes()
    assert sha(source_pool) == archive['pool_sha256']
    originals = {}
    for row in archive['clips']:
        data = base64.b64decode(row['source_clip_base64'], validate=True)
        assert sha(data) == row['source_sha256'] and len(data) == row['source_bytes']
        originals[row['asset']] = data
    palette_report = json.loads((palette_dir / 'palette-sharing-report.json').read_text())
    assert palette_report['source_pool_sha256'] == sha(source_pool)
    assert palette_report['source_assets'].keys() == originals.keys()
    for name, data in originals.items():
        assert palette_report['source_assets'][name]['sha256'] == sha(data), name + ': stale palette source'
    for name, digest in palette_report['source_palette_sha256'].items():
        data = (archive_path.parent / 'palettes' / (name + '.lz')).read_bytes()
        assert sha(data) == digest, name + ': source palette hash mismatch'
    catalog = json.loads((ROOT / 'source_assets/monster_catalog/monsters.json').read_text())['monsters']
    kinds = {r['key'] for r in catalog}
    assert keep_pooled <= kinds, 'Unknown keep-pooled monster'
    by_asset = {}
    for r in catalog:
        for state in ('n', 'w', 'a', 'h', 'hit', 's'):
            if r['key'] + '_' + state in originals:
                by_asset[r['key'] + '_' + state] = r['key']
    assert by_asset.keys() == originals.keys()
    assets = {p.name: p.read_bytes() for p in source_dir.iterdir()
              if p.suffix in ('.lz', '.rle')}
    input_pool = assets['hero_frame_pool.lz']
    joined_pool, rebased = append_source_pool(input_pool, source_pool, originals)
    for name, data in rebased.items():
        assets[name + '.lz'] = data
    # Restore only monster colours; preserve unrelated banks in obj_palette.
    for key in kinds - {'zombie'}:
        path = archive_path.parent / 'palettes' / (key + '_palette.lz')
        assert key + '_palette' in palette_report['source_palette_sha256']
        assets[path.name] = path.read_bytes()
    assert 'obj_palette' in palette_report['source_palette_sha256']
    original_obj = decompress((archive_path.parent / 'palettes/obj_palette.lz').read_bytes())
    current_obj = bytearray(decompress(assets['obj_palette.lz']))
    current_obj[32:64] = original_obj[32:64]
    current_obj[128:160] = original_obj[128:160]
    assets['obj_palette.lz'] = compress(bytes(current_obj))

    palette_rows = [r for r in palette_report['monsters'] if r['accepted']]
    shared_assets = {name for row in palette_rows for name in (*row['shared_clips'], *row['own_clips'])}
    assert shared_assets <= originals.keys()
    for name, item in palette_report['outputs'].items():
        data = (palette_dir / (name + '.lz')).read_bytes()
        assert sha(data) == item['sha256'] and len(data) == item['bytes']
        if name == 'obj_palette':
            # A future hero palette edit must survive monster regeneration.
            obj = bytearray(decompress(assets['obj_palette.lz']))
            zombie = decompress(data)
            obj[32:64] = zombie[32:64]
            obj[128:160] = zombie[128:160]
            data = compress(bytes(obj))
        assets[name + '.lz'] = data
    assert not {by_asset[n] for n in shared_assets} & keep_pooled, 'Shared images already use SPR2'

    converted = []
    with tempfile.TemporaryDirectory(prefix='monster-stage-') as temporary:
        work = Path(temporary)
        decoder = Decoder(work, source_pool)
        for row in archive['clips']:
            name = row['asset']
            if name in shared_assets or by_asset[name] in keep_pooled:
                continue
            frames = decoder.frames(originals[name], row['frames'])
            assert sha(b''.join(frames)) == row['decoded_sha256'], name
            data = raw_tiles(frames)
            assert decoder.frames(data, row['frames']) == frames, name
            assets[name + '.lz'] = data
            converted.append(name)
        roots = {Path(name).stem: data for name, data in assets.items() if data[:4] == b'SPR1'}
        final_pool, final_clips, stats = compact(joined_pool, roots)
        compare_pixels(joined_pool, roots, final_pool, final_clips, work / 'compaction')
        again_pool, again_clips, again_stats = compact(final_pool, final_clips)
        assert again_pool == final_pool and again_clips == final_clips and not again_stats['reclaimed_bytes']
    assets['hero_frame_pool.lz'] = final_pool
    for name, data in final_clips.items():
        assets[name + '.lz'] = data
    # Final display styling follows quantization/sharing, before output hashes.
    from legacy_monster_palette_style import style_assets
    style_assets(assets)
    output.mkdir(parents=True, exist_ok=True)
    for name, data in assets.items():
        (output / name).write_bytes(data)
    report = {'format_version': 1, 'source_archive': str(archive_path.relative_to(ROOT)),
              'source_pool_sha256': sha(source_pool), 'runtime_pool_sha256': sha(final_pool),
              'input_runtime_pool_bytes': len(input_pool), 'runtime_pool_bytes': len(final_pool),
              'runtime_pool_reduction_bytes': len(input_pool) - len(final_pool),
              'extra_runtime_pixel_passes': 0, 'extra_runtime_palette_banks': 0,
              'shared_monsters': [r['key'] for r in palette_rows],
              'lossless_raw_clips': converted, 'keep_pooled_monsters': sorted(keep_pooled),
              'monster_clips': len(originals), 'source_frames': sum(r['frames'] for r in archive['clips']),
              'compaction': stats, 'compaction_pixel_verification': 'PASS all remaining SPR1 frames, ASAN/UBSAN',
              'palette_report': 'palette-sharing-report.json',
              'asset_hashes': {name: sha(data) for name, data in sorted(assets.items())}}
    (output / 'monster-optimization-report.json').write_text(json.dumps(report, indent=2) + '\n')
    shutil.copy2(palette_dir / 'palette-sharing-report.json', output / 'palette-sharing-report.json')
    print(json.dumps({k: report[k] for k in ('runtime_pool_bytes', 'runtime_pool_reduction_bytes',
        'monster_clips', 'source_frames', 'shared_monsters', 'keep_pooled_monsters')}, indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=ROOT / 'generated')
    parser.add_argument('--source-archive', type=Path,
                        default=ROOT / 'source_assets/actor_sources/stage20/manifest.json')
    parser.add_argument('--palette-stage', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--keep-pooled', default='', help='comma-separated nonshared monster keys')
    args = parser.parse_args()
    stage(args.source_dir, args.source_archive, args.palette_stage, args.output,
          {x for x in args.keep_pooled.split(',') if x})


if __name__ == '__main__':
    main()
