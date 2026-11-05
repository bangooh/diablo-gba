"""Audit eight original unique speeches; select seven boss cues for production.

This emits audit data, not production trigger or audio code. The full speech
table remains a source oracle. IDs0..6 are production cues; Lachdanan7 remains
an excluded friendly-NPC reference. Diablo's bugged entry cue is documented only.
"""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

from stage53_dialogue_catalog import ROOT, SOURCE as TOWN, STRING, body, enum, preprocess, string

SOURCE = ROOT / 'source_assets/boss_speech_stage53'
SPECS = [
    ('BUTCHER', 'The Butcher', 'UMT_BUTCHER', 'MONSTER_BUTCHER', 6,
     'TEXT_BUTCH10', 'USFX_CLEAVER', 'visibility',
     'ProcessMonsters: BFLAG_VISIBLE on monster tile and _msquelch == 0.',
     'Visible tiles reset awareness to255; unseen ticks decrement it. Native can replay after awareness decays to0. No permanent native speech flag.',
     'First visible awareness in the port; preserve user-requested first-only behavior with a durable existing-save-field bit.'),
    ('KING', 'Skeleton King', 'UMT_SKELKING', 'MONSTER_KING', 33,
     'TEXT_KING11', 'USFX_SKING1', 'region entry',
     'LoadGameLevel: setlevel && setlvlnum == SL_SKELKING && Q_SKELKING is QUEST_ACTIVE, after loading and music_start.',
     'No native once flag: the active-quest tomb greeting can replay on another region load.',
     'Active Leoric quest and committed REGION_KING entry. First-only bit is an intentional scope adaptation.'),
    ('LAZARUS', 'Arch-Bishop Lazarus', 'UMT_LAZURUS', 'MONSTER_LAZARUS', 41,
     'TEXT_VILE13', 'USFX_LAZ1', 'altar/visibility',
     'MAI_Lazurus standing and visible: SP mtalkmsg VILE13 + INQUIRING + local player(35,46), then fprst3.smk and MM_TALK; MP VILE13 + INQUIRING + Betrayer qvar1<=3 triggers MM_TALK.',
     'M_DoTalk sets TALKING. SP after USFX_LAZ1 finishes sets qvar1=6, goal NORMAL, awareness255, message0. MP M_DoTalk clears message immediately and sets qvar1=6.',
     'Use the existing committed Lazarus altar/stage transition; do not add a separate first-LOS event to the SP encounter.'),
    ('WARLORD', 'Warlord of Blood', 'UMT_WARLORD', 'MONSTER_WARLORD', 48,
     'TEXT_WARLRD9', 'USFX_WARLRD1', 'visibility',
     'MAI_Warlord standing + visible + mtalkmsg WARLRD9 + INQUIRING enters MM_TALK.',
     'M_DoTalk sets quest qvar1=2. After USFX_WARLRD1 finishes, clear message, set NORMAL and awareness255.',
     'Existing quest stage[11]1->2 on party visibility, before the boss becomes attackable.'),
    ('GHARBAD', 'Gharbad the Weak', 'UMT_GARBUD', 'MONSTER_GHARBAD', 46,
     'TEXT_GARBUD1', 'USFX_GARBUD1', 'player contact',
     'Player action against talkable unique calls TalktoMonster, then M_DoTalk. Initial UniqMonst message is GARBUD1. Visibility alone does not start speech.',
     'First talk activates/logs quest. Leaving visibility while TALKING advances to the next message; later three speeches are not part of this first-speech scope.',
     'Existing successful stage[2]0->1 contact event. Repeated stage1 contact must not replay the first voice.'),
    ('ZHAR', 'Zhar the Mad', 'UMT_ZHAR', 'MONSTER_ZHAR', 47,
     'TEXT_ZHAR1', 'USFX_ZHAR1', 'player contact',
     'Player action calls TalktoMonster/M_DoTalk with initial ZHAR1. Native gives a book and sets MFLAG_QUEST_COMPLETE in M_DoTalk.',
     'Leaving visibility while TALKING changes message to ZHAR2. Hostile follow-up speech is outside first-speech scope.',
     'Existing stage[3]0->1 after book reward succeeds. A full-ground retry must not mark or play a completed first encounter.'),
    ('SNOTSPIL', 'Snotspill', 'UMT_SNOTSPIL', 'MONSTER_SNOTSPIL', 29,
     'TEXT_BANNER10', 'USFX_SNOT1', 'player contact',
     'Player action calls TalktoMonster/M_DoTalk with initial BANNER10. Native opens the first map section and sets qvar1=2 plus MFLAG_QUEST_COMPLETE.',
     'Leaving visibility changes message to BANNER11; banner turn-in changes it to BANNER12. These follow-up speeches are excluded.',
     'Port uses PROP_SNOTSPIL before hostility: successful stage[7]<2 ->2 initial request, not banner delivery or hostile-monster spawn.'),
    ('LACHDANAN', 'Lachdanan', 'UMT_LACHDAN', None, None,
     'TEXT_VEIL9', 'USFX_LACH1', 'player contact',
     'Player action calls TalktoMonster/M_DoTalk with initial VEIL9; M_DoTalk activates/logs quest.',
     'Leaving visibility changes to VEIL10; elixir hand-in changes to VEIL11 and later kills the NPC. Follow-up speeches excluded.',
     'Port uses PROP_LACHDANAN: successful stage[4]0->1 initial request, not elixir reward exchange.'),
]


