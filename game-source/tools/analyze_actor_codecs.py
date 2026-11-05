"""Read-only monster codec/ROM analysis; operation counts are not GBA cycles.

All input clips and the pool are an immutable Stage20 snapshot. Generated
assets are never modified. Every raw-tile candidate is decoded by the shipped
C decoder and compared byte-for-byte with its source frames.
"""
from pathlib import Path
import argparse
import collections
import hashlib
import json
import struct
import tempfile

from lz77 import compress_actor, indexed_chunks
from pack_hot_monsters import Decoder, raw_tiles

ROOT = Path(__file__).resolve().parents[1]
SPR1, SPR2 = 0x31525053, 0x32525053
SPECIAL = {'fallen_spear', 'fallen_sword', 'skeleton_king', 'carver_sword',
           'devil_kin_spear', 'advocate', 'lazarus', 'diablo', 'zhar'}


def word(data, offset=0):
    return struct.unpack_from('<I', data, offset)[0]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def lz_work(data):
    stats = collections.Counter()
    if data[0] == 17:
        stats.update(lz_work(data[9:]))
        stats['sparse_expand_word_writes'] += 512
        stats['sparse_tile_tests'] += 64
        stats['encoded_bytes'] += 9
        return stats
    assert data[0] == 16
    size, position, cursor = word(data) >> 8, 0, 4
    while position < size:
        flags = data[cursor]
        cursor += 1
        for bit in range(8):
            if position == size:
                break
            stats['lz_tokens'] += 1
            if flags & (128 >> bit):
                a, b = data[cursor:cursor + 2]
                cursor += 2
                run, distance = (a >> 4) + 3, ((a & 15) << 8) + b + 1
                assert distance <= position and position + run <= size
                while run and position & 3:
                    position += 1
                    run -= 1
                    stats['lz_byte_writes'] += 1
                if distance == 1 or (distance >= 4 and not distance & 3):
                    words = run // 4
                    stats['lz_word_writes'] += words
                    position += words * 4
                    run -= words * 4
                stats['lz_byte_writes'] += run
                position += run
            else:
                cursor += 1
                position += 1
                stats['lz_byte_writes'] += 1
    stats['encoded_bytes'] += cursor
    return stats


