#!/usr/bin/env python3
"""Compare complete deterministic host traces from baseline/candidate sources."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT.parents[1] / 'work/stage56/baseline/src'
NAMES = 'quests shop items game progress coop potions spells arrows objects controller exploration flash'.split()
PRIVATE = {
    'spells': 'spell_pending spell_casts spell_failures spell_audio_event spell_rng missiles shield cast_source source_kind effect_mask source_seed cast_level cast_cost portal'.split(),
    'arrows': 'game_arrows bow_shots bow_hits bow_blocked bow_misses aim bow_rng'.split(),
    'coop': ['ai_player'],
    'exploration': 'explored revision last_x last_y last_area last_radius'.split(),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def sources_hash(source):
    files = sorted(p for p in source.rglob('*') if p.is_file())
    return sha(b''.join(str(p.relative_to(source)).encode() + b'\0' + p.read_bytes() for p in files))


def build_run(source, directory):
    assert source.is_dir(), source
    directory.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, directory / 'src', dirs_exist_ok=True)
    generated = directory / 'generated'
    if not generated.exists():
        generated.symlink_to(ROOT / 'generated', target_is_directory=True)
    (directory / 'tools').mkdir(exist_ok=True)
    test = directory / 'tools/stage56_monster_equivalence.c'
    shutil.copyfile(ROOT / 'tools/stage56_monster_equivalence.c', test)
    # Relative ../src fixture includes now resolve to this immutable test copy.
    shutil.copyfile(ROOT / 'tests/item-fixture.inc', directory / 'tools/item-fixture.inc')
    (directory / 'region-production.inc').write_text(
        (directory / 'src/menu.inc').read_text().split('static UI_FN void menu_clear(void)')[0])
    units = []
    for name in NAMES:
        if name not in PRIVATE:
            units.append(directory / f'src/{name}.c')
            continue
        wrapper = directory / f'{name}-snapshot.c'
        wrapper.write_text('#include <stddef.h>\n#include "src/' + name + '.c"\n'
                           'extern void stage56_emit(const char *,const void *,size_t);\n'
                           'void stage56_' + name + '(void){\n' +
                           ''.join(f' stage56_emit("{name}.{field}",(const void*)&{field},sizeof {field});\n'
                                   for field in PRIVATE[name]) + '}\n')
        units.append(wrapper)
    executable = directory / 'equivalence'
    command = ['gcc', '-std=gnu11', '-DGRAPHICS_ASSETS', '-DFLASH_HOST_TEST', '-O1', '-g',
               '-ftrivial-auto-var-init=zero', '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
               '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
               '-I' + str(directory), '-I' + str(directory / 'src'),
               str(test), *map(str, units), '-o', str(executable)]
    compiled = subprocess.run(command, text=True, capture_output=True)
    (directory / 'compile.log').write_text(compiled.stdout + compiled.stderr)
    if compiled.returncode:
        print(compiled.stderr)
        compiled.check_returncode()
    run = subprocess.run([str(executable), str(directory / 'trace.bin')], text=True,
                         capture_output=True, timeout=120,
                         env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0',
                              'UBSAN_OPTIONS': 'halt_on_error=1'})
    (directory / 'run.log').write_text(run.stdout + run.stderr)
    if run.returncode:
        print(run.stdout + run.stderr)
        run.check_returncode()
    return {'source_sha256': sources_hash(directory / 'src'),
            'monster_ai_sha256': sha((directory / 'src/monster_ai.c').read_bytes()),
            'run': json.loads(run.stdout), 'trace': directory / 'trace.bin'}


def compare(left, right):
    records = size = 0
    context = None
    digest = hashlib.sha256()
    with left.open('rb') as a, right.open('rb') as b:
        while True:
            ah, bh = a.read(44), b.read(44)
            assert ah == bh, f'record layout mismatch after {records} records, context={context}'
            if not ah:
                break
            assert len(ah) == 44
            name = ah[:40].split(b'\0')[0].decode()
            length = struct.unpack('<I', ah[40:])[0]
            av, bv = a.read(length), b.read(length)
            assert len(av) == len(bv) == length
            if name == 'context':
                context = struct.unpack('<III', av)
            if av != bv:
                offset = next(i for i, (x, y) in enumerate(zip(av, bv)) if x != y)
                raise AssertionError(f'context(case,tick,phase)={context} field={name} offset={offset} baseline={av[offset]} candidate={bv[offset]}')
            digest.update(ah)
            digest.update(av)
            records += 1
            size += len(ah) + length
    return {'records_compared': records, 'trace_bytes_compared': size, 'trace_sha256': digest.hexdigest()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline', type=Path, default=BASELINE)
    parser.add_argument('--candidate', type=Path, required=True,
                        help='Complete source directory, or monster_ai.c replacement over the frozen baseline')
    parser.add_argument('--out', type=Path, default=ROOT / 'generated/stage56-validation/simulation')
    args = parser.parse_args()
    work = ROOT / 'work/stage56/monster-equivalence' / args.out.name
    report = {'status': 'RUNNING', 'baseline_source': str(args.baseline.resolve()),
              'candidate_source': str(args.candidate.resolve()),
              'harness_sha256': sha((ROOT / 'tools/stage56_monster_equivalence.c').read_bytes()),
              'sanitizers': ['address', 'undefined'],
              'method': 'Separate ASan/UBSan host builds of full production simulation with identical fixtures; exact tagged binary snapshots after every tick, with direct private AI/RNG/pack and projectile/co-op/exploration state. No ROM build.',
              'fixtures': ['dead slots with stale private bytes', 'dormant slots including Rhino charge',
                           'mixed alive0/1/2, walking reservations, doors, rooms and unique pack',
                           'two-player zero-alive world', 'two/four-player mixed AI before/after coop_ai_finish',
                           'full solo game_step', 'full two/four-player coop_sim_step'],
              'limitations': 'Display/audio/terrain generation and co-op frontend interaction use the established item fixture mocks. Saves are checked through actual archive packing; no Flash restore or native timing claim.'}
    args.out.mkdir(parents=True, exist_ok=True)
    try:
        candidate = args.candidate
        if candidate.is_file():
            replacement = candidate
            candidate = work / 'candidate-input'
            shutil.copytree(args.baseline, candidate, dirs_exist_ok=True)
            shutil.copyfile(replacement, candidate / 'monster_ai.c')
            report['replacement_sha256'] = sha(replacement.read_bytes())
        for label, source in [('baseline', args.baseline), ('candidate', candidate)]:
            print(f'Building/running {label}: {source}', flush=True)
            report[label] = build_run(source.resolve(), work / label)
        report.update(compare(report['baseline']['trace'], report['candidate']['trace']))
        report['status'] = 'PASS'
    except Exception as error:
        report['status'] = 'FAIL'
        report['error'] = str(error)
        raise
    finally:
        text = json.dumps(report, indent=2, default=str) + '\n'
        (args.out / 'monster-equivalence.json').write_text(text)
        print(text, end='')


if __name__ == '__main__':
    main()
