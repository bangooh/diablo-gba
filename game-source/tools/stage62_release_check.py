#!/usr/bin/env python3
"""Bind Stage62 source, assets, existing validation evidence and delivered ROM.

This gate reads completed reports. It does not rerun emulator/performance tests.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
VALIDATION = ROOT / 'generated/stage62-validation'
EXPECTED_CHANGED = {
    'build.py', 'src/automap_raster.inc', 'src/gba_mode0.c', 'src/hud_colors.inc',
    'src/infopanel.inc', 'src/inventory.inc', 'src/item_name_color.inc',
    'src/menu.inc', 'src/shop_ui.inc', 'src/town_dialogue.inc',
}
NATIVE = {
    'npc-ui-native/report.json': ('cases', 14),
    'npc-regression/report.json': ('npcs', 6),
    'item-colors-native/report.json': ('checks', 23),
    'hud-colors-native/report.json': ('checks', 3),
    'minimap-native/report.json': ('samples', 8),
    'minimap-pixels-native/report.json': ('cases', 7),
    'integrated-ui/report.json': ('cases', 8),
    'firebolt-native/report.json': ('cases', 3),
    'melee-native/report.json': ('cases', 6),
}
BASELINE = ['minimap-baseline/report.json', 'firebolt-baseline/report.json',
            'melee-baseline/report.json']


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as handle:
        for block in iter(lambda: handle.read(1024*1024), b''):
            digest.update(block)
    return digest.hexdigest()


def passed(report, name):
    assert report.get('result', report.get('status')) == 'PASS', f'Not PASS: {name}'


def source_hashes(report, key, name):
    values = report[key]
    assert isinstance(values, dict) and values, name
    for path, digest in values.items():
        assert sha(ROOT / path) == digest, f'Stale tested source: {name}: {path}'


def dialogue_function(source, name):
    start = re.search(r'static UI_FN [^\n]+\b' + name + r'\(', source).start()
    body = source.index('{', start)
    depth, end = 1, body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--freeze', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    rom = ROOT / 'build/diablo-gba-assets.gba'
    elf = ROOT / 'build/diablo-gba-assets.elf'
    build_report = ROOT / 'build/diablo-gba-assets-build-report.json'
    meta = json.loads(build_report.read_text())
    rom_sha, baseline_sha = sha(rom), sha(args.baseline / 'diablo-gba-assets.gba')
    assert rom_sha != baseline_sha, 'Candidate must differ from Stage61 baseline'
    assert meta['sha256'] == rom_sha
    assert meta['arm_elf'] == meta['gba_header_checksum'] == 'PASS'
    rom_data = rom.read_bytes()
    assert len(rom_data) == meta['rom_bytes'] == 33554432
    assert (sum(rom_data[160:190]) + 0x19) & 255 == 0

    frozen = json.loads(args.freeze.read_text())
    production = sorted(p for p in (ROOT / 'src').rglob('*') if p.is_file())
    production += [ROOT / 'build.py', ROOT / 'link.ld']
    asset_inputs = [p for p in (ROOT / 'generated').iterdir()
                    if p.suffix in ('.h', '.lz', '.rle', '.bin')]
    required = {str(p.relative_to(ROOT)) for p in production + asset_inputs}
    assert set(frozen) == required, {'missing': sorted(required - set(frozen)),
                                     'unexpected': sorted(set(frozen) - required)}
    for name, digest in frozen.items():
        assert sha(ROOT / name) == digest, f'Input changed after integrated build: {name}'
    changed = [str(p.relative_to(ROOT)) for p in production
               if not (args.baseline / p.relative_to(ROOT)).exists()
               or sha(p) != sha(args.baseline / p.relative_to(ROOT))]
    assert set(changed) == EXPECTED_CHANGED, changed

    # Reconcile every loaded ELF segment with final ROM bytes, excluding only
    # the standard header fields replaced by build.py after linking.
    data = elf.read_bytes()
    assert data[:7] == b'\x7fELF\x01\x01\x01'
    assert struct.unpack_from('<H', data, 18)[0] == 40
    phoff = struct.unpack_from('<I', data, 28)[0]
    phsize, phcount = struct.unpack_from('<HH', data, 42)
    payload = ram_used = fast_used = 0
    for i in range(phcount):
        kind, off, vaddr, paddr, filesz, memsz, flags, align = struct.unpack_from(
            '<8I', data, phoff + i*phsize)
        if kind != 1:
            continue
        if 0x02000000 <= vaddr < 0x02040000:
            assert vaddr + memsz <= 0x02040000
            ram_used += memsz
        if 0x03000000 <= vaddr < 0x03008000:
            assert vaddr + memsz <= 0x03006000
            fast_used += memsz
        if filesz and 0x08000000 <= paddr < 0x0a000000:
            dest = paddr - 0x08000000
            payload = max(payload, dest + filesz)
            skip = max(0, min(filesz, 192-dest))
            assert rom_data[dest+skip:dest+filesz] == data[off+skip:off+filesz]
    assert 0 < payload <= 33554432
    assert ram_used == meta['ewram_static_bytes'] <= 262144
    assert fast_used == meta['iwram_code_bytes'] <= 24576
    assert re.search(r'FAST\s*\(rwx\)\s*:\s*ORIGIN\s*=\s*0x03000000,\s*LENGTH\s*=\s*24K',
                     (ROOT / 'link.ld').read_text())
    startup = (ROOT / 'src/start.s').read_text()
    assert 'ldr sp, =0x03007d00' in startup and 'ldr sp, =0x03007fa0' in startup
    menu, link = (ROOT / 'src/menu.inc').read_text(), (ROOT / 'src/link.h').read_text()
    for text, name, value in ((menu, 'SAVE_VERSION', 127), (menu, 'SAVE_GENERATOR', 17),
                              (link, 'LINK_VERSION', 131)):
        assert int(re.search(r'\b' + name + r'\s*(?:=\s*)?(\d+)', text)[1]) == value

    native_hashes, native_counts = {}, {}
    for name, (field, minimum) in NATIVE.items():
        path = VALIDATION / name
        report = json.loads(path.read_text())
        passed(report, name)
        assert report.get('romSha256', report.get('rom_sha256')) == rom_sha, name
        assert len(report[field]) >= minimum, (name, field, len(report[field]), minimum)
        native_hashes[name] = sha(path)
        native_counts[name] = {field: len(report[field])}
    baseline_hashes = {}
    for name in BASELINE:
        path = VALIDATION / name
        report = json.loads(path.read_text())
        passed(report, name)
        assert report.get('romSha256', report.get('rom_sha256')) == baseline_sha, name
        baseline_hashes[name] = sha(path)

    comparison_path = VALIDATION / 'minimap-comparison.json'
    comparison = json.loads(comparison_path.read_text())
    passed(comparison, str(comparison_path))
    assert comparison['romSha256'] == rom_sha
    assert comparison['baselineRomSha256'] == baseline_sha
    assert comparison['nativeFixtureMatches'] is True
    assert len(comparison['measurements']) == 5
    for row in comparison['measurements']:
        if row['scene'].startswith('L1-'):
            family, field, name, fps_key, minimum_key = (
                'minimap', 'samples', row['scene'], 'fps', 'minRolling60HardwareFramesFps')
        elif row['scene'].startswith('Firebolt-'):
            family, field, name, fps_key, minimum_key = (
                'firebolt', 'cases', row['scene'].removeprefix('Firebolt-'),
                'fps', 'minimumRolling60HardwareFramesFps')
        else:
            assert row['scene'] == 'Twelve-enemy combat'
            family, field, name, fps_key, minimum_key = (
                'melee', 'cases', 'crowd12', 'rendererFps', 'minimumRolling60HardwareFramesFps')
        for variant, prefix in (('baseline', 'baseline'), ('native', 'candidate')):
            report = json.loads((VALIDATION / (family+'-'+variant) / 'report.json').read_text())
            case = next(case for case in report[field] if case['name'] == name)
            assert row[prefix+'Fps'] == case[fps_key]
            assert row[prefix+'RollingMinimumFps'] == case[minimum_key]
            if variant == 'native':
                assert row['mapCommitOverruns'] == case['mapOverruns'] == 0
        assert abs(row['deltaFps']-(row['candidateFps']-row['baselineFps'])) < 1e-10
    assert comparison['minimum24FpsGoalMet'] is (
        min(row['candidateRollingMinimumFps'] for row in comparison['measurements']) >= 24)
    native_hashes['minimap-comparison.json'] = sha(comparison_path)
    native_counts['minimap-comparison.json'] = {'measurements': len(comparison['measurements'])}

    host_hashes = {}
    for name, key in (('minimap-host/report.json', 'sourceSha256'),
                      ('hud-colors/host-report.json', 'sources'),
                      ('item-colors/fixtures.json', 'source_sha256')):
        path = VALIDATION / name
        report = json.loads(path.read_text())
        passed(report, name)
        source_hashes(report, key, name)
        host_hashes[name] = sha(path)
    minimap = json.loads((VALIDATION / 'minimap-host/report.json').read_text())
    assert minimap['scenes'] >= 545 and minimap['pixel_exact_frames'] >= 14872
    assert minimap['junction_diamonds'] == 9216
    assert minimap['negative_capacity_cases'] >= 2
    assert set(minimap['sanitizers']) == {'ASan', 'UBSan'}
    hud = json.loads((VALIDATION / 'hud-colors/host-report.json').read_text())
    assert hud['checks'] >= 1101393 and hud['scene_palettes'] == 2 and hud['colors'] == 4
    items = json.loads((VALIDATION / 'item-colors/fixtures.json').read_text())
    assert items['checks'] >= 106 and len(items['fixtures']) >= 8

    layout_path = VALIDATION / 'dialogue-layout/report.json'
    layout = json.loads(layout_path.read_text())
    passed(layout, str(layout_path))
    assert layout['messages'] == 207 and layout['stress_cases'] == 4
    assert layout['glyphs'] == 96 and layout['max_width'] == 216
    assert layout['line_pitch'] == 10 and layout['blank_rows'] == 3
    probe_path = VALIDATION / 'dialogue-layout/probe.c'
    probe = probe_path.read_text()
    dialogue = (ROOT / 'src/town_dialogue.inc').read_text()
    tested_functions = {}
    for name in ('dialogue_glyph', 'dialogue_line'):
        function = dialogue_function(dialogue, name)
        assert function in probe, f'Tested dialogue function changed: {name}'
        tested_functions[name] = hashlib.sha256(function.encode()).hexdigest()
    font = (ROOT / 'generated/town_dialogue_font.h').read_text()
    font_rows = [[int(v, 16) for v in row.split(',')]
                 for row in re.findall(r'\{((?:0x[0-9a-f]+,?)+)\}', font)]
    assert layout['font'] == font_rows
    host_hashes['dialogue-layout/report.json'] = sha(layout_path)
    host_hashes['dialogue-layout/probe.c'] = sha(probe_path)

    # Recompute only the tiny deterministic cursor asset, without writing it.
    from pack_npc_cursor_stage62 import build as cursor_build
    cursor_outputs, _, cursor = cursor_build()
    asset_hashes = {}
    for name, expected_bytes in cursor_outputs.items():
        path = ROOT / 'generated' / name
        assert path.read_bytes() == expected_bytes, f'Stale cursor: {name}'
        asset_hashes[str(path.relative_to(ROOT))] = sha(path)
    assert cursor['packed']['rom_bytes'] == 512
    assert cursor['packed']['persistent_ram_bytes'] == 0

    result = {
        'result': 'PASS', 'rom_sha256': rom_sha, 'elf_sha256': sha(elf),
        'build_report_sha256': sha(build_report), 'baseline_rom_sha256': baseline_sha,
        'changed_production_sources': sorted(changed), 'build_inputs_sha256': frozen,
        'native_validation_sha256': native_hashes, 'native_validation_counts': native_counts,
        'baseline_validation_sha256': baseline_hashes, 'host_validation_sha256': host_hashes,
        'asset_validation_sha256': asset_hashes,
        'dialogue_tested_functions_sha256': tested_functions,
        'rom_payload_bytes': payload, 'rom_free_bytes': 33554432-payload,
        'ewram_static_bytes': ram_used, 'ewram_static_free_bytes': 262144-ram_used,
        'iwram_code_bytes': fast_used, 'iwram_code_headroom_bytes': 24576-fast_used,
        'iwram_stack_reserved_bytes': 8192,
        'main_stack_top': '0x03007d00', 'irq_stack_top': '0x03007fa0',
        'save_version': 127, 'save_generator': 17, 'link_version': 131,
        'stage61_save_compatible': True, 'physical_console_tested': False,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'build_inputs_sha256'}, indent=2))


if __name__ == '__main__':
    main()
