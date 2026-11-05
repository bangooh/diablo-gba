"""Sample the immutable 15FPS opening at 10FPS, preserving palette and pixels.

Both logos retain their original 11s/5s durations. This reads the complete
240-frame front_video.lz, never an earlier subsampled output. Without --codec,
only comparison candidates and their validation report are written. Selecting
--codec independent/delta also writes the production front_opening assets.
The delta codec is the existing sequential full/XOR/aligned-span LZ10 format.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

from lz77 import decompress, indexed_chunks
from pack_front_delta import ENCODER, FRAME_BYTES, patch_between

ROOT = Path(__file__).resolve().parents[1]
COUNT, FPS, BLUE_COUNT = 160, 10, 110
SOURCE_COUNT = 240
SOURCE_INDICES = [(3 * i + 1) // 2 for i in range(COUNT)]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def checked_lz10(data):
    """Reject malformed/truncated streams before the unbounded ROM C reader."""
    assert len(data) >= 4 and data[0] == 16
    size, written, at = int.from_bytes(data[1:4], 'little'), 0, 4
    while written < size:
        assert at < len(data)
        flags = data[at]
        at += 1
        for bit in range(8):
            if written == size:
                break
            if flags & (128 >> bit):
                assert at + 2 <= len(data)
                token = int.from_bytes(data[at:at + 2], 'big')
                count, distance = (token >> 12) + 3, (token & 4095) + 1
                assert distance <= written and written + count <= size
                written += count
                at += 2
            else:
                assert at < len(data)
                at += 1
                written += 1
    assert at == len(data), 'Trailing bytes or an incorrect record boundary.'
    result = decompress(data)
    assert len(result) == size
    return result


def native_encoder(directory):
    c, so = directory / 'encoder.c', directory / 'encoder.so'
    c.write_text(ENCODER)
    subprocess.run(['gcc', '-O3', '-shared', '-fPIC', str(c), '-o', str(so)], check=True)
    native = ctypes.CDLL(str(so))
    native.encode.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p]
    native.encode.restype = ctypes.c_uint

    def compress(data):
        buffer = ctypes.create_string_buffer(len(data) * 2 + 16)
        size = native.encode(data, len(data), buffer)
        assert size >= 4
        packed = buffer.raw[:size]
        assert checked_lz10(packed) == data
        return packed

    return compress


def verify_candidates(directory, candidates, frames, capacity):
    wrapper = directory / 'verify.c'
    wrapper.write_text('#include <stdint.h>\n#include <string.h>\n#include "lz.h"\n#define UI_FN\n'
                       'static void dma(const void *s,void *d,unsigned n){memcpy(d,s,n);}\n'
                       '#include "front_delta.inc"\n'
                       'unsigned verify_delta(const uint8_t*s,unsigned f,uint8_t*p,uint8_t*t,unsigned n)'
                       '{return front_delta_decode(s,f,p,t,n);}\n')
    so = directory / 'verify.so'
    subprocess.run(['gcc', '-O2', '-shared', '-fPIC', '-I' + str(ROOT / 'src'),
                    str(wrapper), str(ROOT / 'src/lz.c'), '-o', str(so)], check=True)
    native = ctypes.CDLL(str(so))
    native.verify_delta.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p,
                                    ctypes.c_void_p, ctypes.c_uint]
    native.verify_delta.restype = ctypes.c_uint
    native.lz_block.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_uint]
    native.lz_block.restype = ctypes.c_uint
    results = {}
    for codec, packed in candidates.items():
        source = ctypes.create_string_buffer(packed)
        pixels = ctypes.create_string_buffer(b'\xa5' * (FRAME_BYTES + 128))
        scratch = ctypes.create_string_buffer(b'\x5a' * (capacity + 128))
        pixel_start, scratch_start = ctypes.addressof(pixels) + 64, ctypes.addressof(scratch) + 64
        for index, frame in enumerate(frames):
            if codec == 'delta':
                result = native.verify_delta(source, index, pixel_start, scratch_start, capacity)
                assert result == 1, (codec, index)
            else:
                result = native.lz_block(source, index, pixel_start, FRAME_BYTES)
                assert result == FRAME_BYTES, (codec, index)
            assert pixels.raw[64:64 + FRAME_BYTES] == frame, (codec, index)
            assert pixels.raw[:64] == pixels.raw[64 + FRAME_BYTES:128 + FRAME_BYTES] == b'\xa5' * 64
            assert scratch.raw[:64] == scratch.raw[64 + capacity:128 + capacity] == b'\x5a' * 64
        if codec == 'delta':
            assert native.verify_delta(source, len(frames), pixel_start, scratch_start, capacity) == 0
        else:
            assert native.lz_block(source, len(frames), pixel_start, FRAME_BYTES) == 0
        results[codec] = {'all_frames_bit_exact': 'PASS', 'frame_and_scratch_canaries': 'PASS',
                          'frame_count_boundary': 'PASS'}
    return results


def build(directory, source_path, palette_path):
    original, palette = source_path.read_bytes(), palette_path.read_bytes()
    source_count = struct.unpack_from('<I', original)[0]
    assert source_count == SOURCE_COUNT, 'Input must contain the complete 240 original frames.'
    assert SOURCE_INDICES[BLUE_COUNT] == 165 and SOURCE_INDICES[-1] == 239
    offsets = struct.unpack_from('<' + 'I' * source_count, original, 4)
    ends = dict(zip(sorted(set(offsets)), sorted(set(offsets))[1:] + [len(original)]))
    assert min(offsets) == 4 + 4 * source_count and max(offsets) < len(original)
    compress = native_encoder(directory)
    decoded, full_blocks, frames = {}, [], []
    for index in SOURCE_INDICES:
        offset = offsets[index]
        if offset not in decoded:
            decoded[offset] = checked_lz10(original[offset:ends[offset]])
            assert len(decoded[offset]) == FRAME_BYTES
        frames.append(decoded[offset])
        full_blocks.append(original[offset:ends[offset]])
    assert len(checked_lz10(palette)) == 512
    previous, records, kinds = bytes(FRAME_BYTES), [], [0] * 4
    record_metadata = []
    scratch_bytes = FRAME_BYTES
    max_patch_bytes = 0
    for frame, full in zip(frames, full_blocks):
        patch = patch_between(previous, frame)
        max_patch_bytes = max(max_patch_bytes, len(patch))
        if not patch:
            record = b'\0'
        else:
            xor = bytes(a ^ b for a, b in zip(frame, previous))
            choices = [b'\1' + full, b'\2' + compress(xor), b'\3' + compress(patch)]
            record = min(choices, key=len)
            if record[0] == 3:
                scratch_bytes = max(scratch_bytes, len(patch))
        records.append(record)
        kinds[record[0]] += 1
        spans = decoded_bytes = 0
        if record[0]:
            raw = checked_lz10(record[1:])
            decoded_bytes = len(raw)
            if record[0] in (1, 2):
                assert len(raw) == FRAME_BYTES
            else:
                at = 0
                while at < len(raw):
                    assert at + 4 <= len(raw)
                    offset, count = struct.unpack_from('<HH', raw, at)
                    at += 4
                    assert count and not ((offset | count | at) & 3)
                    assert offset + count <= FRAME_BYTES and at + count <= len(raw)
                    at += count
                    spans += 1
                assert at == len(raw)
        record_metadata.append({'kind': record[0], 'record_bytes': len(record),
                                'decoded_bytes': decoded_bytes, 'patch_spans': spans})
        previous = frame
    candidates = {'independent': indexed_chunks(full_blocks), 'delta': indexed_chunks(records)}
    validation = verify_candidates(directory, candidates, frames, scratch_bytes)
    report = {
        'status': 'PASS', 'source': str(source_path.relative_to(ROOT)) if source_path.is_relative_to(ROOT) else str(source_path),
        'source_sha256': sha(original), 'source_bytes': len(original), 'source_frames': source_count,
        'palette_source': str(palette_path.relative_to(ROOT)) if palette_path.is_relative_to(ROOT) else str(palette_path),
        'palette_sha256': sha(palette), 'palette_unchanged': True,
        'frame_bytes': FRAME_BYTES, 'frames': COUNT, 'fps': FPS, 'blue_frames': BLUE_COUNT,
        'white_frames': COUNT - BLUE_COUNT, 'source_indices': SOURCE_INDICES,
        'sample_rule': 'nearest-half-up source index=floor((3*output_index+1)/2)',
        'start_ticks': [i * 16384 // FPS for i in range(COUNT)],
        'deadline_ticks': [(i + 1) * 16384 // FPS for i in range(COUNT)],
        'blue_end_ticks': BLUE_COUNT * 16384 // FPS, 'total_ticks': COUNT * 16384 // FPS,
        'timer_hz': 16384, 'total_seconds': COUNT / FPS,
        'decoded_selected_sha256': sha(b''.join(frames)),
        'decoded_frame_sha256': [sha(frame) for frame in frames],
        'persistent_ram_added': 0,
        'candidates': {
            codec: {'bytes': len(data), 'saved_bytes': len(original) - len(data),
                    'sha256': sha(data), 'scratch_bytes': scratch_bytes if codec == 'delta' else 0,
                    'production_c_validation': validation[codec]}
            for codec, data in candidates.items()
        },
        'delta_record_counts': dict(zip(('repeat', 'full', 'xor', 'patch'), kinds)),
        'delta_records': record_metadata,
        'record_stream_boundaries_and_patch_bounds': 'PASS',
        'delta_largest_unselected_or_selected_patch': max_patch_bytes,
        'delta_sequence': 'Decode all frames in order; previous pixels stay in scene arena between frames.'
    }
    return candidates, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'generated/front_video.lz')
    parser.add_argument('--palette', type=Path, default=ROOT / 'generated/front_video_palette.lz')
    parser.add_argument('--codec', choices=['independent', 'delta'])
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'generated')
    parser.add_argument('--candidate-dir', type=Path, default=ROOT / 'generated/stage52-validation/opening')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='front-opening-') as temporary:
        candidates, report = build(Path(temporary), args.source.resolve(), args.palette.resolve())
    outputs = {args.candidate_dir / ('front-opening-' + name + '.bin'): data for name, data in candidates.items()}
    if args.codec:
        meta = report['candidates'][args.codec]
        report['selected_codec'] = args.codec
        header = ('/* Generated by tools/pack_front_opening.py; 10FPS, original palette/pixels. */\n'
                  '#pragma once\n#include <stdint.h>\nextern const uint8_t asset_front_opening_video[];\n'
                  f'#define FRONT_OPENING_DELTA {int(args.codec == "delta")}\n'
                  f'enum {{ FRONT_OPENING_FRAMES={COUNT}, FRONT_OPENING_FPS={FPS}, FRONT_OPENING_BLUE_FRAMES={BLUE_COUNT},\n'
                  f'       FRONT_OPENING_FRAME_BYTES={FRAME_BYTES}, FRONT_OPENING_SCRATCH_BYTES={meta["scratch_bytes"]},\n'
                  f'       FRONT_OPENING_STREAM_BYTES={meta["bytes"]} }};\n')
        outputs[args.output_dir / 'front_opening_video.lz'] = candidates[args.codec]
        outputs[args.output_dir / 'front_opening.h'] = header.encode()
        outputs[args.output_dir / 'front-opening-report.json'] = (json.dumps(report, indent=2) + '\n').encode()
    outputs[args.candidate_dir / 'comparison.json'] = (json.dumps(report, indent=2) + '\n').encode()
    for path, data in outputs.items():
        if args.check:
            assert path.read_bytes() == data, str(path) + ' is stale'
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    print(json.dumps({key: report[key] for key in ('status', 'frames', 'fps', 'blue_frames', 'total_seconds',
                     'candidates', 'delta_record_counts')}, indent=2))


if __name__ == '__main__':
    main()
