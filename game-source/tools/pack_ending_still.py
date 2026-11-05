"""Pack the original common epilogue frame for the GBA ending.

python3 pack_ending_still.py /path/to/DIABDAT.MPQ
python3 pack_ending_still.py --check

The source is zero-based frame275 of retail gendata\\Diabend.smk. It is a
static adaptation of the common epilogue, not an implementation of the movie.
Source pal8 pixels and palette are retained in a PNG so the same resampling
pipeline can be repeated without retaining the seven-megabyte movie.
"""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import struct
import subprocess
import sys
import tempfile

from PIL import Image, __version__ as PIL_VERSION

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'generated'
SOURCE_DIR = ROOT / 'source_assets/ending'
sys.path.insert(0, str(ROOT / 'tools'))
from lz77 import compress, decompress

MOVIE = 'gendata\\Diabend.smk'
MOVIE_SHA = 'ce6e5a04e4f557362033c3fe07b1a54b5029f875fee652ab9adcc8f3153efa75'
NATIVE_SHA = '7c3a602ce9acc6d1c00bbc18ffa53d211ae78e8eaa64641d2945ac32c7dad812'
FRAME = 275
SOURCE_W, SOURCE_H = 320, 156
WIDTH, HEIGHT = 240, 117
COLORS, PALETTE_BASE, BACKGROUND, TOP = 16, 16, 252, 21
PACKED_BYTES = WIDTH * HEIGHT // 2


def sha(data):
    return hashlib.sha256(data).hexdigest()


def extract(mpq):
    import mpyq
    from extract_assets import read_mpq
    archive = mpyq.MPQArchive(str(mpq), listfile=False)
    movie = read_mpq(archive, MOVIE)
    assert len(movie) == 7777752 and sha(movie) == MOVIE_SHA
    assert struct.unpack_from('<III', movie, 4) == (SOURCE_W, SOURCE_H, 825)
    with tempfile.TemporaryDirectory(prefix='ending-native-') as temp:
        path = Path(temp) / 'Diabend.smk'
        path.write_bytes(movie)
        frame = subprocess.check_output([
            'ffmpeg', '-v', 'error', '-i', str(path), '-vf',
            f'select=eq(n\\,{FRAME})', '-frames:v', '1', '-f', 'rawvideo',
            '-pix_fmt', 'pal8', '-'])
    assert len(frame) == SOURCE_W * SOURCE_H + 1024
    assert all(v == 255 for v in frame[SOURCE_W * SOURCE_H + 3::4])
    return frame


def source_png(frame):
    pixels, palette = frame[:SOURCE_W * SOURCE_H], frame[SOURCE_W * SOURCE_H:]
    image = Image.frombytes('P', (SOURCE_W, SOURCE_H), pixels)
    image.putpalette(bytes(v for i in range(256)
                           for v in (palette[4*i+2], palette[4*i+1], palette[4*i])))
    return image


def source_bytes(image):
    assert image.mode == 'P' and image.size == (SOURCE_W, SOURCE_H)
    p = image.getpalette()
    assert len(p) == 768
    return image.tobytes() + bytes(v for i in range(256)
                                  for v in (p[3*i+2], p[3*i+1], p[3*i], 255))


def convert(frame):
    # Preserve PAL8 until FFmpeg scales it. Converting to RGB before scaling
    # changes the filter's rounding and does not reproduce the audited frame.
    pixels = subprocess.run([
        'ffmpeg', '-v', 'error', '-f', 'rawvideo', '-pixel_format', 'pal8',
        '-video_size', f'{SOURCE_W}x{SOURCE_H}', '-i', 'pipe:0',
        '-vf', f'scale={WIDTH}:{HEIGHT}', '-frames:v', '1', '-f', 'rawvideo',
        '-pix_fmt', 'rgb24', '-'], input=frame, stdout=subprocess.PIPE, check=True).stdout
    assert len(pixels) == WIDTH * HEIGHT * 3
    rgb = Image.frombytes('RGB', (WIDTH, HEIGHT), pixels)
    indexed = rgb.quantize(colors=COLORS, method=Image.Quantize.MEDIANCUT,
                           dither=Image.Dither.NONE)
    indices = indexed.tobytes()
    assert len(indices) == WIDTH * HEIGHT and max(indices) < COLORS
    packed = bytes(a | (b << 4) for a, b in zip(indices[::2], indices[1::2]))
    p = indexed.getpalette()
    colors = tuple((p[3*i] >> 3) | (p[3*i+1] >> 3) << 5 | (p[3*i+2] >> 3) << 10
                   for i in range(COLORS))
    return rgb, indices, packed, colors


