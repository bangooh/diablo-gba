"""Lossless codec candidates for the Stage19 combat fixtures.

The shared hero/monster pool is never changed. Original SPR1 index tables are
backed up before edits and embedded in the manifest so the conversion remains
reproducible when build/ is omitted from a source archive. --check is read-only.
After pool compaction, source_archive identifies the immutable source pool.
"""
from pathlib import Path
import argparse
import base64
import ctypes
import hashlib
import json
import struct
import subprocess
import tempfile

from lz77 import compress_actor, indexed_chunks

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'generated/hot-monsters-report.json'
BASELINE = ROOT / 'build/stage19/baseline/generated'
POOL = ROOT / 'generated/hero_frame_pool.lz'
TARGETS = ('stone_clan', 'horned_demon', 'blue_fallen_spear', 'blood_knight')
CLIPS = tuple(f'{name}_{state}' for name in TARGETS for state in 'nwah')
SPR1 = 0x31525053
SPR2 = 0x32525053
CODECS = {'independent': 'independent indexed compress_actor (0x10/0x11)',
          'raw-tiles': 'SPR2 independent aligned nonempty 8x8 tile records'}


def raw_tiles(frames):
    records, offsets, seen = bytearray(), [], {}
    first = 12 + 4 * len(frames)
    for frame in frames:
        if frame not in seen:
            mask, packed = 0, bytearray()
            for tile in range(64):
                pixels = frame[tile * 32:(tile + 1) * 32]
                if any(pixels):
                    mask |= 1 << tile
                    packed.extend(pixels)
            seen[frame] = first + len(records)
            records.extend(struct.pack('<III', len(packed), mask & 0xffffffff, mask >> 32))
            records.extend(packed)
        offsets.append(seen[frame])
    return (struct.pack('<III', SPR2, len(frames), first + len(records))
            + struct.pack('<' + 'I' * len(offsets), *offsets) + records)


def codec_key(report):
    return report.get('codec_key', 'independent')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def source_clip(record):
    data = base64.b64decode(record['source_clip_base64'], validate=True)
    assert len(data) == record['source_bytes'], record['asset'] + ': source size'
    assert sha(data) == record['source_sha256'], record['asset'] + ': source hash'
    magic, count = struct.unpack_from('<II', data)
    assert magic == SPR1 and len(data) == 8 + 4 * count
    assert count == record['frames'], record['asset'] + ': frame count'
    return data


def source_pool_path(report=None):
    report = report or json.loads(MANIFEST.read_text())
    if report.get('source_archive'):
        path = ROOT / report['source_archive']
        archive = json.loads(path.read_text())
        return path.parent / archive['pool_file']
    return POOL


class Decoder:
    """Use the shipped C decoder, with guard words around both output buffers."""

    def __init__(self, directory, pool):
        library = Path(directory) / 'actor-codec.so'
        subprocess.run(['gcc', '-shared', '-fPIC', '-O2',
                        str(ROOT / 'src/actor_frames.c'), str(ROOT / 'src/lz.c'),
                        '-o', str(library)], check=True)
        self.library = ctypes.CDLL(str(library))
        self.library.actor_frame.argtypes = [ctypes.c_void_p, ctypes.c_uint,
            ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_void_p]
        self.library.actor_frame.restype = ctypes.c_uint
        self.pool = ctypes.create_string_buffer(pool)

    def frames(self, clip, count):
        src = ctypes.create_string_buffer(clip)
        destination = (ctypes.c_uint32 * 514)()
        scratch = (ctypes.c_uint32 * 514)()
        result = []
        for index in range(count):
            destination[0] = destination[513] = 0xA5A5A5A5
            scratch[0] = scratch[513] = 0x5A5A5A5A
            decoded = self.library.actor_frame(src, index,
                ctypes.byref(destination, 4), 2048, ctypes.byref(scratch, 4), self.pool)
            assert decoded == 2048, f'frame {index}: decode failed'
            assert destination[0] == destination[513] == 0xA5A5A5A5
            assert scratch[0] == scratch[513] == 0x5A5A5A5A
            result.append(ctypes.string_at(ctypes.byref(destination, 4), 2048))
        return result


def read_manifest(pool):
    report = json.loads(MANIFEST.read_text())
    assert report['format_version'] in (1, 2, 3)
    assert report['codec'] == CODECS[codec_key(report)]
    assert report['pool_sha256'] == sha(pool), 'shared pool changed; rebuild the source manifest'
    assert report['pool_bytes'] == len(pool)
    assert tuple(row['asset'] for row in report['clips']) == CLIPS
    return report


