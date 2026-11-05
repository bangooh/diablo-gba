"""Offline Sorcerer-ending size/quality experiment; never builds a ROM.

First run: --smk /path/to/DiabVic1.smk. Later runs reuse the preserved native
5FPS grayscale frames. Compare16/4 orange shades at exact-aspect240x117 and
160x78. Independent Nintendo-LZ blocks are compared with key/XOR-delta blocks.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess
import concurrent.futures

import numpy as np
from PIL import Image, ImageDraw

from lz77 import compress, decompress, indexed_chunks

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'source_assets/stage49-sorcerer'
OUT = ROOT / 'generated/stage49-sorcerer'
FPS = 5


def digest(data):
    return hashlib.sha256(data).hexdigest()


def source_frames(smk):
    SOURCE.mkdir(parents=True, exist_ok=True)
    cache = SOURCE / 'native-gray-5fps.npz'
    metadata = SOURCE / 'source.json'
    if smk:
        probe = json.loads(subprocess.check_output([
            'ffprobe', '-v', 'error', '-show_streams', '-show_format', '-of', 'json', str(smk)]))
        video = next(s for s in probe['streams'] if s['codec_type'] == 'video')
        width, height = video['width'], video['height']
        raw = subprocess.check_output([
            'ffmpeg', '-v', 'error', '-i', str(smk), '-an', '-vf', 'fps=5,format=gray',
            '-f', 'rawvideo', '-pix_fmt', 'gray', '-'])
        assert len(raw) % (width*height) == 0
        frames = np.frombuffer(raw, dtype=np.uint8).reshape(-1, height, width)
        np.savez_compressed(cache, frames=frames)
        info = {'mpq_path': 'gendata\\DiabVic1.smk', 'class': 'Sorcerer; used for all classes',
                'movie_bytes': smk.stat().st_size, 'movie_sha256': digest(smk.read_bytes()),
                'native_video': video, 'output_fps': FPS, 'output_frame_count': len(frames),
                'output_duration_seconds': len(frames)/FPS,
                'native_gray_sha256': digest(raw),
                'conversion': 'FFmpeg fps=5,format=gray; no source frame interpolation'}
        metadata.write_text(json.dumps(info, indent=2) + '\n')
    else:
        assert cache.exists(), 'First run needs --smk /path/to/DiabVic1.smk'
        frames = np.load(cache)['frames']
        info = json.loads(metadata.read_text())
        assert digest(frames.tobytes()) == info['native_gray_sha256']
    return frames, info


def compress_pair(pair):
    raw, delta = pair
    a, b = compress(raw), compress(delta)
    assert decompress(a) == raw and decompress(b) == delta
    return a, b


def delta_container(packed, compressed, width, height, bits):
    # ED49:16-byte header, N uint32 refs, payload. Bit31 means XOR with the
    # previous packed frame; zero ref means unchanged. Force a key every5s.
    base = 16 + 4*len(packed)
    payload, refs, seen = bytearray(), [], {}
    key_count = delta_count = repeated = 0
    last = bytes(len(packed[0]))
    for n, (raw, (key, delta)) in enumerate(zip(packed, compressed)):
        if n and raw == last:
            refs.append(0)
            repeated += 1
            continue
        use_delta = bool(n % 25 and len(delta) < len(key))
        block = delta if use_delta else key
        if block not in seen:
            seen[block] = base + len(payload)
            payload.extend(block)
        refs.append(seen[block] | (0x80000000 if use_delta else 0))
        delta_count += use_delta
        key_count += not use_delta
        last = raw
    header = struct.pack('<4sHHBBHI', b'ED49', width, height, bits, FPS, len(packed), len(packed[0]))
    blob = header + struct.pack('<'+'I'*len(refs), *refs) + payload
    previous = bytes(len(packed[0]))
    for n, ref in enumerate(refs):
        if ref:
            block = decompress(blob[ref & 0x7fffffff:])
            previous = bytes(a ^ b for a, b in zip(previous, block)) if ref >> 31 else block
        assert previous == packed[n], ('delta roundtrip', n)
    return blob, {'key_frames': key_count, 'xor_delta_frames': delta_count,
                  'repeat_frames': repeated, 'unique_blocks': len(seen)}


def orange_palette(bits):
    count = 1 << bits
    colors = []
    for i in range(count):
        red = (i*31+(count-1)//2)//(count-1)
        green, blue = (red*20+15)//31, (red*5+15)//31
        colors.append(red | (green << 5) | (blue << 10))
    palette = [((c & 31)*255//31, ((c >> 5) & 31)*255//31, ((c >> 10) & 31)*255//31)
               for c in colors]
    return colors, palette


def preview_frame(indices, palette):
    image = Image.fromarray(indices, 'P')
    flat = [v for color in palette for v in color]
    image.putpalette(flat + [0]*(768-len(flat)))
    #160x78 is enlarged by exactly1.5; both candidates keep native320:156 aspect.
    image = image.resize((240, 117), Image.Resampling.NEAREST).convert('RGB')
    canvas = Image.new('RGB', (240, 160))
    canvas.paste(image, (0, 21))
    return canvas


def candidate(frames, width, height, bits, workers):
    name = f'{width}x{height}-{bits}bpp'
    levels = (1 << bits)-1
    grey = np.stack([np.asarray(Image.fromarray(f, 'L').resize((width, height), Image.Resampling.LANCZOS))
                     for f in frames])
    indices = ((grey.astype(np.uint16)*levels+127)//255).astype(np.uint8)
    flat = indices.reshape(len(indices), -1)
    per_byte = 8//bits
    packed_array = np.zeros((len(indices), flat.shape[1]//per_byte), dtype=np.uint8)
    for i in range(per_byte):
        packed_array |= flat[:, i::per_byte] << (i*bits)
    packed = [row.tobytes() for row in packed_array]
    previous = np.concatenate([np.zeros_like(packed_array[:1]), packed_array[:-1]])
    deltas = [row.tobytes() for row in np.bitwise_xor(previous, packed_array)]
    with concurrent.futures.ProcessPoolExecutor(max_workers=workers) as pool:
        compressed = list(pool.map(compress_pair, zip(packed, deltas), chunksize=4))
    independent = indexed_chunks([pair[0] for pair in compressed])
    delta, delta_info = delta_container(packed, compressed, width, height, bits)
    colors, palette = orange_palette(bits)
    palette_blob = struct.pack('<'+'H'*len(colors), *colors)
    (OUT / (name+'-independent.lz')).write_bytes(independent)
    (OUT / (name+'-delta.bin')).write_bytes(delta)
    (OUT / (name+'-palette.bin')).write_bytes(palette_blob)
    points = [min(len(indices)-1, round((len(indices)-1)*f)) for f in (.1, .3, .5, .7, .9)]
    contact = Image.new('RGB', (240*len(points), 184), (14, 14, 17))
    draw = ImageDraw.Draw(contact)
    draw.text((8, 5), f'{name} |5FPS |fixed orange palette |independent {len(independent):,}B |delta {len(delta):,}B', fill='white')
    for column, n in enumerate(points):
        image = preview_frame(indices[n], palette)
        contact.paste(image, (column*240, 24))
        draw.text((column*240+8, 26), f'{n/FPS:.1f}s', fill='white')
    contact.save(OUT / (name+'-contact.png'))
    return {'name': name, 'width': width, 'height': height, 'bits_per_pixel': bits,
            'shades': len(colors), 'fps': FPS, 'frames': len(packed),
            'seconds': len(packed)/FPS, 'raw_packed_bytes': len(packed)*len(packed[0]),
            'packed_frame_bytes': len(packed[0]), 'independent_lz_bytes': len(independent),
            'delta_lz_bytes': len(delta), 'palette_bytes': len(palette_blob),
            'independent_bytes_per_second': len(independent)*FPS/len(packed),
            'delta_bytes_per_second': len(delta)*FPS/len(packed),
            'delta_frame_statistics': delta_info,
            'all_blocks_and_all_delta_frames_roundtrip': True,
            'packed_frames_sha256': digest(packed_array.tobytes())}, indices, palette


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--smk', type=Path)
    parser.add_argument('--workers', type=int, default=4)
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    frames, info = source_frames(args.smk)
    assert tuple(frames.shape[1:]) == (156, 320), frames.shape
    report = {'status': 'offline prototype only; no production source/ROM build',
              'source': info, 'candidates': []}
    for width, height, bits in [(240, 117, 4), (240, 117, 2), (160, 78, 4), (160, 78, 2)]:
        result, indices, palette = candidate(frames, width, height, bits, args.workers)
        report['candidates'].append(result)
        (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(result), flush=True)
        if width == 240 and bits == 4:
            command = ['ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo', '-pixel_format', 'rgb24',
                       '-video_size', '240x160', '-framerate', str(FPS), '-i', '-',
                       '-an', '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '18',
                       str(OUT / 'sorcerer-5fps-orange-preview.mp4')]
            process = subprocess.Popen(command, stdin=subprocess.PIPE)
            for frame in indices:
                process.stdin.write(preview_frame(frame, palette).tobytes())
            process.stdin.close()
            assert process.wait() == 0
    contacts = [Image.open(OUT / (c['name']+'-contact.png')) for c in report['candidates']]
    combined = Image.new('RGB', (1200, 184*len(contacts)))
    for i, contact in enumerate(contacts):
        combined.paste(contact, (0, i*184))
    combined.save(OUT / 'comparison.png')
    print('PASS: all4 candidates independently roundtrip; source/preview provenance recorded', flush=True)


if __name__ == '__main__':
    main()
