"""Pack all-class Sorcerer ending:240x117,5FPS,16 shades tinted per frame.

Normal invocation regenerates the selected ED49 key/XOR-delta stream from the
preserved native5FPS gray source. --check validates existing assets against
that source through the production C helper and decoder, without recompressing.
--smk can recreate the preserved source from the original DiabVic1.smk.
--palette-only updates the tint palettes/header/report while retaining the
existing compressed video and source files without rewriting or reencoding.

Colour: the video stays 16 gray levels. Each frame gets its own 16-colour
palette: entry k is the mean original colour of the pixels at gray level k
(gaps interpolated, 1-2-1 temporal smoothing, entry0 black). The palettes
are measured from the original DiabVic1.smk colours and kept beside the
preserved gray source, so --check needs no movie.
"""
from pathlib import Path
import argparse
import concurrent.futures
import ctypes
import hashlib
import json
import struct
import subprocess
import tempfile

import numpy as np
from PIL import Image
from stage49_pack_sorcerer import SOURCE, source_frames, compress_pair, delta_container

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'generated'
WIDTH, HEIGHT, BITS, FPS, FRAMES = 240, 117, 4, 5, 373
PACKED_BYTES = WIDTH*HEIGHT//2
MOVIE_SHA = '1db2a2bf4994633ac04ce843d29ebdabe5cf937f8f68e810a4695444aa10cd29'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def packed_source(frames):
    grey = np.stack([np.asarray(Image.fromarray(f, 'L').resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS))
                     for f in frames])
    indices = ((grey.astype(np.uint16)*15+127)//255).astype(np.uint8).reshape(len(frames), -1)
    return indices[:, ::2] | (indices[:, 1::2] << 4)


TINT_CACHE = SOURCE / 'tint-palettes-5fps.npy'


def tint_palettes(smk, packed):
    """Per-frame RGB555 palettes for the 16 gray levels (see module notes)."""
    if not smk:
        assert TINT_CACHE.exists(), 'First run needs --smk /path/to/DiabVic1.smk'
        tint = np.load(TINT_CACHE)
        assert tint.shape == (FRAMES, 16) and tint.dtype == np.uint16
        return tint
    raw = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(smk), '-an', '-vf', 'fps=5',
                                   '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'])
    native = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 156, 320, 3)
    assert len(native) == FRAMES
    levels = np.empty((FRAMES, WIDTH*HEIGHT), np.uint8)
    levels[:, ::2], levels[:, 1::2] = packed & 15, packed >> 4
    means = np.zeros((FRAMES, 16, 3))
    for n, frame in enumerate(native):
        rgb = np.asarray(Image.fromarray(frame).resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS),
                         dtype=np.float64).reshape(-1, 3)
        known = [0]
        for k in range(1, 16):
            mask = levels[n] == k
            if mask.sum() >= 20:
                means[n, k] = rgb[mask].mean(0)
                known.append(k)
        for k in range(1, 16):
            if k in known:
                continue
            low = max(j for j in known if j < k)
            high = [j for j in known if j > k]
            if high:
                t = (k-low)/(high[0]-low)
                means[n, k] = means[n, low]*(1-t) + means[n, high[0]]*t
            else:
                means[n, k] = means[n, low] + (k-low)*np.array([12.0, 9.0, 6.0])
    smooth = means.copy()
    smooth[1:-1] = (means[:-2] + 2*means[1:-1] + means[2:]) / 4
    five = np.round(np.clip(smooth, 0, 255)*31/255).astype(np.uint16)
    tint = five[..., 0] | (five[..., 1] << 5) | (five[..., 2] << 10)
    tint[:, 0] = 0
    np.save(TINT_CACHE, tint)
    return tint