def nibble_work(data):
    cursor, position = 0, 0
    stats = collections.Counter()

    def read():
        nonlocal cursor
        assert cursor < len(data) * 2
        result = (data[cursor // 2] >> ((cursor & 1) * 4)) & 15
        cursor += 1
        return result

    while position < 4096:
        value = read()
        if value:
            stats['nibble_xor_writes'] += 1
            position += 1
        else:
            stats['nibble_skip_tokens'] += 1
            run = read()
            if run in (0, 15):
                a, b = read(), read()
                run = 271 + a + (b << 4) + (read() << 8) if not run else 15 + a + (b << 4)
            assert run <= 4096 - position
            position += run
    assert cursor == len(data) * 2 or (cursor + 1 == len(data) * 2 and read() == 0)
    stats['nibble_reads'] = cursor
    return stats


def analyze(snapshot, pool_path, output, independent):
    manifest = json.loads((snapshot / 'snapshot.json').read_text())
    pool = pool_path.read_bytes()
    assert sha(pool) == manifest['pool_sha256']
    clips = {name[:-3]: (snapshot / name).read_bytes() for name in manifest['files']}
    for name, data in clips.items():
        assert sha(data) == manifest['files'][name + '.lz']
    records, chains = {}, {}

    def record(offset):
        if offset not in records:
            assert 8 <= offset <= len(pool) - 4
            kind, depth, length = struct.unpack_from('<BBH', pool, offset)
            assert kind <= 4 and depth <= 3 and offset + length + 4 <= len(pool)
            records[offset] = (kind, depth, length)
        return records[offset]

    def chain(offset):
        if offset not in chains:
            kind, depth, length = record(offset)
            result = [offset]
            if kind not in (0, 4):
                assert length >= 4
                result += chain(word(pool, offset + 4))
            assert len(result) == depth + 1
            chains[offset] = result
        return chains[offset]

    users = collections.defaultdict(set)
    for name, data in clips.items():
        if word(data) == SPR1:
            for index in range(word(data, 4)):
                for offset in chain(word(data, 8 + index * 4)):
                    users[offset].add(name)

    record_work = {}

    def pooled_work(offset):
        result = collections.Counter(chain_depth=len(chain(offset)) - 1,
                                     records=len(chain(offset)))
        for node in chain(offset):
            if node not in record_work:
                kind, depth, length = record(node)
                data = pool[node + 4:node + 4 + length]
                stats = collections.Counter(pool_bytes=length + 4)
                if kind == 0:
                    stats.update(lz_work(data))
                elif kind == 4:
                    stats['zero_word_writes'] = 512
                    stats.update(nibble_work(data))
                elif kind == 3:
                    stats.update(nibble_work(data[4:]))
                else:
                    raise AssertionError('Unexpected legacy byte-patch record; extend analysis before use')
                record_work[node] = stats
            result.update(record_work[node])
        return result

    monsters = json.loads((ROOT / 'source_assets/monster_catalog/monsters.json').read_text())['monsters']
    hashes = json.loads((ROOT / 'build/stage20/baseline/generated/monster-pool-report.json').read_text())['decoded_sha256']
    for row in json.loads((ROOT / 'generated/compact-actors-report.json').read_text()):
        hashes.setdefault(row['asset'], row['decoded_sha256'])
    rows = []
    with tempfile.TemporaryDirectory(prefix='actor-analysis-') as temporary:
        decoder = Decoder(temporary, pool)
        for monster in monsters:
            for state in 'nwahs':
                if state == 's' and monster['key'] not in SPECIAL:
                    continue
                name = monster['key'] + ('_hit' if state == 'h' and monster['kind'] < 6 else '_' + state)
                if name not in clips:
                    continue
                data = clips[name]
                magic = word(data)
                count = word(data, 4) if magic in (SPR1, SPR2) else magic
                frames = decoder.frames(data, count)
                digest = sha(b''.join(frames))
                if name in hashes:
                    assert digest == hashes[name], name + ': source pixels differ from previous manifest'
                encoded = raw_tiles(frames)
                assert decoder.frames(encoded, count) == frames, name + ': raw-tile pixel mismatch'
                work = []
                for index, frame in enumerate(frames):
                    if magic == SPR1:
                        stats = pooled_work(word(data, 8 + 4 * index))
                    elif magic == SPR2:
                        stats = collections.Counter(raw_word_writes=512)
                    else:
                        stats = lz_work(data[word(data, 4 + 4 * index):])
                    stats['nonempty_tiles'] = sum(any(frame[t * 32:(t + 1) * 32]) for t in range(64))
                    work.append(stats)
                keys = set().union(*(item.keys() for item in work))
                means = {key: round(sum(item[key] for item in work) / count, 3) for key in sorted(keys)}
                row = {'asset': name, 'kind': monster['kind'], 'monster': monster['key'], 'state': state,
                       'catalog_floors': [monster['min_floor'], monster['max_floor']],
                       'frames': count, 'source_codec': 'SPR1' if magic == SPR1 else 'SPR2' if magic == SPR2 else 'independent-LZ',
                       'source_bytes': len(data), 'raw_tile_bytes': len(encoded), 'raw_delta_bytes': len(encoded) - len(data),
                       'decoded_sha256': digest, 'roundtrip': 'pixel-exact',
                       'mean_work': means, 'max_chain_depth': max(item['chain_depth'] for item in work),
                       'raw_word_writes_per_frame': 512,
                       'exclusive_pool_bytes_if_converted': sum(record(node)[2] + 4 for node, names in users.items() if names == {name})}
                if monster['key'] in independent or name in independent:
                    compressed = indexed_chunks([compress_actor(frame) for frame in frames])
                    assert decoder.frames(compressed, count) == frames
                    row['independent_lz_bytes'] = len(compressed)
                    row['independent_lz_delta_bytes'] = len(compressed) - len(data)
                rows.append(row)
            print(monster['key'], 'analyzed', flush=True)

    groups = []
    for monster in monsters:
        selected = [row for row in rows if row['monster'] == monster['key']]
        candidates = [row for row in selected if row['source_codec'] != 'SPR2']
        names = {row['asset'] for row in candidates}
        reclaimed = sum(record(node)[2] + 4 for node, owners in users.items() if owners <= names)
        delta = sum(row['raw_delta_bytes'] for row in candidates)
        groups.append({'monster': monster['key'], 'kind': monster['kind'], 'catalog_floors': [monster['min_floor'], monster['max_floor']],
                       'clips': [row['asset'] for row in candidates], 'frames': sum(row['frames'] for row in candidates),
                       'raw_delta_bytes_before_pool_compaction': delta,
                       'pool_bytes_reclaimable_as_group': reclaimed,
                       'net_delta_after_pool_compaction': delta - reclaimed,
                       'mean_nibble_reads': round(sum(row['mean_work'].get('nibble_reads', 0) * row['frames'] for row in candidates) / max(1, sum(row['frames'] for row in candidates)), 2),
                       'mean_lz_byte_writes': round(sum(row['mean_work'].get('lz_byte_writes', 0) * row['frames'] for row in candidates) / max(1, sum(row['frames'] for row in candidates)), 2)})
    report = {'scope': 'immutable Stage20 baseline; no generated/source asset modifications',
              'cost_warning': 'Static decoder operation counts and ROM bytes only. Not host timing, GBA cycles, or predicted FPS. All frames weighted equally within each clip.',
              'pool_bytes': len(pool), 'pool_sha256': sha(pool), 'snapshot_clip_count': len(clips),
              'clips_analyzed': len(rows), 'frames_verified': sum(row['frames'] for row in rows),
              'baseline_unreferenced_pool_bytes': len(pool) - 8 - sum(record(node)[2] + 4 for node in users),
              'current_rom_free_bytes': 217780, 'clips': rows, 'monsters': groups}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + '\n')
    print('Saved', output, 'frames', report['frames_verified'], flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, default=ROOT / 'build/stage20/analysis-snapshot')
    parser.add_argument('--pool', type=Path, default=ROOT / 'build/stage20/baseline/generated/hero_frame_pool.lz')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/stage20/candidates.json')
    parser.add_argument('--independent', default='', help='comma-separated monster keys or clip names for optional independent LZ size comparison')
    args = parser.parse_args()
    analyze(args.snapshot, args.pool, args.output, set(args.independent.split(',')) - {''})
