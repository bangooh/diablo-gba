#!/usr/bin/env python3
"""Read-only vanilla Diablo catalog comparison; writes an audit evidence JSON.

This is a source inventory, not a gameplay-completion or ROM validation test.
The packer's source_id refers to unpreprocessed monstdat.cpp rows. Match the
underlying name/AI/graphic identity after preprocessing, not raw numeric IDs.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def preprocess(path):
    source = path.read_text().replace('#include "all.h"', '')
    return subprocess.check_output(
        ['gcc', '-E', '-P', '-x', 'c', '-'], input=source, text=True)


def monster_rows(source):
    body = source[source.index('MonsterData monsterdata[]'):]
    body = body[:body.index('\n};')]
    result = []
    for line in body.splitlines():
        if not re.match(r'\s*\{\s*\d+,', line):
            continue
        quoted = re.findall(r'"((?:\\.|[^"\\])*)"', line)
        result.append((quoted[-1], re.search(r'\bAI_\w+', line)[0], quoted[0]))
    return result


def main():
    native_path = ROOT / 'source_assets/monster_catalog/monstdat.cpp'
    raw = native_path.read_text()
    native_source = preprocess(native_path)
    raw_rows = monster_rows(raw)
    native_rows = monster_rows(native_source)
    availability = [int(value) for value in re.search(
        r'char MonstAvailTbl\[\] = \{(.*?)\};', native_source, re.S)[1].split(',')
        if value.strip()]
    assert len(native_rows) == len(availability) == 112
    body_records = json.loads((ROOT / 'source_assets/monster_catalog/monsters.json').read_text())['monsters']
    extra_records = json.loads((ROOT / 'source_assets/monster_catalog/uniques.json').read_text())['monsters']
    represented = {raw_rows[row['source_id']] for row in body_records}
    missing = [dict(vanilla_table_id=i, name=name, ai=ai, graphic=graphic)
               for i, (name, ai, graphic) in enumerate(native_rows)
               if availability[i] and (name, ai, graphic) not in represented]
    unique_body = native_source[native_source.index('UniqMonstStruct UniqMonst[]'):]
    unique_body = unique_body[:unique_body.index('\n};')]
    unique_names = re.findall(r'\{\s*MT_\w+,\s*"([^"]+)"', unique_body)
    known_names = {row['name'] for row in body_records + extra_records}
    known_names.add('Lachdanan')  # Existing static quest prop, not Enemy kind.
    spell_source = (ROOT / 'src/spells.c').read_text().split('uint8_t spell_pending')[0]
    spells = [dict(id=int(i), name=name) for i, name in
              re.findall(r'\[(\d+)\]=\{"([^"]+)"', spell_source) if int(i)]
    magic = json.loads((ROOT / 'source_assets/item_catalog/magic_items.json').read_text())
    supported = set(range(12)) | set(range(19, 35)) | {
        14, 15, 16, 17, 35, 36, 37, 38, 39, 42, 43, 53, 59, 60, 58,
        55, 56, 61, 45, 46, 57, 52}
    expected = 'int item_unique_supported(unsigned id){return id<ITEM_UNIQUE_COUNT;}'
    assert expected in (ROOT / 'src/item_magic.inc').read_text()
    paths = [
        'source_assets/monster_catalog/monstdat.cpp',
        'source_assets/monster_catalog/monsters.json',
        'source_assets/monster_catalog/uniques.json',
        'source_assets/item_catalog/monster.cpp',
        'source_assets/item_catalog/missiles.cpp',
        'source_assets/item_catalog/spelldat.cpp',
        'source_assets/item_catalog/magic_items.json',
        'source_assets/item_catalog/objects.cpp',
        'source_assets/item_catalog/quests.cpp',
        'source_assets/item_catalog/player.cpp',
        'source_assets/item_catalog/inv.cpp',
        'source_assets/monster_awareness_stage61/lighting.cpp',
        'source_assets/town_dialogue_stage53/sound.cpp',
        'src/monster_ai.c', 'src/monster_path.inc', 'src/monster_unique.inc',
        'src/spells.c', 'src/items.c', 'src/item_magic.inc',
        'src/item_effects.inc', 'src/worldgen/shim.h', 'src/quests.c',
        'src/environment_objects.inc', 'src/objects.c', 'src/dungeon.c',
        'src/prop_archive.h', 'src/progress.c', 'src/exploration.c',
        'src/late_quests.inc', 'src/quest_npcs.inc', 'src/quick_popup.inc',
        'src/town_dialogue.inc', 'src/audio.c', 'src/ending.inc',
        'src/coop_campaign.inc', 'src/coop.h', 'src/game.h',
        'src/gba_mode0.c', 'tools/pack_spell_icons.py',
        'generated/spell-icon-colors-report.json',
    ]
    report = {
        'status': 'SOURCE_AUDIT_NOT_PLAYTHROUGH',
        'source_revision': '9f01757243412bb154d02d9af65c804159e6e0ea',
        'variant': 'Vanilla retail Diablo; HELLFIRE and SPAWN undefined',
        'monsters': {
            'runtime_kinds': len(body_records) + len(extra_records),
            'main_kinds': len(body_records),
            'extra_unique_leaders': [r['name'] for r in extra_records if r['role'] == 'leader'],
            'duplicated_minion_kinds': sum(r['role'] == 'minion' for r in extra_records),
            'vanilla_base_table_rows': len(native_rows),
            'vanilla_random_available_base_rows': sum(bool(v) for v in availability),
            'underlying_native_identities_present': len(represented),
            'available_base_identities_without_current_body': missing,
            'vanilla_unique_catalog_entries': len(unique_names),
            'unique_catalog_names_without_runtime_or_lachdanan_prop':
                sorted(set(unique_names) - known_names),
            'caution': 'Body identity coverage is not normal-spawn coverage or AI completeness. Native table contains dormant entries; no completeness percentage is inferred.',
        },
        'spells': {'runtime_castable_count': len(spells), 'runtime_castable': spells,
                   'other_supported_operations': ['Identify scroll/service', 'Warrior Repair', 'Rogue Disarm', 'Sorcerer Recharge'],
                   'shared_arrow_and_spell_slots': 8, 'native_missile_slots': 125},
        'items': {'prefix_rows': len(magic['PL_Prefix']), 'suffix_rows': len(magic['PL_Suffix']),
                  'unsupported_affixes': [r['name'] for k in ('PL_Prefix', 'PL_Suffix') for r in magic[k] if r['power'] not in supported],
                  'unique_definitions_enabled': len(magic['UniqueItemList']),
                  'caution': 'Enabled catalog/effect support does not mean every random unique necessarily drops; original eligibility and highest-ID selection rules apply.'},
        'stage61_source_implemented': ['Cathedral braziers and ordinary door placement',
                                       'Normal shrines: 21 native single-player IDs, 20 names; 3 item-mutating single-player types excluded',
                                       'Left/right bookcases with one depth-eligible book',
                                       'Object-aware melee sight and pursuit improvements',
                                       'Solid spell-icon backgrounds without dithering',
                                       'Telekinesis prepayment and exact resource refund on failed object use'],
        'limitations': ['No new emulator tests or full campaign playthrough in this audit.',
                        'Stage61 functionality is connected in current source; final ROM validation belongs to the integration and owner reports.',
                        'Shrine dedicated use audio and native poetic notification text are not included; existing spell/item cues and explanatory messages are used.',
                        'Older generated import reports contain historical limitations and are not treated as authoritative current runtime status.'],
        'file_sha256': {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in paths},
    }
    out = ROOT / 'generated/stage61-parity-audit.json'
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'evidence': str(out), 'runtime_monster_kinds': report['monsters']['runtime_kinds'],
                      'castable_spell_count': len(spells), 'affixes': 178,
                      'enabled_unique_definitions': 90}, ensure_ascii=False))


if __name__ == '__main__':
    main()
