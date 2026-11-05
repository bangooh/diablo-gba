"""Rebuild UI panel assets from an MPQ into a new, private directory.

Default: panels only. --with-frontend adds static screens and cursor.
Does not rebuild the complete game asset set.
Requires Pillow, mpyq==0.2.5 and dclimplode==0.0.1.0.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys

from PIL import Image, __version__ as pillow_version

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / 'game-source/tools'
sys.path.insert(0, str(TOOLS))
from extract_assets import read_mpq
import mpyq


def decode(raw, width):
    start, end = struct.unpack_from('<II', raw, 4)
    data = raw[start:end]
    pixels = bytearray()
    pos = 0
    while pos < len(data):
        count = data[pos]
        pos += 1
        if count >= 128:
            pixels.extend(bytes(256 - count))
        else:
            if pos + count > len(data):
                raise ValueError('Truncated CEL literal')
            pixels.extend(data[pos:pos + count])
            pos += count
    if len(pixels) % width:
        raise ValueError('Invalid CEL dimensions')
    return Image.frombytes('P', (width, len(pixels) // width), bytes(pixels)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mpq', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--with-frontend', action='store_true', help='Also regenerate static frontend screens and cursor')
    parser.add_argument('--compare', type=Path, help='Read-only baseline generated directory')
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error('Output must be outside the public source tree')
    output.mkdir(parents=True, exist_ok=False)
    src = output / 'tools/ui_sources'
    opt = src / 'opt128'
    opt.mkdir(parents=True)
    generated = output / 'generated'
    generated.mkdir()
    archive = mpyq.MPQArchive(str(args.mpq), listfile=False)
    inputs = {}

    def read(name):
        raw = read_mpq(archive, name)
        inputs[name] = hashlib.sha256(raw).hexdigest()
        return raw

    pal = read('Levels\\TownData\\Town.PAL')
    for name, width in [('Char', 320), ('Quest', 320), ('Diabsmal', 296)]:
        im = decode(read('Data\\' + name + '.CEL'), width)
        im.putpalette(pal)
        im.save(src / (name + '.CEL.png'))
    inventory = decode(read('Data\\Inv\\Inv.CEL'), 320)
    palette = [tuple(pal[i:i + 3]) for i in range(0, 768, 3)]
    allowed = [i for i in range(1, 256) if not 80 <= i < 96]
    lut = [0] + [min(allowed, key=lambda j: sum((palette[j][k] - palette[i][k]) ** 2 for k in range(3))) for i in range(1, 256)]
    black = min(allowed, key=lambda j: sum(c * c for c in palette[j]))
    inventory = inventory.resize((120, 128), Image.Resampling.NEAREST).point([black] + lut[1:])
    canvas = Image.new('P', (128, 128))
    canvas.paste(inventory, (8, 0))
    canvas.putpalette(pal)
    canvas.save(opt / 'Inv-gba.png')
    (opt / 'town.pal').write_bytes(pal)
    (opt / 'CharBut.CEL').write_bytes(read('Data\\CharBut.CEL'))
    archive.file.close()
    for name in ['pack_ui127.py', 'pack_ui128.py', 'lz77.py', 'pack_front_screens.py', 'extract_assets.py', 'lz10_optimal.c']:
        shutil.copyfile(TOOLS / name, output / 'tools' / name)
    if args.with_frontend:
        subprocess.run([sys.executable, str(output / 'tools/pack_front_screens.py'), str(args.mpq.resolve())], check=True)
    for script, flags in [('pack_ui127.py', [] if args.with_frontend else ['--panels-only']), ('pack_ui128.py', [])]:
        subprocess.run([sys.executable, str(output / 'tools' / script), *flags], check=True)
    results = []
    for file in sorted(generated.iterdir()):
        digest = hashlib.sha256(file.read_bytes()).hexdigest()
        entry = {'file': file.name, 'bytes': file.stat().st_size, 'sha256': digest}
        if args.compare:
            baseline = args.compare / file.name
            if file.name == 'frontend_focus.h':
                import re
                extract = lambda text: re.findall(r'\d+', text.split('front_focus81[8][100]={', 1)[1].split('};', 1)[0])
                entry['baseline_match'] = extract(file.read_text()) == extract((args.compare / 'frontend_stage81.h').read_text())
                entry['comparison'] = 'focus pixel array only; credits excluded'
            else:
                entry['baseline_match'] = baseline.is_file() and file.read_bytes() == baseline.read_bytes()
        results.append(entry)
    report = {'scope': 'UI127/UI128 plus static frontend and cursor' if args.with_frontend else 'UI127 panels and UI128 frames only; excludes cursor',
              'pillow': pillow_version, 'input_sha256': inputs, 'outputs': results,
              'full_asset_regeneration_verified': False}
    (output / 'ui-regeneration-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    if args.compare and not all(row['baseline_match'] for row in results):
        sys.exit(1)


if __name__ == '__main__':
    main()
