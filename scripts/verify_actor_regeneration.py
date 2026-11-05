"""Decode regenerated hero clips with the production C decoder and guard words.

Compare each clip to its MPQ-conversion pixel hash and optionally to a private
release. Encoded offsets/pool layout may differ while decoded pixels agree.
"""
import argparse
import ctypes
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--lightning', action='store_true')
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.project / 'tools'))
    from pack_hot_monsters import Decoder
    generated = args.project / 'generated'
    manifest = json.loads((generated / ('cast-families-stage69-report.json' if args.lightning else 'weapon-heroes-report.json')).read_text())
    if args.lightning:
        entries = [{'asset': 'spell_'+r['prefix'].lower()+'_lm', 'frames': r['frame_count'], 'decoded_sha256': r['decoded_sha256']} for r in manifest['records']]
    else:
        entries = [info for family in manifest['sets'] for info in family['states'].values()]
    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        class ExternalDecoder(Decoder):
            def __init__(self, directory, pool, external):
                super().__init__(directory, pool)
                self.external = ctypes.create_string_buffer(external)
                self.library.actor_frame_external.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
                self.library.actor_frame_external.restype = ctypes.c_uint

            def frames(self, clip, count):
                source = ctypes.create_string_buffer(clip)
                destination = (ctypes.c_uint32 * 514)()
                scratch = (ctypes.c_uint32 * 514)()
                frames = []
                for i in range(count):
                    destination[0] = destination[513] = 0xA5A5A5A5
                    scratch[0] = scratch[513] = 0x5A5A5A5A
                    decoded = self.library.actor_frame_external(source, i, ctypes.byref(destination, 4), 2048, ctypes.byref(scratch, 4), self.pool, self.external)
                    assert decoded == 2048
                    assert destination[0] == destination[513] == 0xA5A5A5A5
                    assert scratch[0] == scratch[513] == 0x5A5A5A5A
                    frames.append(ctypes.string_at(ctypes.byref(destination, 4), 2048))
                return frames

        def make(directory):
            pool = (directory / 'hero_frame_pool.lz').read_bytes()
            return ExternalDecoder(tmp, (directory / 'lightning_frame_pool.lz').read_bytes(), pool) if args.lightning else Decoder(tmp, pool)

        decoder = make(generated)
        reference = make(args.baseline) if args.baseline else None
        for info in entries:
            name = info['asset'] + '.lz'
            count = info['frames'] * 8
            frames = decoder.frames((generated / name).read_bytes(), count)
            digest = hashlib.sha256(b''.join(frames)).hexdigest()
            row = dict(file=name, frames=count, source_pixels_match=digest == info['decoded_sha256'])
            if reference:
                data = (args.baseline / name).read_bytes()
                # Both current hero containers explicitly store their frame count.
                magic, reference_count = struct.unpack_from('<II', data)
                if magic not in (0x31525053, 0x32525053, 0x33525053):
                    reference_count = magic
                row['release_comparison'] = ('frame_count_differs' if reference_count != count else
                    'pixels_exact' if reference.frames(data, count) == frames else 'pixels_differ')
            rows.append(row)
    report = dict(clips=len(rows), frames=sum(r['frames'] for r in rows),
                  source_pixels_all_match=all(r['source_pixels_match'] for r in rows),
                  release_comparison=dict(Counter(r.get('release_comparison', 'not_requested') for r in rows)),
                  buffer_guards_passed=True, full_asset_regeneration_verified=False, results=rows)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'results'}, indent=2))
    if not report['source_pixels_all_match'] or (args.baseline and any(r['release_comparison'] != 'pixels_exact' for r in rows)):
        sys.exit(1)


if __name__ == '__main__':
    main()
