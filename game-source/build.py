#!/usr/bin/env python3
"""Compile Diablo GBA 0.9.5 with the archived production build recipe.

Never runs asset packers or writes production src/, generated/, or build/.
Unchanged generated inputs are read directly, and assets.s is emitted privately.
For controls, copy src/ into a private source root, symlink generated/, and use
--reuse-objects VERIFIED_BASELINE --recompile gba_mode0.c.
"""
import argparse
import ast
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from types import SimpleNamespace

PROJECT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', required=True)
    parser.add_argument('--output', type=Path, default=PROJECT/'build')
    parser.add_argument('--source-root', type=Path, default=PROJECT)
    parser.add_argument('--reuse-objects', type=Path)
    parser.add_argument('--recompile', action='append', default=[])
    parser.add_argument('--expected-sha256')
    parser.add_argument('--jobs', type=int, default=4)
    opts = parser.parse_args()
    output = opts.output.resolve()
    source = opts.source_root.resolve()
    from tools.monster_palette_style import verify_or_apply
    verify_or_apply(source / 'generated')
    for name, expected in json.loads((source / 'generated/native-palette-checks.json').read_text()).items():
        assert hashlib.sha256((source / 'generated' / name).read_bytes()).hexdigest() == expected, name + ': native palette correction missing or changed; review required'
    output.mkdir(parents=True, exist_ok=True)
    recipe = (PROJECT / 'build-recipe.py').read_text()
    module = ast.parse(recipe)
    assignments = {n.targets[0].id: n.value for n in module.body
                   if isinstance(n, ast.Assign) and len(n.targets) == 1
                   and isinstance(n.targets[0], ast.Name)}
    flags = ast.literal_eval(assignments['flags']) + ['-DGRAPHICS_ASSETS']
    sources = ast.literal_eval(assignments['sources'].body)
    assert all(name in sources for name in opts.recompile)
    env = os.environ.copy()
    env['ZIG_GLOBAL_CACHE_DIR'] = str(output / 'zig-global-cache')
    env['ZIG_LOCAL_CACHE_DIR'] = str(output / 'zig-local-cache')
    commands = []

    def compile_source(name):
        obj = output / (Path(name).stem + '.o')
        if opts.reuse_objects and name not in opts.recompile:
            shutil.copy2(opts.reuse_objects / obj.name, obj)
            return obj
        source_flags=flags
        if name in ('flash.c','dungeon_visual.c','world.c','shop.c','dungeon.c','worldgen/l2.c','worldgen/l3.c','worldgen/l4.c'):
            replace={'arm-freestanding-eabi':'thumb-freestanding-eabi','-marm':'-mthumb','-O2':'-Oz'}
            source_flags=[replace.get(flag,flag) for flag in flags]
        command = [opts.zig, 'cc', *source_flags,
                   *(['-Wno-logical-op-parentheses', '-Wno-switch-bool']
                     if name.startswith('worldgen/') else []),
                   '-c', str(source / 'src' / name), '-o', str(obj)]
        commands.append(command)
        subprocess.run(command, check=True, env=env)
        print('Compiled', name, flush=True)
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=opts.jobs) as pool:
        objects = list(pool.map(compile_source, sources))
    from tools.asset_bundle import emit_assets, referenced_assets
    required = referenced_assets(objects)
    asset_report = emit_assets(source / 'generated', output / 'assets.s', required)
    asset_object = output / 'assets.o'
    asset_command = [opts.zig, 'cc', *flags, '-c', str(output / 'assets.s'), '-o', str(asset_object)]
    subprocess.run(asset_command, check=True, env=env)
    commands.append(asset_command)
    objects.append(asset_object)
    elf = output / 'diablo-gba-assets.elf'
    link_command = [opts.zig, 'cc', *flags, '-nostdlib', *map(str, objects),
                    '-Wl,-T,' + str(PROJECT / 'link.ld'), '-o', str(elf)]
    subprocess.run(link_command, check=True, env=env)
    commands.append(link_command)

    # Execute the exact archived production ELF extraction and boot-header
    # recipe with only paths redirected to this isolated output directory.
    extraction = recipe[recipe.index('data=elf.read_bytes()'):]
    scope = {'elf': elf, 'build': output, 'ROOT': source,
             'args': SimpleNamespace(graphics=True,spell_lighting="off",demo_input=False),
             'required_assets': required, 'prefix': 'diablo-gba-assets',
             'json': json, 'hashlib': hashlib, 'struct': __import__('struct')}
    exec(compile(extraction, str(PROJECT / 'build.py'), 'exec'), scope)
    report = json.loads((output / 'diablo-gba-assets-build-report.json').read_text())
    provenance = {
        'method': 'Exact archived build.py flags, source/link order and ROM extraction; generated streams read without regeneration.',
        'build_recipe_sha256': hashlib.sha256(recipe.encode()).hexdigest(),
        'source_root': str(source), 'output': str(output),
        'reuse_objects': str(opts.reuse_objects) if opts.reuse_objects else None,
        'recompile': opts.recompile, 'commands': commands,
        'asset_bundle': asset_report, 'build_report': report,
        'source_sha256': {str(p.relative_to(source)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in sorted((source / 'src').rglob('*')) if p.is_file()},
        'objects_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in objects},
    }
    (output / 'private-build-provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    if opts.expected_sha256:
        assert report['sha256'] == opts.expected_sha256, report


if __name__ == '__main__':
    main()
