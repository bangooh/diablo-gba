"""Clean MPQ regeneration of recovered foundation asset stages.

This is a partial pipeline, not a replacement for the private-asset ROM build.
No release assets are copied into the work directory. --compare is read-only.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mpq', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--compare', type=Path)
    p.add_argument('--actors', action='store_true', help='Also rebuild the large hero pool and cathedral monsters')
    args = p.parse_args()
    target = args.output.resolve()
    if target == ROOT or ROOT in target.parents:
        p.error('Use a new output directory outside the public source tree')
    target.mkdir(parents=True, exist_ok=False)
    game = target / 'game-source'
    game.mkdir()
    for name in ('tools', 'src', 'source_assets'):
        shutil.copytree(ROOT / 'game-source' / name, game / name,
                        ignore=shutil.ignore_patterns('__pycache__'))
    generated = game / 'generated'
    generated.mkdir()
    logs = target / 'logs'
    logs.mkdir()
    for name in ('cathedral', 'weapon_heroes', 'spells', 'frontend'):
        (game / 'source_assets' / name).mkdir(exist_ok=True)
    mpq = str(args.mpq.resolve())
    steps = []

    def command(name, argv):
        start = time.monotonic()
        with (logs / (name + '.log')).open('w') as log:
            result = subprocess.run([sys.executable, *map(str, argv)], stdout=log, stderr=subprocess.STDOUT)
        steps.append(dict(name=name, returncode=result.returncode, seconds=round(time.monotonic()-start, 2)))
        print(name, 'PASS' if result.returncode == 0 else 'FAILED', flush=True)
        if result.returncode:
            report = dict(scope='Incomplete foundation', full_asset_regeneration_verified=False,
                          mpq_bytes=args.mpq.stat().st_size, steps=steps, outputs=[])
            (target / 'regeneration-report.json').write_text(json.dumps(report, indent=2) + '\n')
            log_path = logs / (name + '.log')
            print(f'First failing stage: {name}. Full log: {log_path}', file=sys.stderr)
            print('\n'.join(log_path.read_text(errors='replace').splitlines()[-30:]), file=sys.stderr)
            print('Fix this error, then use a NEW output directory (foundation cannot resume).',
                  file=sys.stderr)
            sys.exit(1)
        return True

    def tool(name, *argv):
        return command(name, [game / 'tools' / (name + '.py'), *argv])

    ui = target / 'ui'
    if command('ui', [ROOT / 'scripts/regenerate_ui.py', '--mpq', mpq, '--output', ui, '--with-frontend']):
        shutil.copytree(ui / 'generated', generated, dirs_exist_ok=True)
    tool('extract_assets', mpq, game / 'source_assets/cathedral')
    tool('pack_gba_assets', game / 'source_assets/cathedral', generated)
    sys.path.insert(0, str(game / 'tools'))
    from lz77 import compress

    def compress_names(names):
        for name in names:
            path = generated / (name + '.bin')
            if path.exists():
                path.with_suffix('.lz').write_bytes(compress(path.read_bytes()))

    compress_names(['obj_palette', 'bg_palette'])
    tool('pack_heroes', mpq)
    # pack_heroes emits the historical three-choice layout. Restore current four.
    tool('pack_front_screens', mpq)
    tool('import_item_catalog', '--extract')
    tool('import_item_magic', '--extract')
    tool('pack_inventory', mpq)
    tool('pack_bow', mpq)
    tool('finalize_bow_audio')
    tool('pack_spell_icons', '--mpq', mpq)
    tool('extract_spell_sources', mpq)
    tool('pack_spell_geometry_stage73')
    tool('pack_town', mpq, generated)
    tool('pack_towners', mpq, generated)
    compress_names(['npc_palette', 'town_palette'])
    tool('pack_exocet_font')
    tool('pack_town_dialogue_font')
    tool('extract_control', mpq, game / 'source_assets/control')
    tool('pack_control')
    tool('pack_belt')
    tool('pack_belt129')
    tool('pack_loading', mpq)
    tool('pack_loading_town', mpq)
    tool('pack_audio134', mpq)
    tool('pack_audio_gains')
    tool('pack_item_grab_rc7', mpq)
    tool('pack_entry_cows', mpq)
    tool('stage53_dialogue_catalog')
    tool('pack_quest_prose_stage71')
    tool('pack_npc_greetings', '--mpq', mpq)
    tool('pack_death152', mpq)
    import mpyq
    from extract_assets import read_mpq
    archive = mpyq.MPQArchive(mpq, listfile=False)
    for local, source in [('DurIcons.CEL', 'Items\\DurIcons.CEL'), ('town.pal', 'Levels\\TownData\\Town.PAL')]:
        path = game / 'assets/durability' / local
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(read_mpq(archive, source))
    archive.file.close()
    tool('pack_durability')
    tool('regenerate_monster_metadata')
    if args.actors:
        if tool('pack_player_armor', mpq):
            if tool('pack_cast_frames'):
                argv = [ROOT / 'scripts/verify_actor_regeneration.py', '--project', game, '--report', target / 'hero-verification.json']
                if args.compare:
                    argv += ['--baseline', args.compare]
                command('verify_hero_pixels', argv)
                if tool('pack_cast_families_stage69', mpq):
                    argv = [ROOT / 'scripts/verify_actor_regeneration.py', '--project', game, '--lightning', '--report', target / 'lightning-verification.json']
                    if args.compare:
                        argv += ['--baseline', args.compare]
                    command('verify_lightning_pixels', argv)
        tool('pack_cathedral_monsters', mpq)
        tool('regenerate_monster_metadata')
    outputs = []
    for path in sorted(generated.iterdir()):
        if path.suffix not in ('.h', '.inc', '.lz', '.rle', '.bin'):
            continue
        row = dict(file=path.name, bytes=path.stat().st_size, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        if args.compare:
            other = args.compare / path.name
            row['comparison'] = ('not_in_baseline' if not other.exists() else
                                 'exact' if path.read_bytes() == other.read_bytes() else 'different')
        outputs.append(row)
    report = dict(scope='Recovered foundation only', full_asset_regeneration_verified=False,
                  mpq_bytes=args.mpq.stat().st_size, steps=steps, outputs=outputs)
    (target / 'regeneration-report.json').write_text(json.dumps(report, indent=2) + '\n')
    failures = [s['name'] for s in steps if s['returncode']]
    print('Pipeline failures:', failures, flush=True)
    if failures:
        sys.exit(1)


if __name__ == '__main__':
    main()
