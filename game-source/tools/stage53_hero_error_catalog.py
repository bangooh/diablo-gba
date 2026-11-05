"""Reproduce original retail class-specific mana/ground-pickup failure mapping."""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from stage53_dialogue_catalog import ROOT, SOURCE, STRING, body, enum, preprocess, string


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    enums = preprocess(SOURCE / 'enums.h')
    effects = preprocess(SOURCE / 'effects.cpp')
    sounds = enum(enums, '_sfx_id')
    rows = re.findall(r'\{\s*([^,]+),\s*(' + STRING + r'),\s*NULL\s*\}', body(effects, 'sgSFX[]'))
    classes = []
    for cls, name, prefix, variants, weights in [(0, 'Warrior', 'WARR', ['14', '14B', '14C'], [1, 4, 4]),
                                                (1, 'Rogue', 'ROGUE', ['14', '15', '16'], [1, 1, 1]),
                                                (2, 'Sorcerer', 'MAGE', ['14', '15', '16'], [1, 1, 1])]:
        def record(suffix, weight=1):
            symbol = 'PS_' + prefix + suffix
            return dict(sfx=symbol, sfx_id=sounds[symbol], mpq_path=string(rows[sounds[symbol]][1]), weight=weight)
        classes.append(dict(class_id=cls, name=name, mana=record('35'), inventory_full=[record(v, w) for v, w in zip(variants, weights)]))
    # Compile the actual native SFX initializer and compare every selected path.
    code = '#include <assert.h>\n#include <stddef.h>\n#include <string.h>\n'
    code += '\n'.join('enum ' + n + '{' + body(enums, 'enum ' + n) + '};' for n in ('_sfx_id', 'sfx_flag'))
    code += 'typedef struct {int flags;const char*path;void*sound;} Sfx;\nstatic const Sfx native_sfx[]={' + body(effects, 'sgSFX[]') + '};\nint main(void){\n'
    for cls in classes:
        for r in [cls['mana'], *cls['inventory_full']]:
            code += f'assert({r["sfx"]}=={r["sfx_id"]});assert(!strcmp(native_sfx[{r["sfx"]}].path,{json.dumps(r["mpq_path"])}));\n'
    code += 'return 0;}\n'
    from pathlib import Path
    with tempfile.TemporaryDirectory(prefix='hero-error-source-') as tmp:
        p = Path(tmp) / 'check.c'; p.write_text(code)
        subprocess.run(['gcc', '-std=c11', str(p), '-o', str(p.with_suffix(''))], check=True)
        subprocess.run([str(p.with_suffix(''))], check=True)
    inv = ROOT / 'source_assets/item_catalog/inv.cpp'
    spell = ROOT / 'source_assets/item_catalog/spells.cpp'
    player = ROOT / 'source_assets/boss_speech_stage53/player.cpp'
    stores = SOURCE / 'stores.cpp'
    assert 'PlaySFX(random_(0, 3) + PS_WARR14)' in inv.read_text()
    assert 'if (plr[myplr]._pRSplType == RSPLTYPE_SPELL)' in player.read_text()
    assert not re.search(r'PS_(?:WARR|ROGUE|MAGE)(?:14|15|16)', stores.read_text())
    files = [inv, spell, player, stores, SOURCE / 'effects.cpp', SOURCE / 'enums.h']
    report = dict(status='PASS compiled native SFX IDs and paths for all12 records',
                  source_commit='9f01757243412bb154d02d9af65c804159e6e0ea',
                  build_flags='retail Diablo; neither SPAWN nor HELLFIRE',
                  api_contract=dict(classes='0Warrior,1Rogue,2Sorcerer', reasons='0mana,1inventory_full'),
                  classes=classes,
                  mana=dict(trigger='CheckPlrSpell: real local cast attempt, RSPLTYPE_SPELL, CheckSpell returns false.',
                            check='CheckSpell returns false if learned spell level<=0 or mana<GetManaAmount. Cursor and UI-target gating happen before the failure voice.',
                            exclusions='Invalid selected spell uses class34; town-forbidden spell uses class27. Scroll/staff failures do not use class35. Skills bypass mana check.',
                            port_scope='Only actual insufficient-mana failure requested; do not announce unrelated invalid/unlearned/town/item failures.'),
                  inventory_full=dict(trigger='AutoGetItem: after gold placement or autoequip, usable1x1 belt and all backpack AutoPlace attempts fail, and pnum==myplr.',
                                      consequences='Original item remains/respawns on ground; CMD_RESPAWNITEM synchronizes it. Voice is local-only.',
                                      exclusions='InvGetItem manual cursor pickup has no capacity voice. Stores use STORE_NOROOM text, and equipment/reward insertion is not a generic class14 failure site.',
                                      variants='Rogue/Mage IDs14..16 contiguous. Warrior enum14,14B,14C occupy the three initial random slots; RndSFX rerolls base14, yielding exact relative weights1:4:4. Warrior15/16 groups are not selected by this call.'),
                  port_presentation_policy='Private cosmetic RNG, accepted-cue cooldown2seconds, drop while greeting/item voice busy; these are port presentation rules, not original gameplay mutations.',
                  subtitle='These voice assets have no corresponding alltext subtitle records in this audit; no transcript inferred from filenames.',
                  sources={str(p.relative_to(ROOT)):dict(sha256=hashlib.sha256(p.read_bytes()).hexdigest(),
                           primary_url='https://raw.githubusercontent.com/diasurgical/devilution/9f01757243412bb154d02d9af65c804159e6e0ea/' + ('' if p.name=='enums.h' else 'Source/') + p.name) for p in files})
    target = ROOT / 'source_assets/hero_errors_stage53/hero-error-source-audit.json'
    data = (json.dumps(report, indent=2) + '\n').encode()
    if args.check:
        assert target.read_bytes() == data, 'hero error source audit is stale'
    else:
        target.parent.mkdir(exist_ok=True); target.write_bytes(data)
    print(json.dumps(dict(status=report['status'], path=str(target)), indent=2))


if __name__ == '__main__':
    main()
