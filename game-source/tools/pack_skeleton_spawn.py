"""Import the retail Skeleton King's summoned skeleton rising animations.

Usage: python tools/pack_skeleton_spawn.py DIABDAT.MPQ
       python tools/pack_skeleton_spawn.py --check

The existing Stage20 canonical image indices and hardware palettes are retained.
Only three new independent clips are emitted; ordinary assets and the shared
actor pool are never rewritten. The shipped C decoder verifies every frame.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import tempfile

import mpyq
from PIL import Image

from extract_assets import read_mpq, cl2_frames
from pack_gba_assets import tile_bytes
from pack_hot_monsters import Decoder
from lz77 import decompress, indexed


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'generated'
REPORT = OUT / 'skeleton_spawn-report.json'
GROUPS = {
    'axe': ('burning_dead', [10]),
    'sword': ('corpse_captain', [13]),
    'bow': ('corpse_bow', [34, 35, 36]),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def preserved_assets():
    return {p.name: sha(p.read_bytes()) for p in OUT.glob('*.lz')
            if not p.name.startswith('skeleton_spawn_')}


def check():
    report = json.loads(REPORT.read_text())
    with tempfile.TemporaryDirectory(prefix='skeleton-spawn-check-') as temp:
        decoder = Decoder(temp, (OUT / 'hero_frame_pool.lz').read_bytes())
        for row in report['clips']:
            source = ROOT / row['source_file']
            assert sha(source.read_bytes()) == row['source_sha256']
            blob = (OUT / (row['asset'] + '.lz')).read_bytes()
            assert len(blob) == row['bytes'] and sha(blob) == row['sha256']
            decoded = decoder.frames(blob, row['directions'] * row['frames_per_direction'])
            assert sha(b''.join(decoded)) == row['decoded_sha256']
            for palette, digest in row['palette_sha256'].items():
                assert sha((OUT / palette).read_bytes()) == digest
    print('PASS', report['checked_frames'], 'skeleton rising frames;',
          report['additional_bytes'], 'additional bytes')


def pack(mpq):
    before = preserved_assets()
    archive = mpyq.MPQArchive(str(mpq), listfile=False)
    native_palette = read_mpq(archive, 'Levels\\L1Data\\L1_1.PAL')
    rgb555 = [tuple(c >> 3 for c in native_palette[i:i + 3])
              for i in range(0, 768, 3)]
    records = json.loads((ROOT / 'source_assets/monster_catalog/monsters.json').read_text())['monsters']
    by_key = {r['key']: r for r in records}
    by_kind = {r['kind']: r for r in records}
    sharing = json.loads((OUT / 'palette-sharing-report.json').read_text())
    shared = {r['key']: r for r in sharing['monsters']}
    header = ['#pragma once', '#include <stdint.h>',
              '#define SKELETON_SPAWN_FRAMES 16',
              '#define SKELETON_SPAWN_DIRECTIONS 8']
    report = []
    with tempfile.TemporaryDirectory(prefix='skeleton-spawn-pack-') as temp:
        decoder = Decoder(temp, (OUT / 'hero_frame_pool.lz').read_bytes())
        for name, (canonical, kinds) in GROUPS.items():
            monster = by_key[canonical]
            for kind in kinds:
                key = by_kind[kind]['key']
                assert shared[key]['accepted'] and shared[key]['canonical'] == canonical
                assert by_kind[kind]['graphic'] == monster['graphic']
            native_path = monster['graphic'].replace('%c', 's')
            source = read_mpq(archive, native_path)
            source_file = Path('source_assets/monster_catalog') / canonical / 's.cl2'
            (ROOT / source_file).parent.mkdir(parents=True, exist_ok=True)
            (ROOT / source_file).write_bytes(source)
            translation = (read_mpq(archive, monster['translation'])
                           if monster['translation'] else bytes(range(256)))
            palette = struct.unpack('<32H', decompress((OUT / (canonical + '_palette.lz')).read_bytes()))
            canonical_rgb = [tuple((color >> shift) & 31 for shift in (0, 5, 10))
                             for color in palette[:16]]
            lut = [0] + [min(range(1, 16), key=lambda index: sum(
                (canonical_rgb[index][channel] - rgb555[translation[pixel]][channel]) ** 2
                for channel in range(3))) for pixel in range(1, 256)]
            directions = cl2_frames(source, monster['width'])
            assert len(directions) == 8 and all(len(d) == 16 for d in directions)
            frames = []
            for direction in directions:
                for native in direction:
                    side = max(128, monster['width'], native.height)
                    canvas = Image.new('P', (side, side))
                    canvas.paste(native, ((side - native.width) // 2, side - native.height))
                    scaled = canvas.resize((64, 64), Image.Resampling.NEAREST)
                    mapped = scaled.point(lut)
                    assert all(bool(a) == bool(b) for a, b in
                               zip(scaled.tobytes(), mapped.tobytes()))
                    frames.append(tile_bytes(mapped, 4))
            blob = indexed(b''.join(frames), 2048)
            assert decoder.frames(blob, len(frames)) == frames
            asset = 'skeleton_spawn_' + name
            (OUT / (asset + '.lz')).write_bytes(blob)
            header.append('extern const uint8_t asset_' + asset + '[];')
            palette_names = sorted({canonical + '_palette.lz'} | {
                by_kind[kind]['key'] + '_palette.lz' for kind in kinds})
            report.append({
                'asset': asset, 'canonical': canonical, 'kinds': kinds,
                'mpq_path': native_path, 'source_file': source_file.as_posix(),
                'source_bytes': len(source), 'source_sha256': sha(source),
                'translation_sha256': sha(translation),
                'native_palette_sha256': sha(native_palette),
                'directions': 8, 'frames_per_direction': 16,
                'frame_order': 'native forward, rising from ground',
                'native_rate': monster['rates'][5],
                'codec': 'independent indexed sparse LZ',
                'bytes': len(blob), 'sha256': sha(blob),
                'decoded_sha256': sha(b''.join(frames)),
                'palette_sha256': {name: sha((OUT / name).read_bytes()) for name in palette_names},
                'alpha_preserved': True,
            })
    assert preserved_assets() == before, 'ordinary assets or palettes changed'
    additional = sum(row['bytes'] for row in report)
    assert additional < 120 * 1024, 'skeleton animation payload exceeded budget'
    (OUT / 'skeleton_spawn.h').write_text('\n'.join(header) + '\n')
    result = {
        'source': 'retail DIABDAT.MPQ',
        'source_revision': '9f01757243412bb154d02d9af65c804159e6e0ea',
        'native_entry': 'M_SpawnSkel -> M_StartSpStand -> M_DoSpStand',
        'clips': report, 'checked_frames': sum(
            row['directions'] * row['frames_per_direction'] for row in report),
        'additional_bytes': additional,
        'additional_aligned_bytes': sum((row['bytes'] + 3) & ~3 for row in report),
        'existing_assets_changed': False,
        'special_audio_added': False,
        'audio_note': 'Summoned skeletons and King have mAFNum2=0; no special sound frame.',
    }
    REPORT.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mpq', type=Path, nargs='?')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if args.check:
        check()
    else:
        if not args.mpq:
            parser.error('DIABDAT.MPQ is required unless --check is used')
        pack(args.mpq)


if __name__ == '__main__':
    main()
