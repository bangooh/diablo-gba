"""Analyze/stage a lossless compaction of the SPR1 actor pool; never edit generated/.

Every remaining SPR1 clip in --source-dir is a root, including clips omitted by
the current linker. Parents of difference records are retained transitively.
--stage writes a separate candidate plus an optional immutable source archive.
Run without --stage for read-only statistics. Archive manifests hold remapped
SPR1 index tables and pool.lz; original source hashes remain as provenance.
"""
from pathlib import Path
import argparse
import base64
import hashlib
import json
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
POOL_NAME = 'hero_frame_pool.lz'
SPR1 = 0x31525053
POOL_MAGIC = 0x31465053


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(value, message):
    if not value:
        raise ValueError(message)


def parse_pool(data):
    require(len(data) >= 8, 'truncated pool header')
    require(struct.unpack_from('<II', data) == (len(data), POOL_MAGIC), 'pool header/size mismatch')
    records, offset = {}, 8
    while offset < len(data):
        require(offset + 4 <= len(data), 'truncated record header')
        kind, depth, size = struct.unpack_from('<BBH', data, offset)
        require(kind <= 4 and depth <= 3, 'unknown record codec or excessive depth')
        require(offset + 4 + size <= len(data), 'truncated record payload')
        parent = None
        if kind in (1, 2, 3):
            require(size >= 4 and depth > 0, 'invalid patch record')
            parent = struct.unpack_from('<I', data, offset + 4)[0]
            require(parent in records and parent < offset, 'parent is not an earlier record start')
            require(records[parent]['depth'] + 1 == depth, 'parent depth mismatch')
        else:
            require(depth == 0, 'key record has patch depth')
        records[offset] = {'kind': kind, 'depth': depth, 'size': size, 'parent': parent}
        offset += size + 4
    return records


def clip_offsets(data):
    require(len(data) >= 8, 'truncated SPR1 clip')
    magic, count = struct.unpack_from('<II', data)
    require(magic == SPR1 and count > 0 and len(data) == 8 + 4 * count, 'invalid SPR1 index table')
    return struct.unpack_from('<' + 'I' * count, data, 8)


def frame_count(data):
    require(len(data) >= 8, 'truncated actor clip')
    if data[:4] == b'SPR1':
        return len(clip_offsets(data))
    if data[:4] == b'SPR2':
        require(len(data) >= 12, 'truncated SPR2 header')
        count, limit = struct.unpack_from('<II', data, 4)
        require(limit == len(data) and count > 0 and 12 + 4 * count <= limit, 'invalid SPR2 source')
        return count
    count = struct.unpack_from('<I', data)[0]
    first = 4 + 4 * count
    require(count > 0 and first <= len(data), 'invalid independent clip index')
    for offset in struct.unpack_from('<' + 'I' * count, data, 4):
        require(first <= offset < len(data) and data[offset] in (16, 17), 'invalid independent frame offset/codec')
    return count


def closure(records, clips):
    roots = {offset for data in clips.values() for offset in clip_offsets(data)}
    pending, live = list(roots), set()
    while pending:
        offset = pending.pop()
        require(offset in records, 'clip/parent reference is not a record start')
        if offset in live:
            continue
        live.add(offset)
        parent = records[offset]['parent']
        if parent is not None:
            pending.append(parent)
    return roots, live


def compact(pool, clips):
    records = parse_pool(pool)
    roots, live = closure(records, clips)
    output, relocation = bytearray(pool[:8]), {}
    for offset, record in records.items():
        if offset not in live:
            continue
        relocation[offset] = len(output)
        body = bytearray(pool[offset:offset + 4 + record['size']])
        if record['parent'] is not None:
            struct.pack_into('<I', body, 4, relocation[record['parent']])
        output.extend(body)
    struct.pack_into('<I', output, 0, len(output))
    remapped = {name: data[:8] + struct.pack('<' + 'I' * len(clip_offsets(data)),
                *(relocation[offset] for offset in clip_offsets(data)))
                for name, data in clips.items()}
    # Reparse the result and verify every parent relation after relocation.
    moved_records = parse_pool(output)
    moved_roots, moved_live = closure(moved_records, remapped)
    require(len(moved_live) == len(moved_records) == len(live), 'compaction lost/live records remain unreachable')
    stats = {'pool_bytes_before': len(pool), 'pool_bytes_after': len(output),
        'reclaimed_bytes': len(pool) - len(output), 'records_before': len(records),
        'records_after': len(live), 'removed_records': len(records) - len(live),
        'root_records': len(roots), 'dependency_only_records': len(live - roots),
        'clips': len(clips), 'frames': sum(len(clip_offsets(data)) for data in clips.values())}
    return bytes(output), remapped, stats


