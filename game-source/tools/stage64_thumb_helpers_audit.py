#!/usr/bin/env python3
"""Audit emitted ARM helper returns used by Thumb code on ARM7TDMI.

This reads the ELF load segments, never emulates or modifies the ROM. LDR/LDM
to PC and MOV PC are not interworking returns on ARMv4T. BX safely restores the
instruction set. Direct tail branches are followed to the destination helper.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('rom_base', type=Path)
    ap.add_argument('output', type=Path)
    ap.add_argument('--expect-unsafe-divmod', action='store_true',
                    help='Capture the original compiler-RT failure as a labelled negative control.')
    args = ap.parse_args()
    base = args.rom_base.resolve()
    elf, rom = base.with_suffix('.elf'), base.with_suffix('.gba')
    before = {'elfSha256': sha(elf), 'romSha256': sha(rom)}
    blob = elf.read_bytes()
    assert blob[:7] == b'\x7fELF\x01\x01\x01', 'ELF32 little endian'
    phoff = struct.unpack_from('<I', blob, 28)[0]
    phsize, phcount = struct.unpack_from('<HH', blob, 42)
    segments = []
    for n in range(phcount):
        kind, off, va, pa, size, memsize, flags, align = struct.unpack_from('<8I', blob, phoff + n * phsize)
        if kind == 1:
            segments.append((va, size, off))

    def read(address, size):
        for va, length, off in segments:
            if va <= address and address + size <= va + length:
                return blob[off + address - va:off + address - va + size]
        raise AssertionError(f'No initialized ELF segment for {address:#x}+{size}')

    symbols = {}
    for line in subprocess.check_output(['readelf', '-sW', str(elf)], text=True).splitlines():
        f = line.split()
        if len(f) >= 8 and f[0].endswith(':') and f[3] == 'FUNC':
            symbols[f[7]] = {'address': int(f[1], 16) & ~1, 'thumb': bool(int(f[1], 16) & 1),
                             'size': int(f[2]), 'binding': f[4]}
    prefix = '__Thumbv4ABSLongBXThunk_'
    veneer_targets = sorted(n[len(prefix):] for n in symbols if n.startswith(prefix + '__'))
    required = set(veneer_targets)
    required.update(n for n in symbols if n.startswith('__aeabi_') and 'Thunk' not in n)
    required.update(n for n in ('memset', 'memcpy', 'memmove', '__divmodsi4', '__udivmodsi4') if n in symbols)
    assert {'__aeabi_uidivmod', '__aeabi_idivmod'} <= required
    functions = {}
    pending = sorted(required)
    while pending:
        name = pending.pop()
        if name in functions:
            continue
        sym = symbols[name]
        assert not sym['thumb'], f'{name}: extend audit decoder for a Thumb helper'
        assert sym['size'] % 4 == 0, name
        words = struct.unpack('<' + 'I' * (sym['size'] // 4), read(sym['address'], sym['size']))
        returns, hazards, tails = [], [], []
        for i, word in enumerate(words):
            address = sym['address'] + i * 4
            record = {'address': f'0x{address:08x}', 'word': f'0x{word:08x}'}
            if word & 0x0ffffff0 == 0x012fff10:
                returns.append({**record, 'kind': 'BX', 'register': word & 15})
            elif word & 0x0e108000 == 0x08108000:
                hazards.append({**record, 'kind': 'LDM including PC'})
            elif word & 0x0c10f000 == 0x0410f000:
                hazards.append({**record, 'kind': 'LDR PC'})
            elif word & 0x0ffffff0 == 0x01a0f000:
                hazards.append({**record, 'kind': 'MOV PC'})
            elif word & 0x0f000000 == 0x0a000000:
                delta = word & 0xffffff
                if delta & 0x800000:
                    delta -= 0x1000000
                target = address + 8 + (delta << 2)
                if not sym['address'] <= target < sym['address'] + sym['size']:
                    names = sorted(n for n, s in symbols.items() if s['address'] == target)
                    assert names, f'Unresolved tail branch at {address:#x} to {target:#x}'
                    chosen = next((n for n in names if not n.startswith('__') or n.startswith('__aeabi_')), names[0])
                    tails.append({**record, 'targetAddress': f'0x{target:08x}', 'targetName': chosen})
                    pending.append(chosen)
        assert returns or tails or hazards, f'{name}: no recognized emitted return/tail path'
        functions[name] = {**sym, 'address': f"0x{sym['address']:08x}", 'safeInterworkingBranches': returns,
                           'nonInterworkingPcWrites': hazards, 'tailBranches': tails}
    unsafe = sorted(name for name, result in functions.items() if result['nonInterworkingPcWrites'])
    expected = ['__aeabi_idivmod', '__aeabi_uidivmod'] if args.expect_unsafe_divmod else []
    matched = unsafe == expected
    checks = []
    if not args.expect_unsafe_divmod:
        for name, helper in [('__aeabi_uidivmod', '__udivmodsi4'), ('__aeabi_idivmod', '__divmodsi4')]:
            sym = symbols[name]
            assert sym['binding'] == 'GLOBAL', name + ' overrides weak compiler-RT wrapper'
            assert 0x08000000 <= sym['address'] < 0x0a000000, name + ' remains in ROM'
            assert sym['size'] == 32, name + ' ARMv4T wrapper size'
            words = struct.unpack('<8I', read(sym['address'], 32))
            assert words[:3] == (0xe52de004, 0xe24dd004, 0xe1a0200d)
            assert words[4:] == (0xe59d1000, 0xe28dd004, 0xe49de004, 0xe12fff1e)
            assert words[3] & 0xff000000 == 0xeb000000
            delta = words[3] & 0xffffff
            if delta & 0x800000:
                delta -= 0x1000000
            target = sym['address'] + 12 + 8 + (delta << 2)
            target_names = sorted(n for n, s in symbols.items() if s['address'] == target)
            # The linker may insert an ARM-to-ARM long veneer for the IWRAM helper.
            assert any(n == helper or n.endswith('Thunk_' + helper) for n in target_names), (name, target_names)
            checks.append({'name': name, 'result': 'PASS', 'strongRomWrapper': True,
                           'address': f"0x{sym['address']:08x}", 'bytes': 32,
                           'rawWords': [f'0x{w:08x}' for w in words],
                           'divisionHelper': helper, 'callTargetSymbols': target_names,
                           'quotientRegister': 'r0', 'remainderRegister': 'r1',
                           'stackAlignmentAtCall': 8, 'returnInstruction': 'BX lr'})
    checks.append({'name': 'all-emitted-helper-return-paths', 'result': 'PASS' if matched else 'FAIL',
                   'unsafeHelpers': unsafe, 'expectedUnsafeHelpers': expected})
    assert before == {'elfSha256': sha(elf), 'romSha256': sha(rom)}, 'ROM/ELF changed during audit'
    report = {
        'result': 'PASS' if matched else 'FAIL',
        'control': 'known-unsafe-before-fix' if args.expect_unsafe_divmod else 'final-safe-helpers',
        **before,
        'physicalGba': False,
        'method': 'Read actual ELF load segments and decode emitted ARM BX returns, noninterworking LDR/LDM/MOV writes to PC, and direct tail branches. Follow helper tail branches. Include every Thumb-to-compiler-RT veneer target and all emitted AEABI helpers.',
        'architecture': 'ARM7TDMI / ARMv4T',
        'thumbVeneerTargets': veneer_targets,
        'auditedHelperCount': len(functions),
        'unsafeHelpers': unsafe,
        'expectedUnsafeHelpers': expected,
        'checks': checks,
        'functions': dict(sorted(functions.items())),
        'limits': ['Static emitted-instruction audit; does not replace native gameplay integration.',
                   'Limited ARM helper decoder; an unrecognized return shape fails validation for review.'],
        'toolSha256': sha(Path(__file__)),
    }
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ('result', 'control', 'auditedHelperCount', 'unsafeHelpers', 'romSha256')}))
    assert matched, f'Unexpected ARMv4T-unsafe helpers: {unsafe}'


if __name__ == '__main__':
    main()