def header_text():
    return ('/* Native Sorcerer DiabVic1.smk, used for all three classes.\n'
            ' * ED49 format:16-byte header, uint32 refs, Nintendo-LZ payload.\n'
            ' * Ref0 repeats; bit31 XORs the decoded block with the prior frame.\n'
            ' * Decode every frame sequentially; the audio clock owns timing. */\n'
            '#pragma once\n#include <stdint.h>\n'
            '#define ENDING_VIDEO_WIDTH 240\n#define ENDING_VIDEO_HEIGHT 117\n'
            '#define ENDING_VIDEO_TOP 21\n#define ENDING_VIDEO_FPS 5\n'
            '#define ENDING_VIDEO_FRAMES 373\n#define ENDING_VIDEO_COLORS 16\n'
            '#define ENDING_VIDEO_PACKED_BYTES 14040\n'
            '#define ENDING_VIDEO_INDEX_OFFSET 16\n'
            '#define ENDING_VIDEO_DELTA_FLAG 0x80000000u\n'
            '#define ENDING_VIDEO_OFFSET_MASK 0x7fffffffu\n'
            'extern const uint8_t asset_ending_video[];\n'
            '/* Raw RGB555, 16 colours per frame in frame order; index0 is black. */\n'
            'extern const uint8_t asset_ending_video_palette[];\n')