def load_source(directory):
    pool = (directory / POOL_NAME).read_bytes()
    clips = {}
    for path in sorted(directory.glob('*.lz')):
        data = path.read_bytes()
        if data[:4] == b'SPR1':
            clip_offsets(data)
            clips[path.stem] = data
    require(bool(clips), 'source contains no SPR1 clips')
    return pool, clips


def archive_sources(pool, clips, manifest_paths, additional, directory=None):
    selected, metadata = {}, {}
    for path in manifest_paths:
        report = json.loads(path.read_text())
        require(report['pool_sha256'] == sha(pool), f'{path}: archive roots use a different source pool')
        for row in report['clips']:
            source = base64.b64decode(row['source_clip_base64'], validate=True)
            require(sha(source) == row['source_sha256'], f"{row['asset']}: source hash mismatch")
            require(len(source) == row['source_bytes'], 'source index size mismatch')
            require(frame_count(source) == row['frames'], 'source frame count mismatch')
            name = row['asset']
            require(name not in selected or selected[name] == source, 'conflicting archive sources')
            selected[name] = source
            metadata[name] = row
    for name in additional:
        require(Path(name).name == name, 'unsafe additional asset name')
        require(name in clips or directory is not None, f'{name}: archive source not found')
        data = clips[name] if name in clips else (directory / (name + '.lz')).read_bytes()
        frame_count(data)
        require(name not in selected or selected[name] == data, 'conflicting additional source')
        selected[name] = data
    return dict(sorted(selected.items())), metadata


def build_source_archive(pool, sources, metadata=None):
    """Return (pool bytes, manifest) for a relocatable, mixed-format source batch.

    SPR1 source clips retain their complete parent closure. Independent LZ and
    SPR2 clips are embedded unchanged. metadata optionally supplies original
    per-asset rows (for decoded_sha256). No filesystem writes are performed.
    """
    metadata = metadata or {}
    dependent = {name: data for name, data in sources.items() if data[:4] == b'SPR1'}
    archive_pool, relocated, _ = compact(pool, dependent)
    manifest = {'format_version': 1, 'pool_file': 'pool.lz',
        'pool_sha256': sha(archive_pool), 'pool_bytes': len(archive_pool), 'clips': []}
    for name, original in sorted(sources.items()):
        data = relocated[name] if name in relocated else original
        count = frame_count(data)
        row = {'asset': name, 'frames': count,
            'source_clip_base64': base64.b64encode(data).decode('ascii'),
            'source_sha256': sha(data), 'source_bytes': len(data),
            'provenance': {'pool_sha256': sha(pool), 'clip_sha256': sha(original)}}
        if metadata.get(name, {}).get('decoded_sha256'):
            row['decoded_sha256'] = metadata[name]['decoded_sha256']
        manifest['clips'].append(row)
    return archive_pool, manifest


