"""Audit the pinned retail Diablo town dialogue catalog; no ROM/runtime edits.

Preprocess with neither SPAWN nor HELLFIRE, then join the native speech enum,
alltext, sound enum, sgSFX, and Qtalklist. Preserve original strings and flags.
"""
from pathlib import Path
import ast
import argparse
import hashlib
import json
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'source_assets/town_dialogue_stage53'
STRING = r'"(?:[^"\\]|\\.)*"'
NPCS = [
    (0, 0, 'Griswold', 'GRISWOLD', 13, 'Smith'),
    (1, 3, 'Ogden', 'OGDEN', 10, 'Tavern'),
    (2, 2, 'Wounded townsman', None, 0, None),
    (3, 6, 'Adria', 'ADRIA', 13, 'Witch'),
    (4, 7, 'Gillian', 'GILLIAN', 10, 'BarMaid'),
    (5, 8, 'Wirt', 'WIRT', 12, 'Boy'),
    (6, 1, 'Pepin', 'PEPIN', 11, 'Healer'),
    (7, 4, 'Cain', 'STORY', 11, 'Story'),
    (8, 5, 'Farnham', 'FARNHAM', 13, 'Drunk'),
]
NOTICE_TEXT = {
    1: 'TEXT_INFRA5', 2: 'TEXT_INFRA7', 4: 'TEXT_ANVIL5', 5: 'TEXT_ANVIL7',
    11: 'TEXT_BANNER2', 12: 'TEXT_BANNER3', 16: 'TEXT_KING2', 17: 'TEXT_KING4',
    18: 'TEXT_POISON3', 19: 'TEXT_POISON5', 21: 'TEXT_VILE1', 22: 'TEXT_VILE3',
    25: 'TEXT_MUSH8', 26: 'TEXT_MUSH10', 27: 'TEXT_MUSH4', 28: 'TEXT_MUSH12',
}


def preprocess(path):
    text = re.sub(r'^\s*#include[^\n]*', '', path.read_text(), flags=re.M)
    return subprocess.check_output(['gcc', '-E', '-P', '-undef', '-x', 'c', '-'], input=text, text=True)


def body(source, marker):
    start = source.index('{', source.index(marker))
    depth, end, quoted, escape = 1, start + 1, False, False
    while depth:
        char = source[end]
        if quoted:
            if escape:
                escape = False
            elif char == '\\':
                escape = True
            elif char == '"':
                quoted = False
        elif char == '"':
            quoted = True
        else:
            depth += (char == '{') - (char == '}')
        end += 1
    return source[start + 1:end - 1]


def enum(source, name):
    values, current = {}, -1
    for entry in body(source, 'enum ' + name).split(','):
        parts = entry.strip().split('=')
        if not parts[0]:
            continue
        current = int(parts[1], 0) if len(parts) == 2 else current + 1
        values[parts[0].strip()] = current
    return values


def string(expression):
    return ''.join(ast.literal_eval(token) for token in re.findall(STRING, expression))