def validate(blob, packed):
    assert struct.unpack_from('<4sHHBBHI', blob) == (b'ED49', WIDTH, HEIGHT, BITS, FPS, FRAMES, PACKED_BYTES)
    refs = struct.unpack_from('<373I', blob, 16)
    assert refs[0] and not refs[0] & 0x80000000
    offsets = sorted({r & 0x7fffffff for r in refs if r})
    assert offsets[0] == 16+4*FRAMES and offsets[-1] < len(blob)
    assert all(blob[o] == 0x10 and int.from_bytes(blob[o+1:o+4], 'little') == PACKED_BYTES for o in offsets)
    with tempfile.TemporaryDirectory(prefix='ending-video-c-') as temp:
        temp = Path(temp)
        (temp / 'video.bin').write_bytes(blob)
        (temp / 'assets.s').write_text('.section .rodata\n.balign 4\n.global asset_ending_video\nasset_ending_video:\n.incbin "'
                                      +str(temp / 'video.bin')+'"\n.section .note.GNU-stack,"",@progbits\n')
        (temp / 'helper.c').write_text('#include <stdint.h>\n#include <string.h>\n#include "lz.h"\n'
                                      'static uint32_t load_buffer[4672];\n#define UI_FN\n'
                                      'static void dma(const void*s,void*d,unsigned n){memcpy(d,s,n);}\n'
                                      '#include "ending_video.inc"\n'
                                      'int test_frame(unsigned n,uint32_t*p){return ending_video_frame(n,p);}\n')
        library = temp / 'helper.so'
        subprocess.run(['gcc', '-O2', '-shared', '-fPIC', '-I'+str(ROOT / 'src'),
                        str(temp / 'helper.c'), str(ROOT / 'src/lz.c'), str(temp / 'assets.s'),
                        '-o', str(library)], check=True)
        decoder = ctypes.CDLL(str(library))
        decoder.test_frame.argtypes = [ctypes.c_uint, ctypes.c_void_p]
        decoder.test_frame.restype = ctypes.c_int
        destination = (ctypes.c_uint32*(PACKED_BYTES//4+2))()
        destination[0] = destination[-1] = 0xA5A5A5A5
        for frame, expected in enumerate(packed):
            assert decoder.test_frame(frame, ctypes.byref(destination, 4)) == 1
            assert destination[0] == destination[-1] == 0xA5A5A5A5
            assert ctypes.string_at(ctypes.byref(destination, 4), PACKED_BYTES) == expected.tobytes(), frame
        before = bytes(destination)
        assert decoder.test_frame(FRAMES, ctypes.byref(destination, 4)) == 0
        assert bytes(destination) == before
    ends = offsets[1:] + [len(blob)]
    return {'production_c_helper_all_frames': 'PASS', 'destination_canaries_intact': True,
            'out_of_range_rejected_without_write': True,
            'key_frames': sum(bool(r) and not bool(r >> 31) for r in refs),
            'xor_delta_frames': sum(bool(r >> 31) for r in refs),
            'repeat_frames': sum(not r for r in refs), 'unique_lz_blocks': len(offsets),
            'maximum_compressed_block_bytes': max(b-a for a, b in zip(offsets, ends))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--smk', type=Path)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--reuse-prototype', action='store_true')
    parser.add_argument('--palette-only', action='store_true')
    parser.add_argument('--workers', type=int, default=4)
    args = parser.parse_args()
    assert not (args.check and args.reuse_prototype)
    if args.palette_only and (args.check or args.reuse_prototype):
        parser.error('--palette-only retains existing video; do not combine it with --check or --reuse-prototype')
    if args.check and args.smk:
        parser.error('--check reads the preserved source; use --smk separately to regenerate it')
    if args.smk:
        assert sha(args.smk.read_bytes()) == MOVIE_SHA, 'Expected original DiabVic1.smk'
    frames, source = source_frames(args.smk)
    assert source['movie_sha256'] == MOVIE_SHA and len(frames) == FRAMES
    packed = packed_source(frames)
    if args.check or args.palette_only:
        blob = (OUT / 'ending_video.lz').read_bytes()
    elif args.reuse_prototype:
        blob = (OUT / 'stage49-sorcerer/240x117-4bpp-delta.bin').read_bytes()
    else:
        previous = np.concatenate([np.zeros_like(packed[:1]), packed[:-1]])
        delta = np.bitwise_xor(packed, previous)
        raw = [row.tobytes() for row in packed]
        with concurrent.futures.ProcessPoolExecutor(max_workers=args.workers) as pool:
            compressed = list(pool.map(compress_pair, zip(raw, (row.tobytes() for row in delta)), chunksize=4))
        blob, _ = delta_container(raw, compressed, WIDTH, HEIGHT, BITS)
    tint = tint_palettes(args.smk, packed)
    palette = tint.astype('<u2').tobytes()
    assert len(palette) == FRAMES*32 and not tint[:, 0].any()
    # The C helper includes the header, so write it before compiling validation.
    header = header_text().encode()
    if args.check:
        assert (OUT / 'ending_video.h').read_bytes() == header
    else:
        (OUT / 'ending_video.h').write_bytes(header)
    checks = validate(blob, packed)
    report = {'status': 'PASS', 'source_movie': source['mpq_path'], 'source_movie_sha256': MOVIE_SHA,
              'source_native_frame_count': 1118, 'source_duration_seconds': 74.52588,
              'source_gray_5fps_sha256': source['native_gray_sha256'],
              'width': WIDTH, 'height': HEIGHT, 'top': 21, 'fps': FPS, 'frames': FRAMES,
              'bits_per_pixel': BITS, 'colors': 16, 'palette_zero_is_black': True,
              'palette_tone': 'per-frame tint: mean original colour of each gray level',
              'palettes': FRAMES,
              'video_bytes': len(blob), 'palette_bytes': len(palette),
              'aligned_rom_asset_bytes': ((len(blob)+3)&~3)+len(palette),
              'packed_frame_bytes': PACKED_BYTES, 'decoded_frames_sha256': sha(packed.tobytes()),
              'video_sha256': sha(blob), 'palette_sha256': sha(palette),
              'extra_persistent_ram_bytes': 0,
              'scratch': 'existing load_buffer for14040-byte decode; caller retains prior packed frame in ending town_map workspace',
              'timing': 'decode sequential frame indices through floor(audio_sample_position*5/16384); audio completion ends final frame at native duration',
              **checks}
    files = {'ending_video.lz': blob, 'ending_video_palette.lz': palette,
             'ending_video-report.json': (json.dumps(report, indent=2)+'\n').encode()}
    for name, data in files.items():
        if args.check:
            assert (OUT / name).read_bytes() == data, name
        elif args.palette_only and name == 'ending_video.lz':
            assert (OUT / name).read_bytes() == data, 'Palette edit changed video bytes'
        else:
            (OUT / name).write_bytes(data)
    print(f'PASS: Sorcerer ending373 frames,240x117,5FPS,16 tinted shades per frame; '
          f'{report["aligned_rom_asset_bytes"]} aligned ROM bytes; all frames validated through production C helper')


if __name__ == '__main__':
    main()
