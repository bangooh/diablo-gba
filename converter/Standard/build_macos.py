"""Rebuild the self-contained Intel + Apple Silicon macOS application with Zig 0.13.0.

Usage: python3 build_macos.py --zig /path/to/zig --package /path/to/v095.dgb
The encrypted payload restores the supplied v0.9.5 / V0.9.5 ROM.
No MPQ digest or derived key is embedded. This is not an asset re-encoder.
"""
import argparse
import hashlib
import plistlib
import shutil
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PACK_HASH = '459277218118e5a58baed1d1932772bd431d0a245942e7b059d6a572ef0351d9'

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--zig', type=Path, required=True)
    p.add_argument('--package', type=Path, required=True)
    p.add_argument('--output', type=Path, default=ROOT/'dist')
    a = p.parse_args()
    data = a.package.read_bytes()
    if len(data) != 33554512 or hashlib.sha256(data).hexdigest() != PACK_HASH:
        p.error('Expected the verified original V0.9.5 encrypted package')
    build = ROOT/'build'
    build.mkdir(exist_ok=True)
    zig = str(a.zig.resolve())
    slices = []
    for name, target in [('arm64','aarch64-macos.11.0'), ('x86_64','x86_64-macos.11.0')]:
        lib = build/f'crypto-{name}.a'
        binary = build/f'builder-{name}'
        subprocess.run([zig,'build-lib',str(ROOT/'src/crypto.zig'),'-static','-O','ReleaseSafe','-lc','-target',target,f'-femit-bin={lib}'],check=True)
        subprocess.run([zig,'cc','-target',target,'-O2','-Wall','-Wextra','-Werror','-DBUILDER_PORTABLE_CRYPTO',str(ROOT/'src/core.c'),str(ROOT/'src/macos.c'),str(lib),'-o',str(binary)],check=True)
        b = binary.read_bytes()
        magic, cpu, subtype = struct.unpack_from('<III', b)
        assert magic == 0xfeedfacf
        slices.append((cpu, subtype, b))
    # Universal Mach-O. Slice signatures are relative to their own Mach-O data.
    align = 14
    offset = 1 << align
    blob = bytearray(struct.pack('>II',0xcafebabe,len(slices)))
    entries = []
    for cpu, subtype, b in slices:
        entries.append((offset,b))
        blob.extend(struct.pack('>IIIII',cpu,subtype,offset,len(b),align))
        offset = (offset+len(b)+(1<<align)-1) & ~((1<<align)-1)
    for offset, b in entries:
        blob.extend(bytes(offset-len(blob)))
        blob.extend(b)
    app = a.output.resolve()/'Diablo GBA ROM Builder.app'
    macos = app/'Contents/MacOS'
    resources = app/'Contents/Resources'
    macos.mkdir(parents=True,exist_ok=True)
    resources.mkdir(parents=True,exist_ok=True)
    exe = macos/'builder'
    exe.write_bytes(blob)
    exe.chmod(0o755)
    (resources/'v095.dgb').write_bytes(data)
    shutil.copyfile(ROOT/'resources/Builder.applescript',resources/'Builder.applescript')
    info = {
        'CFBundleDevelopmentRegion':'en',
        'CFBundleExecutable':'builder',
        'CFBundleIdentifier':'com.bangoo.diablogba.rombuilder',
        'CFBundleName':'Diablo GBA ROM Builder',
        'CFBundleDisplayName':'Diablo GBA ROM Builder',
        'CFBundlePackageType':'APPL',
        'CFBundleShortVersionString':'0.9.5',
        'CFBundleVersion':'905.1',
        'LSMinimumSystemVersion':'11.0',
        'NSHighResolutionCapable':True,
        'NSHumanReadableCopyright':'Unofficial fan project. Special thanks to DevilutionX and team Mupung.',
    }
    (app/'Contents/Info.plist').write_bytes(plistlib.dumps(info))
    (app/'Contents/PkgInfo').write_bytes(b'APPL????')
    print(app)

if __name__ == '__main__':
    main()