def check(decoder, pool):
    report = read_manifest(pool)
    for row in report['clips']:
        original = source_clip(row)
        backup = BASELINE / (row['asset'] + '.lz')
        if backup.exists() and not report.get('source_archive'):
            assert backup.read_bytes() == original, str(backup) + ': backup changed'
        current = (ROOT / 'generated' / (row['asset'] + '.lz')).read_bytes()
        assert sha(current) == row['output_sha256'], row['asset'] + ': output hash'
        assert len(current) == row['output_bytes']
        if codec_key(report) == 'raw-tiles':
            assert struct.unpack_from('<III', current) == (SPR2, row['frames'], len(current))
        else:
            assert struct.unpack_from('<I', current)[0] == row['frames']
        expected = decoder.frames(original, row['frames'])
        actual = decoder.frames(current, row['frames'])
        assert actual == expected, row['asset'] + ': pixels changed'
        assert sha(b''.join(actual)) == row['decoded_sha256']
    assert report['frames'] == sum(row['frames'] for row in report['clips'])
    assert report['source_clip_bytes'] == sum(row['source_bytes'] for row in report['clips'])
    assert report['output_clip_bytes'] == sum(row['output_bytes'] for row in report['clips'])
    assert report['ROM_delta_bytes'] == report['output_clip_bytes'] - report['source_clip_bytes']
    print(f"PASS {len(CLIPS)} clips / {report['frames']} pixel-exact frames; pool, source and output hashes", flush=True)
    return report


def pack(decoder, pool, codec):
    previous = read_manifest(pool) if MANIFEST.exists() else None
    sources = {}
    for index, name in enumerate(CLIPS):
        path = ROOT / 'generated' / (name + '.lz')
        current = path.read_bytes()
        if previous:
            row = previous['clips'][index]
            original = source_clip(row)
            assert sha(current) in (row['source_sha256'], row['output_sha256']), name + ': unrecognized input'
        else:
            original = current
        magic, count = struct.unpack_from('<II', original)
        assert magic == SPR1 and len(original) == 8 + 4 * count, name + ': expected original SPR1 clip'
        sources[name] = original

    # Preserve every original before changing any generated clip. Existing
    # baseline files must agree; never overwrite a different baseline.
    if not previous or not previous.get('source_archive'):
        BASELINE.mkdir(parents=True, exist_ok=True)
        for name, original in sources.items():
            backup = BASELINE / (name + '.lz')
            if backup.exists():
                assert backup.read_bytes() == original, str(backup) + ': baseline differs'
            else:
                backup.write_bytes(original)

    rows, outputs = [], {}
    for name, original in sources.items():
        count = struct.unpack_from('<I', original, 4)[0]
        frames = decoder.frames(original, count)
        encoded = raw_tiles(frames) if codec == 'raw-tiles' else indexed_chunks([compress_actor(frame) for frame in frames])
        assert decoder.frames(encoded, count) == frames, name + ': roundtrip mismatch'
        outputs[name] = encoded
        rows.append({'asset': name, 'frames': count,
            'source_bytes': len(original), 'source_sha256': sha(original),
            'source_clip_base64': base64.b64encode(original).decode('ascii'),
            'output_bytes': len(encoded), 'output_sha256': sha(encoded),
            'decoded_sha256': sha(b''.join(frames))})
        print(f'{name}: {count} frames -> {len(encoded)} bytes', flush=True)
    source_archive = previous.get('source_archive') if previous else None
    report = {'format_version': 3 if source_archive else 2, 'codec_key': codec, 'codec': CODECS[codec],
        'purpose': 'Stage19 codec A/B candidate; performance must be measured separately',
        'pool_bytes': len(pool), 'pool_sha256': sha(pool), 'pool_unchanged': not bool(source_archive),
        'extra_RAM_bytes': 0, 'frames': sum(row['frames'] for row in rows),
        'source_clip_bytes': sum(row['source_bytes'] for row in rows),
        'output_clip_bytes': sum(row['output_bytes'] for row in rows), 'clips': rows}
    report['ROM_delta_bytes'] = report['output_clip_bytes'] - report['source_clip_bytes']
    if source_archive:
        report['source_archive'] = source_archive
    if previous and previous['format_version'] in (2, 3) and codec_key(previous) == codec:
        assert report == previous, 'regenerated manifest differs'
    origin = source_pool_path(previous) if previous else POOL
    assert origin.read_bytes() == pool, 'source pool changed during conversion'
    for name, encoded in outputs.items():
        path = ROOT / 'generated' / (name + '.lz')
        if path.read_bytes() != encoded:
            path.write_bytes(encoded)
    payload = json.dumps(report, indent=2) + '\n'
    if not MANIFEST.exists() or MANIFEST.read_text() != payload:
        MANIFEST.write_text(payload)
    print(f"Packed {report['frames']} frames; ROM delta +{report['ROM_delta_bytes']} bytes; extra RAM 0", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='verify hashes and pixel equality without writing files')
    parser.add_argument('--codec', choices=tuple(CODECS), help='regenerate independent LZ or raw tiles (default: current manifest codec)')
    arguments = parser.parse_args()
    pool = source_pool_path().read_bytes() if MANIFEST.exists() else POOL.read_bytes()
    with tempfile.TemporaryDirectory(prefix='hot-monster-codec-') as directory:
        decoder = Decoder(directory, pool)
        if arguments.check:
            report = check(decoder, pool)
            if arguments.codec:
                assert codec_key(report) == arguments.codec, 'manifest uses a different codec'
        else:
            selected = arguments.codec or (codec_key(read_manifest(pool)) if MANIFEST.exists() else 'independent')
            pack(decoder, pool, selected)


if __name__ == '__main__':
    main()
