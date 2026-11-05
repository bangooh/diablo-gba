"""Restore reviewed native input aliases from MPQ with per-file hash checks."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mpq', type=Path, required=True)
    p.add_argument('--project', type=Path, required=True)
    args = p.parse_args()
    sys.path.insert(0, str(args.project / 'tools'))
    import mpyq
    from extract_assets import read_mpq
    rows = json.loads((ROOT / 'docs/native-input-map.json').read_text())
    archive = mpyq.MPQArchive(str(args.mpq), listfile=False)
    cache = {}
    try:
        for row in rows:
            local = Path(row['local'])
            if local.is_absolute() or '..' in local.parts:
                raise ValueError('Unsafe native input path')
            name = row['mpq']
            if name not in cache:
                cache[name] = read_mpq(archive, name)
            data = cache[name]
            if hashlib.sha256(data).hexdigest() != row['sha256']:
                raise ValueError('Native source differs: ' + name)
            target = args.project / 'source_assets' / local
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    finally:
        archive.file.close()
    print('PASS:', len(rows), 'native input paths;', len(cache), 'unique MPQ entries')


if __name__ == '__main__':
    main()
