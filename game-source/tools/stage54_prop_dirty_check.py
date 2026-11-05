#!/usr/bin/env python3
"""Validate actual prop dirty code against independent LZ10 pixel oracles."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def macro(path, name):
    match = re.search(r'^#define ' + name + r' (\d+)\s*$', path.read_text(), re.M)
    assert match, name
    return int(match[1])


def independent_frame(blob, offset, end):
    """Bounds-checked LZ10 reader independent of the generator and C decoder."""
    assert offset + 4 <= end and blob[offset] == 0x10
    expected = int.from_bytes(blob[offset + 1:offset + 4], 'little')
    assert expected == 4096
    at, pixels = offset + 4, bytearray()
    while len(pixels) < expected:
        assert at < end
        flags = blob[at]
        at += 1
        for bit in range(7, -1, -1):
            if len(pixels) == expected:
                break
            if flags & (1 << bit):
                assert at + 2 <= end
                first, second = blob[at:at + 2]
                at += 2
                count, distance = (first >> 4) + 3, ((first & 15) << 8) + second + 1
                assert distance <= len(pixels) and len(pixels) + count <= expected
                for _ in range(count):
                    pixels.append(pixels[len(pixels) - distance])
            else:
                assert at < end
                pixels.append(blob[at])
                at += 1
    assert at == end, 'unused bytes in independently indexed frame'
    return bytes(pixels)


def frames(path, base, count):
    blob = path.read_bytes()
    total = struct.unpack_from('<I', blob)[0]
    assert base + count <= total and 4 + 4 * total <= len(blob)
    offsets = struct.unpack_from(f'<{total}I', blob, 4)
    unique = sorted(set(offsets))
    assert unique[0] >= 4 + 4 * total
    ends = dict(zip(unique, unique[1:] + [len(blob)]))
    return b''.join(independent_frame(blob, offsets[i], ends[offsets[i]])
                    for i in range(base, base + count))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT / 'work/stage54/prop-dirty-host')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    sources = [
        ('regular', 'prop_frames.lz', 'prop_frames.h', 'PROP_BARREL_BASE', 'PROP_BARREL_FRAMES'),
        ('explosive', 'explosive_barrels.lz', 'explosive_barrels.h', None, 'PROP_BARRELEX_FRAMES'),
        ('candle', 'prop_frames.lz', 'water_candles.h', 'PROP_WATER_CANDLE_BASE', 'PROP_WATER_CANDLE_FRAMES'),
    ]
    inputs, hashes = [], {}
    for name, asset, header, base_macro, count_macro in sources:
        definitions = ROOT / 'generated' / header
        base = macro(definitions, base_macro) if base_macro else 0
        count = macro(definitions, count_macro)
        pixels = frames(ROOT / 'generated' / asset, base, count)
        if asset == 'prop_frames.lz':
            raw = (ROOT / 'generated/prop_frames.bin').read_bytes()
            assert pixels == raw[base * 4096:(base + count) * 4096]
        path = out / (name + '-pixels.bin')
        path.write_bytes(pixels)
        inputs.append(path)
        hashes[name] = {'frames': count, 'decoded_sha256': hashlib.sha256(pixels).hexdigest()}
    source = (ROOT / 'src/gba_mode0.c').read_text()
    start = source.index('static UI_FN void invalidate_prop(')
    end = source.index('\n}', start) + 2
    fallback = source[start:end].replace('void invalidate_prop(', 'void stage54_original_invalidate_prop(', 1)
    (out / 'stage54_original_invalidate.inc').write_text(fallback + '\n')
    executable = out / 'prop-dirty-check'
    subprocess.run(['gcc', '-std=gnu11', '-DGRAPHICS_ASSETS', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-pie', '-no-pie', '-I' + str(ROOT), '-I' + str(out),
                    str(ROOT / 'tools/stage54_prop_dirty_check.c'), '-o', str(executable)], check=True)
    run = subprocess.run([str(executable), *map(str, inputs)], check=True, text=True,
                         capture_output=True, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0',
                                                   'UBSAN_OPTIONS': 'halt_on_error=1'})
    report = json.loads(run.stdout)
    report.update({'oracle_sources': hashes, 'sanitizers': ['address', 'undefined'],
                   'production_helper_sha256': hashlib.sha256((ROOT / 'src/prop_dirty.inc').read_bytes()).hexdigest(),
                   'generated_header_sha256': hashlib.sha256((ROOT / 'generated/prop_dirty.h').read_bytes()).hexdigest(),
                   'coverage': 'Every 81 regular, 100 explosive and 16 candle frame pair at every 8px placement around a 31x21 viewport, plus all 6400 world origins; ring aliases, partial/offscreen clips, strict actor intersections, initial/invalid frames, unrelated props and regional-door fallback.'})
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
