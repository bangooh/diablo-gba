"""Package both applications into one ZIP while retaining Mac execute bits."""
import argparse
import hashlib
import json
import plistlib
import struct
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dist', type=Path, required=True)
    args = parser.parse_args()
    dist = args.dist.resolve()
    report = json.loads((dist / 'BUILD.json').read_text())
    version = report['version']
    exe = dist / f'DiabloGbaRomBuilder-{version}.exe'
    app = dist / 'macOS' / 'Diablo GBA ROM Builder.app'
    info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
    assert info['CFBundleShortVersionString'] == version
    binary = app / 'Contents/MacOS/builder'
    raw = binary.read_bytes()
    assert struct.unpack_from('>II', raw) == (0xcafebabe, 2)
    assert {struct.unpack_from('>I', raw, 8 + i * 20)[0] for i in range(2)} == {0x1000007, 0x100000c}
    assert binary.stat().st_mode & 0o111
    assert exe.stat().st_size == 48 * 1024 * 1024
    assert hashlib.sha256(exe.read_bytes()).hexdigest() == report['windows_sha256']
    files = [(exe, 'Windows/' + exe.name)]
    files += [(p, 'macOS/' + str(p.relative_to(app.parent))) for p in sorted(app.rglob('*')) if p.is_file()]
    files += [(ROOT / 'resources' / n, n) for n in ('README-KO.txt', 'README-EN.txt')]
    files.append((dist / 'BUILD.json', 'BUILD.json'))
    prefix = f'DiabloGbaRomBuilder-{version}/'
    destination = dist / f'DiabloGbaRomBuilder-{version}-Windows-macOS.zip'
    with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path, name in files:
            archive.write(path, prefix + name)
    with zipfile.ZipFile(destination) as archive:
        assert archive.testzip() is None
        for path, name in files:
            assert hashlib.sha256(archive.read(prefix + name)).digest() == hashlib.sha256(path.read_bytes()).digest()
        assert (archive.getinfo(prefix + 'macOS/Diablo GBA ROM Builder.app/Contents/MacOS/builder').external_attr >> 16) & 0o111
    print(json.dumps({'zip': str(destination), 'bytes': destination.stat().st_size,
                      'sha256': hashlib.sha256(destination.read_bytes()).hexdigest(),
                      'files': len(files), 'mac_executable_permission': 'PASS'}))


if __name__ == '__main__':
    main()
