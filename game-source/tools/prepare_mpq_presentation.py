"""Extract native ending movies and approved replacement epilogue stills."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import mpyq
from extract_assets import read_mpq

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mpq', type=Path, required=True)
    args = p.parse_args()
    out = ROOT / 'source_assets/ending'
    frames = out / 'common-stage49'
    frames.mkdir(parents=True, exist_ok=True)
    archive = mpyq.MPQArchive(str(args.mpq), listfile=False)
    movies = []
    try:
        for name in ('Diabend.smk', 'DiabVic1.smk'):
            data = read_mpq(archive, 'gendata\\' + name)
            target = out / name
            target.write_bytes(data)
            movies.append(dict(file=name, sha256=hashlib.sha256(data).hexdigest()))
    finally:
        archive.file.close()
    rows = []
    for index, frame in enumerate((189, 147, 824), 1):
        target = frames / f'frame{index}.png'
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(out / 'Diabend.smk'),
                        '-vf', f'select=eq(n\\,{frame})', '-frames:v', '1', str(target)], check=True)
        if not target.is_file():
            raise RuntimeError(f'Missing ending frame {frame}')
        rows.append(dict(file=str(target.relative_to(ROOT)), source_frame=frame,
                         sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
    report = dict(movies=movies, frames=rows, frame_numbering='zero-based',
                  note='Native movie replacements; not pixel-identical to earlier supplied stills.')
    (out / 'mpq-presentation-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: two ending movies and three native stills')


if __name__ == '__main__':
    main()