def verify_native_and_generated(src, header, blob, records):
    """Compile actual native initializers and generated tables independently."""
    native_enums = '\n'.join('enum ' + name + '{' + body(src['enums.h'], 'enum ' + name) + '};'
                             for name in ('_speech_id', '_sfx_id', 'sfx_flag'))
    code = '#include <assert.h>\n#include <stdio.h>\n#include <string.h>\n#include <ctype.h>\n'
    code += '#include "town_dialogue.h"\n' + native_enums
    code += '\ntypedef struct {const char*txtstr;int scrlltxt,txtspd,sfxnr;} TextDataStruct;\n'
    code += 'static const TextDataStruct native_text[]={' + body(src['textdat.cpp'], 'alltext[]') + '};\n'
    code += 'typedef struct {int flags;const char*path;void*sound;} TSFX;\n'
    code += 'static const TSFX native_sfx[]={' + body(src['effects.cpp'], 'sgSFX[]') + '};\n'
    code += 'const uint8_t asset_town_dialogue_text[]={' + ','.join(str(value) for value in blob) + '};\n'
    code += r'''
int main(void){
 _Static_assert(sizeof(TownDialogueMessage)==6,"compact message ABI");
 _Static_assert(sizeof(TownDialogueNpc)==6,"compact NPC ABI");
 assert(sizeof(native_text)/sizeof(native_text[0])==TOWN_DIALOGUE_MESSAGE_COUNT);
 unsigned count=0;
 for(unsigned id=0;id<TOWN_DIALOGUE_MESSAGE_COUNT;id++){
  TownDialogueMessage m=town_dialogue_messages[id];
  if(m.voice==TOWN_DIALOGUE_NONE){assert(!m.text_offset);continue;}
  assert(m.voice==count++&&m.text_offset<TOWN_DIALOGUE_TEXT_BYTES);
  assert(m.scroll==native_text[id].scrlltxt&&m.speed==native_text[id].txtspd);
  const char*text=native_text[id].txtstr;size_t n=strlen(text);
  while(n&&isspace((unsigned char)text[n-1]))n--;
  if(n&&text[n-1]=='|'){n--;while(n&&isspace((unsigned char)text[n-1]))n--;}
  const char*packed=(const char*)asset_town_dialogue_text+m.text_offset;
  assert(strlen(packed)==n&&!memcmp(packed,text,n));
  printf("%u\t%u\t%d\t%s\n",id,m.voice,native_text[id].sfxnr,native_sfx[native_text[id].sfxnr].path);
 }
 assert(count==TOWN_DIALOGUE_VOICE_COUNT);
 return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='town-dialogue-source-check-') as directory:
        root = Path(directory)
        (root / 'town_dialogue.h').write_bytes(header)
        (root / 'check.c').write_text(code)
        subprocess.run(['gcc', '-std=c11', '-O1', str(root / 'check.c'), '-o', str(root / 'check')], check=True)
        result = subprocess.check_output([str(root / 'check')], text=True).splitlines()
    expected = [f'{r["speech_id"]}\t{r["voice_index"]}\t{r["sfx_id"]}\t{r["mpq_path"]}' for r in records]
    assert result == expected, 'native compiler and parsed/generated catalog disagree'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    src = {name: preprocess(SOURCE / name) for name in ('enums.h', 'textdat.cpp', 'effects.cpp', 'towners.cpp', 'stores.cpp')}
    speeches, sounds = enum(src['enums.h'], '_speech_id'), enum(src['enums.h'], '_sfx_id')
    speech_names = {number: name for name, number in speeches.items()}
    effects = re.findall(r'\{\s*([^,]+),\s*(' + STRING + r'),\s*NULL\s*\}', body(src['effects.cpp'], 'sgSFX[]'))
    rows = re.findall(r'\{\s*((?:' + STRING + r'\s*)+),\s*(\d+),\s*(\d+),\s*(\w+)\s*\}', body(src['textdat.cpp'], 'alltext[]'))
    assert len(rows) == 259, len(rows)
    qtalk = [[x.strip() for x in row.split(',')] for row in re.findall(r'\{([^{}]*)\}', body(src['towners.cpp'], 'Qtalklist[]'))]
    assert len(qtalk) >= 9 and all(len(row) == 16 for row in qtalk)
    quests = ['The Magic Rock', 'Black Mushroom', 'Gharbad the Weak', 'Zhar the Mad', 'Lachdanan', 'Diablo', 'The Butcher', "Ogden's Sign", 'Halls of the Blind', 'Valor', 'Anvil of Fury', 'Warlord of Blood', 'The Curse of King Leoric', 'Poisoned Water Supply', 'The Chamber of Bone', 'Archbishop Lazarus']
    talk = body(src['towners.cpp'], 'void TalkToTowner(')
    special = sorted(set(re.findall(r'InitQTextMsg\((TEXT_\w+)\)', talk)))
    # These quest topics are changed at runtime by the native mushroom flow.
    dynamic = sorted(set(re.findall(r'Qtalklist\[[^]]+\]\._qblkm\s*=\s*(TEXT_\w+)', talk)))
    wanted, uses, npcs = set(special + dynamic), {}, []

    def use(symbol, role):
        wanted.add(symbol)
        uses.setdefault(symbol, []).append(role)

    for symbol in special:
        use(symbol, 'native contact event')
    for symbol in dynamic:
        use(symbol, 'dynamic mushroom topic')
    for port, native, name, prefix, end, menu in NPCS:
        greeting = 'TEXT_' + prefix + '1' if prefix else None
        # Native enums omit several numbered names (e.g.GRISWOLD11). The
        # gameplay range is contiguous numeric speech IDs, not name suffixes.
        gossip = [speech_names[i] for i in range(speeches['TEXT_' + prefix + '2'],
            speeches['TEXT_' + prefix + str(end)] + 1)] if prefix else []
        if greeting:
            use(greeting, name + ' greeting')
        for symbol in gossip:
            use(symbol, name + ' gossip')
        topics = []
        for quest, symbol in enumerate(qtalk[native]):
            if symbol == '-1':
                continue
            use(symbol, name + ' quest topic ' + str(quest))
            topics.append({'quest': quest, 'quest_name': quests[quest], 'speech': symbol})
        landing = []
        if menu:
            landing = [{'line': int(y), 'text': string(text), 'color': color, 'selectable': selectable == 'TRUE'}
                       for y, text, color, selectable in re.findall(r'AddSText\(0,\s*(\d+),\s*TRUE,\s*(' + STRING + r'),\s*(COL_\w+),\s*(TRUE|FALSE)\)', body(src['stores.cpp'], 'void S_Start' + menu + '('))]
        npcs.append({'port_id': port, 'native_id': native, 'name': name, 'greeting': greeting,
                     'gossip': gossip, 'quest_topics': topics, 'native_landing_rows': landing})
    records = []
    for voice_index, symbol in enumerate(sorted(wanted, key=speeches.get)):
        ident = speeches[symbol]
        text, scroll, speed, sound_symbol = rows[ident]
        sound_id = sounds[sound_symbol]
        flags, path = effects[sound_id]
        records.append({'speech': symbol, 'speech_id': ident, 'voice_index': voice_index, 'text': string(text),
                        'scroll_text': bool(int(scroll)), 'scroll_speed_index': int(speed),
                        'sfx': sound_symbol, 'sfx_id': sound_id, 'sfx_flags': flags.strip(),
                        'mpq_path': string(path), 'uses': uses.get(symbol, [])})
    paths = sorted(set(row['mpq_path'] for row in records))
    report = {'status': 'generated source catalog and ROM data; no quest mutation logic',
              'source_commit': '9f01757243412bb154d02d9af65c804159e6e0ea',
              'build_flags': 'retail Diablo; SPAWN and HELLFIRE undefined',
              'native_alltext_records': len(rows), 'reachable_town_speech_records': len(records),
              'unique_voice_paths': len(paths), 'npcs': npcs, 'special_contact_speeches': special,
              'dynamic_mushroom_topics': dynamic, 'dialogues': records, 'voice_paths': paths,
              'port_notice_speeches': NOTICE_TEXT,
              'port_quest_index_contract': 'indices0..15 match retail; Butcher active state lives in butcher_quest, not quest_progress.state[6]',
              'sources': {name: hashlib.sha256((SOURCE / name).read_bytes()).hexdigest() for name in src}}
    target = SOURCE / 'town-dialogue-catalog.json'
    blob = bytearray(b'\0')
    messages = [(0, 65535, 0, 0) for _ in rows]
    for record in records:
        # Preserve the complete prose/newlines. The native trailing pipe is
        # a renderer terminator, not visible dialogue punctuation.
        text = record['text'].rstrip()
        if text.endswith('|'):
            text = text[:-1].rstrip()
        encoded = text.encode('ascii') + b'\0'
        offset = len(blob)
        blob.extend(encoded)
        messages[record['speech_id']] = (offset, record['voice_index'], int(record['scroll_text']), record['scroll_speed_index'])
    assert len(blob) <= 65535, 'text offsets must widen before this bound changes'
    assert len(records) < 65535
    gossip = []
    npc_rows = []
    for npc in npcs:
        npc_rows.append((speeches[npc['greeting']] if npc['greeting'] else 65535,
                         npc['native_id'], len(gossip), len(npc['gossip'])))
        gossip.extend(speeches[symbol] for symbol in npc['gossip'])
    assert len(gossip) < 256
    header = [
        '/* Generated by tools/stage53_dialogue_catalog.py from pinned retail Diablo. */',
        '#pragma once', '#include <stdint.h>',
        '#define TOWN_DIALOGUE_NONE 65535u',
        f'#define TOWN_DIALOGUE_MESSAGE_COUNT {len(rows)}',
        f'#define TOWN_DIALOGUE_VOICE_COUNT {len(records)}',
        '#define TOWN_DIALOGUE_NPC_COUNT 9',
        '#define TOWN_DIALOGUE_QUEST_COUNT 16',
        '#define TOWN_DIALOGUE_NOTICE_COUNT 30',
        f'#define TOWN_DIALOGUE_TEXT_BYTES {len(blob)}',
        '/* Native speech IDs stay stable; voice IDs are catalog order, ascending speech ID. */',
        'enum {',
    ]
    header += [f' TOWN_{record["speech"]}={record["speech_id"]},' for record in records]
    header += [
        '};',
        'typedef struct {uint16_t text_offset,voice;uint8_t scroll,speed;} TownDialogueMessage;',
        'typedef struct {uint16_t greeting;uint8_t native_id,gossip_first,gossip_count;} TownDialogueNpc;',
        '/* Raw NUL-terminated prose. Terminal native pipe markers are removed. */',
        'extern const uint8_t asset_town_dialogue_text[];',
        '/* An absent native message has text_offset0 and voiceTOWN_DIALOGUE_NONE. */',
        'static const TownDialogueMessage town_dialogue_messages[TOWN_DIALOGUE_MESSAGE_COUNT]={',
    ]
    header += [' {' + ','.join(str(value) for value in row) + '},' for row in messages]
    header += ['};', 'static const TownDialogueNpc town_dialogue_npcs[TOWN_DIALOGUE_NPC_COUNT]={']
    header += [' {' + ','.join(str(value) for value in row) + '},' for row in npc_rows]
    header += ['};', 'static const uint16_t town_dialogue_gossip[]={',
               ' ' + ','.join(str(value) for value in gossip), '};',
               '/* Port NPC rows; native/current quest columns0..15; -1 means no topic. */',
               'static const int16_t town_dialogue_topics[TOWN_DIALOGUE_NPC_COUNT][TOWN_DIALOGUE_QUEST_COUNT]={']
    for npc in npcs:
        row = [-1] * 16
        for topic in npc['quest_topics']:
            row[topic['quest']] = speeches[topic['speech']]
        header.append(' {' + ','.join(str(value) for value in row) + '},')
    header += ['};', 'static const uint16_t town_dialogue_notice[TOWN_DIALOGUE_NOTICE_COUNT]={',
               ' ' + ','.join(str(speeches[NOTICE_TEXT[i]]) if i in NOTICE_TEXT else '65535' for i in range(30)), '};',
               'static const char *const town_dialogue_quest_names[TOWN_DIALOGUE_QUEST_COUNT]={']
    header += [' ' + json.dumps(name) + ',' for name in quests]
    header += ['};', 'static const char *const town_dialogue_npc_names[TOWN_DIALOGUE_NPC_COUNT]={']
    header += [' ' + json.dumps(npc['name']) + ',' for npc in npcs]
    header += ['};', '']
    order = [{'voice_index': record['voice_index'], 'speech_id': record['speech_id'],
              'speech': record['speech'], 'mpq_path': record['mpq_path']} for record in records]
    generated = ROOT / 'generated'
    outputs = {
        target: (json.dumps(report, indent=2, ensure_ascii=False) + '\n').encode(),
        generated / 'town_dialogue.h': '\n'.join(header).encode(),
        generated / 'town_dialogue_text.lz': bytes(blob),
        generated / 'town-dialogue-voice-order.json': (json.dumps(order, indent=2) + '\n').encode(),
    }
    verify_native_and_generated(src, outputs[generated / 'town_dialogue.h'], blob, records)
    metadata = {'status': 'PASS', 'source_commit': report['source_commit'],
        'native_message_count': len(rows), 'included_messages': len(records),
        'text_bytes': len(blob), 'text_offset_bits': 16, 'gossip_references': len(gossip),
        'voice_order': 'ascending native speech ID; exact207-record catalog order',
        'quest_index_mapping': list(range(16)), 'extra_persistent_ram_bytes': 0,
        'native_c_initializers_vs_generated_catalog': 'PASS all207 text/voice/scroll/speed records;6-byte message and NPC ABI',
        'text_sha256': hashlib.sha256(blob).hexdigest(),
        'header_sha256': hashlib.sha256(outputs[generated / 'town_dialogue.h']).hexdigest()}
    outputs[generated / 'town-dialogue-report.json'] = (json.dumps(metadata, indent=2) + '\n').encode()
    for path, data in outputs.items():
        if args.check:
            assert path.read_bytes() == data, path
        else:
            path.write_bytes(data)
    print(json.dumps({key: report[key] for key in ('native_alltext_records', 'reachable_town_speech_records', 'unique_voice_paths', 'special_contact_speeches', 'dynamic_mushroom_topics')}, indent=2))
    print(target)
    print(json.dumps(metadata, indent=2))


if __name__ == '__main__':
    main()