def make_catalog():
    src = {name: preprocess(TOWN / name) for name in ('enums.h', 'textdat.cpp', 'effects.cpp')}
    speeches, sounds = enum(src['enums.h'], '_speech_id'), enum(src['enums.h'], '_sfx_id')
    unique_body = re.search(r'typedef enum\s*\{([^{}]*UMT_GARBUD[^{}]*)\}\s*_uniq_monsterid;', src['enums.h']).group(1)
    unique = {name: int(value, 0) for name, value in re.findall(r'(UMT_\w+)\s*=\s*(\w+)', unique_body)}
    rows = re.findall(r'\{\s*((?:' + STRING + r'\s*)+),\s*(\d+),\s*(\d+),\s*(\w+)\s*\}', body(src['textdat.cpp'], 'alltext[]'))
    effects = re.findall(r'\{\s*([^,]+),\s*(' + STRING + r'),\s*NULL\s*\}', body(src['effects.cpp'], 'sgSFX[]'))
    records = []
    for voice, spec in enumerate(SPECS):
        key, name, native, port, port_id, speech, sound, kind, condition, once, mapping = spec
        raw, scroll, speed, actual_sound = rows[speeches[speech]]
        assert actual_sound == sound, (speech, actual_sound, sound)
        raw = string(raw)
        text = raw.rstrip().removesuffix('|').rstrip()
        records.append(dict(voice_id=voice, production=voice<7, key=key, name=name, native_unique=native,
                            native_unique_id=unique[native], port_monster=port, port_monster_id=port_id,
                            speech=speech, speech_id=speeches[speech], text=text,
                            text_note='Native alltext is blank; the requested spoken cue is "Ah, fresh meat!".' if not text else 'Exact native alltext prose, renderer terminator removed.',
                            scroll_text=bool(int(scroll)), scroll_speed_index=int(speed), sfx=sound,
                            sfx_id=sounds[sound], mpq_path=string(effects[sounds[sound]][1]),
                            trigger_kind=kind, native_condition=condition, native_once_behavior=once,
                            port_transition=mapping))
    code = '#include <assert.h>\n#include <stdio.h>\n#include <string.h>\n'
    code += '\n'.join('enum ' + n + '{' + body(src['enums.h'], 'enum ' + n) + '};' for n in ('_speech_id', '_sfx_id', 'sfx_flag'))
    code += 'enum native_unique{' + unique_body + '};\n'
    code += 'typedef struct {const char*text;int scroll,speed,sfx;} Text;\n'
    code += 'static const Text native_text[]={' + body(src['textdat.cpp'], 'alltext[]') + '};\n'
    code += 'typedef struct {int flags;const char*path;void*sound;} Sfx;\n'
    code += 'static const Sfx native_sfx[]={' + body(src['effects.cpp'], 'sgSFX[]') + '};\nint main(void){\n'
    for record in records:
        ident = record['speech_id']
        code += f'assert({record["speech"]}=={ident}&&{record["sfx"]}=={record["sfx_id"]});\n'
        code += f'assert({record["native_unique"]}=={record["native_unique_id"]});\n'
        code += f'assert(native_text[{ident}].sfx=={record["sfx"]});\n'
        code += f'assert(!strcmp(native_text[{ident}].text,{json.dumps(string(rows[ident][0]))}));\n'
        code += f'assert(!strcmp(native_sfx[{record["sfx"]}].path,{json.dumps(record["mpq_path"])}));\n'
    code += 'return 0;}\n'
    with tempfile.TemporaryDirectory(prefix='boss-dialogue-check-') as tmp:
        c = Path(tmp) / 'check.c'; c.write_text(code)
        subprocess.run(['gcc', '-std=c11', '-O1', str(c), '-o', str(c.with_suffix(''))], check=True)
        subprocess.run([str(c.with_suffix(''))], check=True)
    # Assert the source anchors behind the two easily mistaken trigger choices.
    monster = (SOURCE / 'monster.cpp').read_text()
    diablo = (SOURCE / 'diablo.cpp').read_text()
    player = (SOURCE / 'player.cpp').read_text()
    assert 'dFlags[mx][my] & BFLAG_VISIBLE && Monst->_msquelch == 0' in monster
    assert 'setlevel && setlvlnum == SL_SKELKING && quests[Q_SKELKING]._qactive == QUEST_ACTIVE' in diablo
    assert 'currlevel == 16 && !plr[myplr]._pLvlVisited[15]' in player
    return dict(status='PASS native compiled enum/text/SFX mapping for eight audited records; seven production cues',
                source_commit='9f01757243412bb154d02d9af65c804159e6e0ea',
                build_flags='retail Diablo; neither SPAWN nor HELLFIRE',
                approved_voice_count=7, production_voice_ids=list(range(7)), voices=records,
                excluded=[dict(name='Lachdanan', audited_voice_id=7, reason='Friendly quest NPC and long monologue; retain as text-only under the greeting-only town/quest dialogue scope.'),
                          dict(name='Diablo', sfx='PS_DIABLVLINT', sfx_id=sounds['PS_DIABLVLINT'],
                               mpq_path=string(effects[sounds['PS_DIABLVLINT']][1]),
                               reason='No original boss-first-visibility line. The level16 SP entry cue has a documented visited[15] bug; restoring it is outside this request. DiabloD.wav is a death cue.'),
                          dict(name='Red Vex / Black Jade', reason='Share Lazarus talk sentinel for encounter gating, but MAI_Lazhelp starts no independent speech.'),
                          dict(name='Later unique quest speeches', reason='Garbud02-04, Zhar02, Snot02-03, Lach02-03 and unrelated unused/roar assets are outside first-contact scope.')],
                sources={str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                         for path in [*(SOURCE / n for n in ('monster.cpp', 'monstdat.cpp', 'diablo.cpp', 'player.cpp')),
                                      *(TOWN / n for n in ('enums.h', 'textdat.cpp', 'effects.cpp'))]})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    report = make_catalog()
    target = SOURCE / 'boss-speech-catalog.json'
    data = (json.dumps(report, indent=2, ensure_ascii=False) + '\n').encode()
    if args.check:
        assert target.read_bytes() == data, 'boss catalog is stale'
    else:
        target.write_bytes(data)
    print(json.dumps({'status': report['status'], 'audited_voice_count': len(report['voices']),
                      'production_voice_count': report['approved_voice_count'], 'catalog': str(target)}, indent=2))


if __name__ == '__main__':
    main()
