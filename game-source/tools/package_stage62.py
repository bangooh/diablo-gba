#!/usr/bin/env python3
"""Package the validated Stage62 ROM, source, required assets and evidence.

Keeps current ROM/ELF/build report, all source assets, generated game inputs,
tools/tests/docs, and validation reports/screenshots. Omits disposable compiled
history, host executables, caches and failed-attempt dumps beside PASS reports.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CURRENT_BUILD = {'diablo-gba-assets.gba', 'diablo-gba-assets.elf',
                 'diablo-gba-assets-build-report.json'}
SOURCE_SUFFIXES = {'.c', '.h', '.inc', '.py', '.mjs', '.js', '.json',
                   '.md', '.txt', '.html', '.s', '.log'}
SKIP_DIRS = {'__pycache__', '.zig-cache', 'zig-cache', 'node_modules', '.git'}
REQUIRED = [
    'README.md', 'NPC-UI-AND-MAP-STAGE62.md', 'NPC-UI-SOURCE-STAGE62.md',
    'MINIMAP-STAGE62.md', 'src/town_dialogue.inc', 'src/shop_ui.inc',
    'src/hud_colors.inc', 'src/item_name_color.inc', 'src/automap_raster.inc',
    'generated/npc_menu_cursor.h', 'generated/stage62-release-report.json',
    'source_assets/npc_ui_stage62/PentSpn2.CEL',
    'source_assets/npc_ui_stage62/Town.PAL',
    'tools/stage62_release_check.py', 'tools/package_stage62.py',
    'build/diablo-gba-assets.gba', 'build/diablo-gba-assets.elf',
    'build/diablo-gba-assets-build-report.json',
]


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as handle:
        for block in iter(lambda: handle.read(1024*1024), b''):
            digest.update(block)
    return digest.hexdigest()


def include(file):
    rel = file.relative_to(ROOT)
    if any(part in SKIP_DIRS for part in rel.parts):
        return False
    if rel.parts[0] == 'build':
        return ((len(rel.parts) == 2 and file.name in CURRENT_BUILD)
                or file.suffix in SOURCE_SUFFIXES)
    if file.suffix in {'.o', '.pyc', '.tmp', '.a', '.so'}:
        return False
    # A successfully completed run may leave diagnostic dumps from earlier
    # failed attempts. Its PASS report/screenshots are the final evidence.
    if rel.parts[:2] == ('generated', 'stage62-validation'):
        if file.name.startswith('failure') or file.name == 'last-run.log':
            report = file.parent / 'report.json'
            if report.exists():
                result = json.loads(report.read_text())
                if result.get('result', result.get('status')) == 'PASS':
                    return False
    # Check the signature rather than treating extensionless source assets as
    # binaries. The current deliverable ARM ELF was handled above.
    with file.open('rb') as handle:
        if handle.read(4) == b'\x7fELF':
            return False
    return True


def verify_release():
    path = ROOT / 'generated/stage62-release-report.json'
    release = json.loads(path.read_text())
    assert release['result'] == 'PASS'
    for name in REQUIRED:
        assert (ROOT / name).is_file(), f'Missing required deliverable: {name}'
    for name, digest in release['build_inputs_sha256'].items():
        assert sha(ROOT / name) == digest, f'Input changed after release gate: {name}'
    for key in ('native_validation_sha256', 'baseline_validation_sha256',
                'host_validation_sha256'):
        for name, digest in release[key].items():
            assert sha(ROOT / 'generated/stage62-validation' / name) == digest, name
    for name, digest in release['asset_validation_sha256'].items():
        assert sha(ROOT / name) == digest, name
    for name, key in [('diablo-gba-assets.gba', 'rom_sha256'),
                      ('diablo-gba-assets.elf', 'elf_sha256'),
                      ('diablo-gba-assets-build-report.json', 'build_report_sha256')]:
        assert sha(ROOT / 'build' / name) == release[key], name
    return release


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--dry-run', action='store_true',
                        help='Validate release and list counts without creating a ZIP')
    args = parser.parse_args()
    output = args.output.resolve()
    assert not output.is_relative_to(ROOT), 'Write delivery archive outside the project tree'
    release = verify_release()
    files = sorted(f for f in ROOT.rglob('*') if f.is_file() and include(f))
    names = {str(f.relative_to(ROOT)) for f in files}
    assert set(REQUIRED) <= names
    assert set(release['build_inputs_sha256']) <= names
    for key in ('native_validation_sha256', 'baseline_validation_sha256', 'host_validation_sha256'):
        assert {'generated/stage62-validation/' + n for n in release[key]} <= names
    summary = {'result': 'PASS', 'file': str(output), 'files': len(files)+1,
               'uncompressed_bytes': sum(f.stat().st_size for f in files),
               'rom_sha256': release['rom_sha256'], 'dry_run': args.dry_run}
    if args.dry_run:
        print(json.dumps(summary, indent=2))
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    pending = output.with_suffix('.pending.zip')
    manifest = {
        'stage': 62, 'rom_sha256': release['rom_sha256'],
        'release_report_sha256': sha(ROOT / 'generated/stage62-release-report.json'),
        'policy': 'Current source/assets/docs/tests/evidence and current ROM/ELF/build report; '
                  'historical compiled builds, host ELF executables and caches omitted.',
        'files': {str(f.relative_to(ROOT)): {'bytes': f.stat().st_size, 'sha256': sha(f)}
                  for f in files},
    }
    with zipfile.ZipFile(pending, 'w', compression=zipfile.ZIP_DEFLATED,
                         compresslevel=6, allowZip64=True) as archive:
        for file in files:
            archive.write(file, 'dungeon-gba/' + str(file.relative_to(ROOT)))
        archive.writestr('dungeon-gba/PACKAGE-MANIFEST-STAGE62.json',
                         json.dumps(manifest, indent=2)+'\n')
    with zipfile.ZipFile(pending) as archive:
        assert archive.testzip() is None
        assert len(archive.infolist()) == len(files)+1
        for name in REQUIRED:
            assert archive.read('dungeon-gba/' + name) == (ROOT / name).read_bytes(), name
        # The complete manifest catches any source/evidence change while packing.
        for name, record in manifest['files'].items():
            assert sha(ROOT / name) == record['sha256'], f'Changed during package: {name}'
    pending.replace(output)
    summary.update(bytes=output.stat().st_size, sha256=sha(output))
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