def validate(blob, packed, indices, colors):
    assert len(packed) == PACKED_BYTES and decompress(blob) == packed
    with tempfile.TemporaryDirectory(prefix='ending-decoder-') as temp:
        library = Path(temp) / 'lz.so'
        subprocess.run(['gcc', '-O2', '-shared', '-fPIC', str(ROOT / 'src/lz.c'),
                        '-o', str(library)], check=True)
        decoder = ctypes.CDLL(str(library))
        decoder.lz_decode.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint]
        decoder.lz_decode.restype = ctypes.c_uint
        source = ctypes.create_string_buffer(blob)
        destination = (ctypes.c_uint32 * (PACKED_BYTES // 4 + 2))()
        destination[0] = destination[-1] = 0xA5A5A5A5
        assert decoder.lz_decode(source, ctypes.byref(destination, 4), PACKED_BYTES) == PACKED_BYTES
        assert destination[0] == destination[-1] == 0xA5A5A5A5
        decoded = ctypes.string_at(ctypes.byref(destination, 4), PACKED_BYTES)
        assert decoded == packed
        before = bytes(destination)
        assert decoder.lz_decode(source, ctypes.byref(destination, 4), PACKED_BYTES - 1) == 0
        assert bytes(destination) == before
    unpacked = bytes(v for byte in decoded for v in (byte & 15, byte >> 4))
    assert unpacked == indices
    palette = list(struct.unpack('<256H', decompress((ROOT / 'generated/front_palette.lz').read_bytes())))
    font_indices = set((ROOT / 'generated/front_font.lz').read_bytes()) - {0}
    assert not font_indices.intersection(range(PALETTE_BASE, PALETTE_BASE + COLORS))
    original_text = {i: palette[i] for i in (10, 252, 253, 254, 255)}
    palette[PALETTE_BASE:PALETTE_BASE + COLORS] = colors
    assert original_text == {i: palette[i] for i in original_text}
    assert palette[BACKGROUND] == 0
    canvas = bytearray([BACKGROUND] * (240 * 160))
    canvas[TOP * WIDTH:(TOP + HEIGHT) * WIDTH] = bytes(PALETTE_BASE + v for v in unpacked)
    assert all(v == BACKGROUND for v in canvas[:TOP * WIDTH] + canvas[(TOP + HEIGHT) * WIDTH:])
    assert all(PALETTE_BASE <= v < PALETTE_BASE + COLORS
               for v in canvas[TOP * WIDTH:(TOP + HEIGHT) * WIDTH])
    assert all(palette[PALETTE_BASE + index] == colors[index] for index in indices)
    preview = Image.new('P', (240, 160))
    preview.putdata(canvas)
    preview.putpalette(bytes(v for c in palette for v in
                             ((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31,
                              ((c >> 10) & 31) * 255 // 31)))
    return preview.convert('RGB')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mpq', type=Path, nargs='?')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    source_path = SOURCE_DIR / 'Diabend-frame0275-source.png'
    if args.mpq:
        frame = extract(args.mpq)
        if args.check:
            assert source_bytes(Image.open(source_path)) == frame
    else:
        if not source_path.exists():
            parser.error('Supply DIABDAT.MPQ for the first extraction')
        frame = source_bytes(Image.open(source_path))
    assert NATIVE_SHA and sha(frame) == NATIVE_SHA
    rgb, indices, packed, colors = convert(frame)
    rgb_again, indices_again, packed_again, colors_again = convert(frame)
    assert (rgb.tobytes(), indices, packed, colors) == (rgb_again.tobytes(), indices_again, packed_again, colors_again)
    blob = compress(packed)
    palette = struct.pack('<16H', *colors)
    total = ((len(blob) + 3) & ~3) + len(palette)
    assert total == 9096, f'Audited 9096-byte budget changed: {total}'
    preview = validate(blob, packed, indices, colors)
    report = {
        'status': 'integrated source asset; no ROM generated',
        'native_movie': MOVIE, 'native_movie_bytes': 7777752, 'native_movie_sha256': MOVIE_SHA,
        'native_frame_zero_based': FRAME, 'native_frame_one_based': FRAME + 1,
        'native_frame_count': 825, 'native_fps_fraction': [50000, 3333],
        'native_frame_time_seconds': FRAME * 3333 / 50000,
        'native_dimensions': [SOURCE_W, SOURCE_H], 'native_pal8_sha256': sha(frame),
        'source_png_preserves_original_indices_and_palette': True,
        'native_decoder': 'FFmpeg smackvid, original PAL8 frame',
        'resample': 'FFmpeg PAL8 scale=240:117 to RGB24; no stretch of native aspect ratio',
        'quantization': f'Pillow {PIL_VERSION}, MEDIANCUT16, no dithering; static GBA adaptation',
        'scaled_rgb_sha256': sha(rgb.tobytes()), 'indexed_sha256': sha(indices),
        'packed_sha256': sha(packed), 'palette_rgb555_sha256': sha(palette), 'lz_sha256': sha(blob),
        'display_dimensions': [WIDTH, HEIGHT], 'mode4_offset': [0, TOP],
        'bits_per_pixel': 4, 'packing': 'even x low nibble; odd x high nibble; row-major',
        'opaque_pixels': WIDTH * HEIGHT, 'transparent_pixels': 0,
        'palette_base': PALETTE_BASE, 'palette_count': COLORS, 'background_index': BACKGROUND,
        'existing_font_palette_indices_preserved': [10, 252, 253, 254, 255],
        'decoded_bytes': PACKED_BYTES, 'lz_bytes': len(blob), 'palette_bytes': len(palette),
        'aligned_asset_bytes': total, 'extra_large_ram_bytes': 0,
        'decode_scratch': 'existing load_buffer:14040 of18688 bytes',
        'display_scratch': 'existing town_map:38400 of159744 bytes',
        'production_c_decoder_roundtrip': True, 'destination_canaries_intact': True,
        'undersized_capacity_rejected_without_write': True,
        'mode4_geometry_palette_and_opacity_verified': True,
        'resample_determinism_verified': True,
    }
    header = '\n'.join([
        '/* Native Diabend frame275; static GBA epilogue adaptation. */', '#pragma once',
        '#include <stdint.h>', '#define ENDING_STILL_WIDTH 240', '#define ENDING_STILL_HEIGHT 117',
        '#define ENDING_STILL_TOP 21', '#define ENDING_STILL_PALETTE_BASE 16',
        '#define ENDING_STILL_COLORS 16', '#define ENDING_STILL_BACKGROUND 252',
        '#define ENDING_STILL_PACKED_BYTES 14040',
        'extern const uint8_t asset_ending_still[];',
        '/* Raw little-endian RGB555 palette; historical .lz name is not LZ. */',
        'extern const uint8_t asset_ending_still_palette[];', '',
    ])
    files = {'ending_still.lz': blob, 'ending_still_palette.lz': palette,
             'ending_still.h': header.encode(),
             'ending_still-report.json': (json.dumps(report, indent=2) + '\n').encode()}
    if args.check:
        for name, data in files.items():
            assert (OUT / name).read_bytes() == data, name
        assert Image.open(OUT / 'ending_still-preview.png').convert('RGB').tobytes() == preview.tobytes()
        assert source_bytes(Image.open(source_path)) == frame
    else:
        OUT.mkdir(exist_ok=True);SOURCE_DIR.mkdir(parents=True,exist_ok=True)
        source_png(frame).save(source_path)
        for name, data in files.items():
            (OUT / name).write_bytes(data)
        preview.save(OUT / 'ending_still-preview.png')
    print(f'PASS native Diabend frame{FRAME}: {total} aligned ROM bytes; '
          f'{PACKED_BYTES} decode bytes; original provenance, C decoder and Mode4 geometry verified')


if __name__ == '__main__':
    main()
