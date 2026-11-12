"""Check generation dependencies and fingerprint a private MPQ before extraction."""
import argparse
import ctypes
import hashlib
import importlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def fingerprint(path):
    """Stream the archive: do not allocate a second ~500 MB copy in RAM."""
    sha = hashlib.sha256()
    md5 = hashlib.md5(usedforsecurity=False)
    size = 0
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            size += len(chunk)
            sha.update(chunk)
            md5.update(chunk)
    return dict(bytes=size, sha256=sha.hexdigest(), md5=md5.hexdigest())


def check_environment(zig=None):
    errors = []
    if sys.platform != 'linux':
        errors.append('Native Windows/macOS generation is not supported by this pipeline. '
                      'On Windows use WSL2 Ubuntu with Linux Python, GCC/G++, ffmpeg and Zig; '
                      'see docs/BUILDING.md.')
        return errors
    if sys.version_info < (3, 12):
        errors.append('Python 3.12 or newer is required.')
    for module in ('PIL', 'mpyq', 'dclimplode', 'numpy'):
        try:
            importlib.import_module(module)
        except Exception as exc:
            errors.append(f'Cannot import {module}: {exc}. Run '
                          'python -m pip install -r requirements-assets.txt')
    cc = os.environ.get('CC', 'cc')
    for tool in dict.fromkeys((cc, 'gcc', 'g++', 'ffmpeg')):
        if not shutil.which(tool):
            errors.append(f'Missing executable: {tool}. Install build-essential and ffmpeg '
                          '(CC must name a single executable, not a command with flags).')
            continue
        try:
            result = subprocess.run([tool, '-version' if tool == 'ffmpeg' else '--version'],
                                    capture_output=True, text=True, timeout=15)
            if result.returncode:
                errors.append(f'{tool} failed its version check: {result.stderr.strip()}')
        except (OSError, subprocess.TimeoutExpired) as exc:
            errors.append(f'Cannot run {tool}: {exc}')
    if not errors:
        # Match the frontend packer's native shared-library requirement.
        try:
            with tempfile.TemporaryDirectory() as directory:
                source = Path(directory) / 'probe.c'
                library = Path(directory) / 'probe.so'
                source.write_text('int probe(void) { return 42; }\n')
                result = subprocess.run([cc, '-O2', '-shared', '-fPIC', str(source),
                                         '-o', str(library)], capture_output=True,
                                        text=True, timeout=30)
                if result.returncode:
                    errors.append(f'Native C shared-library compilation failed: {result.stderr.strip()}')
                elif ctypes.CDLL(str(library)).probe() != 42:
                    errors.append('Native C shared-library probe returned an unexpected value.')
        except (OSError, subprocess.TimeoutExpired) as exc:
            errors.append(f'Cannot build/load a native C shared library: {exc}')
    if zig is not None:
        try:
            result = subprocess.run([str(zig.resolve()), 'version'], capture_output=True,
                                    text=True, timeout=15)
            if result.returncode or result.stdout.strip() != '0.13.0':
                errors.append('Zig must be the executable for version 0.13.0, not its folder '
                              f'(reported: {result.stdout.strip()!r}).')
        except (OSError, subprocess.TimeoutExpired) as exc:
            errors.append(f'Cannot run Zig executable {zig}: {exc}')
    return errors


def preflight(mpq, zig=None, expected_sha256=None):
    errors = check_environment(zig)
    info = None
    try:
        info = fingerprint(mpq)
        print(f'MPQ bytes: {info["bytes"]}\nMPQ MD5: {info["md5"]}\n'
              f'MPQ SHA-256: {info["sha256"]}', flush=True)
        if expected_sha256 and info['sha256'] != expected_sha256.lower():
            errors.append('MPQ SHA-256 does not match --expected-mpq-sha256.')
        if not expected_sha256:
            print('Fingerprint only: no reference digest supplied; this does not verify the MPQ.', flush=True)
    except OSError as exc:
        errors.append(f'Cannot read MPQ {mpq}: {exc}')
    if errors:
        raise SystemExit('Build preflight failed before asset generation:\n- ' + '\n- '.join(errors))
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mpq', type=Path, required=True)
    parser.add_argument('--zig', type=Path)
    parser.add_argument('--expected-mpq-sha256')
    args = parser.parse_args()
    preflight(args.mpq, args.zig, args.expected_mpq_sha256)
    print('Build preflight PASS (not a full generation or gameplay test).')


if __name__ == '__main__':
    main()
