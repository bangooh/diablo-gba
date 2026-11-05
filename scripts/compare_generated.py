"""Compare rebuilt files with a private release, without copying release data.

Optional --objects identifies runtime blobs referenced by a verified ELF32 build.
Counts are file counts, not feature completeness or gameplay equivalence.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generated', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--objects', type=Path)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    required = set()
    if args.objects:
        sys.path.insert(0, str(ROOT / 'game-source/tools'))
        from asset_bundle import referenced_assets
        objects = [p for p in args.objects.glob('*.o') if p.name != 'assets.o']
        if not objects:
            parser.error('No object files found')
        required = referenced_assets(objects)
    rows = []
    suffixes = {'.h', '.inc', '.lz', '.rle', '.bin'}
    names = {p.name for parent in [args.generated, args.baseline] for p in parent.iterdir()
             if p.is_file() and p.suffix in suffixes}
    for name in sorted(names):
        actual, expected = args.generated / name, args.baseline / name
        row = {'file': name, 'runtime_blob': 'asset_' + Path(name).stem in required and Path(name).suffix in ('.lz', '.rle')}
        a = actual.read_bytes() if actual.exists() else None
        b = expected.read_bytes() if expected.exists() else None
        row['status'] = 'not_regenerated' if a is None else 'not_in_release' if b is None else 'exact' if a == b else 'different'
        if a is not None:
            row.update(generated_sha256=hashlib.sha256(a).hexdigest(), generated_bytes=len(a))
        if b is not None:
            row.update(release_sha256=hashlib.sha256(b).hexdigest(), release_bytes=len(b))
        rows.append(row)
    report = {'full_asset_regeneration_verified': False,
              'summary': dict(Counter(row['status'] for row in rows)),
              'runtime_blob_summary': dict(Counter(row['status'] for row in rows if row['runtime_blob'])),
              'runtime_symbol_count': len(required), 'files': rows}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'files'}, indent=2))


if __name__ == '__main__':
    main()