def write_identical_or_new(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        require(path.read_bytes() == data, f'{path}: refusing to replace a different snapshot')
    else:
        path.write_bytes(data)


def json_bytes(value):
    return (json.dumps(value, indent=2) + '\n').encode()


def compare_pixels(old_pool, old_clips, new_pool, new_clips, work):
    """Both real C decoder outputs, all frames in reverse order, under sanitizers."""
    work.mkdir(parents=True, exist_ok=True)
    old_path, new_path, pairs = work / 'before.pool', work / 'after.pool', work / 'pairs.bin'
    old_path.write_bytes(old_pool)
    new_path.write_bytes(new_pool)
    with pairs.open('wb') as stream:
        stream.write(struct.pack('<I', len(old_clips)))
        for name, before in old_clips.items():
            after = new_clips[name]
            stream.write(struct.pack('<III', len(before), len(after), frame_count(before)))
            stream.write(before)
            stream.write(after)
    executable = work / 'actor-pool-roundtrip'
    subprocess.run(['gcc', '-O1', '-g', '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
        str(ROOT / 'tests/actor-pool-roundtrip.c'), str(ROOT / 'src/actor_frames.c'),
        str(ROOT / 'src/lz.c'), '-o', str(executable)], check=True)
    import os
    subprocess.run([str(executable), str(old_path), str(new_path), str(pairs)], check=True,
        env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0', 'UBSAN_OPTIONS': 'halt_on_error=1'})


def stage(source, destination, baseline, archive_manifest_paths, archive_names, batch):
    source, destination = source.resolve(), destination.resolve()
    generated = (ROOT / 'generated').resolve()
    require(destination != source and source not in destination.parents, 'stage cannot be inside source directory')
    require(destination != generated and generated not in destination.parents, 'stage cannot write generated/')
    require(batch and all(c.isalnum() or c in '-_' for c in batch), 'unsafe archive batch name')
    pool, clips = load_source(source)
    archived, metadata = archive_sources(pool, clips, archive_manifest_paths, archive_names, source)
    new_pool, new_clips, stats = compact(pool, clips)
    # A baseline is immutable; baseline conflicts stop before candidate writes.
    for name, data in {POOL_NAME: pool, **{n + '.lz': b for n, b in clips.items()}}.items():
        write_identical_or_new(baseline / name, data)
    output = destination / 'generated'
    write_identical_or_new(output / POOL_NAME, new_pool)
    for name, data in new_clips.items():
        write_identical_or_new(output / (name + '.lz'), data)
    report = {'format_version': 1, 'statistics': stats,
        'source_pool_sha256': sha(pool), 'output_pool_sha256': sha(new_pool),
        'clips': [{'asset': name, 'frames': len(clip_offsets(clips[name])),
                   'source_sha256': sha(clips[name]), 'output_sha256': sha(new_clips[name])}
                  for name in clips]}
    if archived:
        archive_pool, archive_report = build_source_archive(pool, archived, metadata)
        archive_clips = {row['asset']: base64.b64decode(row['source_clip_base64']) for row in archive_report['clips']}
        archive_path = destination / 'source_assets/actor_sources' / batch
        write_identical_or_new(archive_path / 'pool.lz', archive_pool)
        write_identical_or_new(archive_path / 'manifest.json', json_bytes(archive_report))
        report['archive'] = {'batch': batch, 'pool_bytes': len(archive_pool),
            'clips': len(archived), 'frames': sum(frame_count(data) for data in archived.values()),
            'pool_sha256': sha(archive_pool)}
    with tempfile.TemporaryDirectory(prefix='actor-pool-check-') as temporary:
        compare_pixels(pool, clips, new_pool, new_clips, Path(temporary) / 'runtime')
        if archived:
            compare_pixels(pool, archived, archive_pool, archive_clips, Path(temporary) / 'archive')
    # Idempotence is structural: a compact pool has no additional dead records.
    again_pool, again_clips, again_stats = compact(new_pool, new_clips)
    require(again_pool == new_pool and again_clips == new_clips and again_stats['reclaimed_bytes'] == 0,
            'second compaction is not identical')
    current_pool, current_clips = load_source(source)
    require(current_pool == pool and current_clips == clips, 'source changed during staging')
    report['pixel_verification'] = 'PASS all runtime/source-archive frames; ASAN/UBSAN, guards, reverse access'
    report['idempotent'] = True
    write_identical_or_new(destination / 'report.json', json_bytes(report))
    print(json.dumps(stats, indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=ROOT / 'generated')
    parser.add_argument('--stage', type=Path, help='write verified candidate outside generated/; never apply it')
    parser.add_argument('--baseline', type=Path, default=ROOT / 'build/stage20/baseline/generated')
    parser.add_argument('--archive-manifest', action='append', type=Path, default=[])
    parser.add_argument('--archive-clips', default='', help='comma-separated additional SPR1 clip names')
    parser.add_argument('--archive-batch', default='stage20')
    args = parser.parse_args()
    names = tuple(name for name in args.archive_clips.split(',') if name)
    if args.stage:
        stage(args.source_dir, args.stage, args.baseline, args.archive_manifest, names, args.archive_batch)
    else:
        pool, clips = load_source(args.source_dir)
        _, _, stats = compact(pool, clips)
        archived, metadata = archive_sources(pool, clips, args.archive_manifest, names, args.source_dir)
        if archived:
            archive_pool, _ = build_source_archive(pool, archived, metadata)
            stats['archive'] = {'pool_bytes': len(archive_pool), 'clips': len(archived),
                'frames': sum(frame_count(data) for data in archived.values())}
        print(json.dumps(stats, indent=2))


if __name__ == '__main__':
    main()
