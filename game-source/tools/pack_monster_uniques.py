"""Stage26 native leader definitions; existing 50 visual bodies stay unchanged.

Generate the four selected retail unique rows and their explicit minion kinds.
No image, palette, audio or MPQ data is rewritten by this tool.
"""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[1]
SELECTION = (
    ("Bonehead Keenaxe", "MT_TSKELAX", 9, "bonehead_keenaxe"),
    ("Rotcarnage", "MT_BZOMBIE", 7, "rotcarnage"),
    ("Breakspine", "MT_MUDRUN", 24, "breakspine"),
    ("Blackstorm", "MT_OBLORD", 26, "blackstorm"),
)
STAT_FIELDS = (
    "family", "min_floor", "max_floor", "level", "hp_min", "hp_max",
    "intelligence", "hit", "impact", "damage_min", "damage_max", "armor",
    "immune", "resist", "walk", "hit_frames", "image_cost", "xp", "attack_frames",
)


def split_fields(text):
    fields, start, depth, quoted, escape = [], 0, 0, False, False
    for i, char in enumerate(text):
        if quoted:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == '"':
                quoted = False
        elif char == '"':
            quoted = True
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
        elif char == "," and depth == 0:
            fields.append(text[start:i].strip())
            start = i + 1
    fields.append(text[start:].strip())
    return fields


def unique_rows(source):
    table = source.split("UniqMonstStruct UniqMonst[] = {", 1)[1].split("\n};", 1)[0]
    result = {}
    for line in table.splitlines():
        if not re.match(r"\s*\{\s*MT_", line):
            continue
        row = split_fields(line[line.index("{") + 1:line.rindex("}")])
        assert len(row) == 14, row
        result[json.loads(row[1])] = row
    return result


def stats(record):
    values = dict(record)
    values.update(walk=record["frames"][1], hit_frames=record["frames"][3],
                  attack_frames=record["frames"][2])
    return "{" + ",".join(str(values[key]) for key in STAT_FIELDS) + "}"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build_outputs(root=ROOT):
    catalog_path = root / "source_assets/monster_catalog/monsters.json"
    source_path = root / "source_assets/monster_catalog/monstdat.cpp"
    catalog = json.loads(catalog_path.read_text())
    bases = catalog["monsters"]
    assert len(bases) == 50 and [d["kind"] for d in bases] == list(range(50))
    rows = unique_rows(source_path.read_text())
    leaders, members = [], []
    for index, (name, native_type, base_kind, key) in enumerate(SELECTION):
        row, base = rows[name], bases[base_kind]
        assert row[0] == native_type
        flags, variable = int(row[10]), int(row[11])
        assert flags & 3 == 3 and int(row[13]) == 0
        # These four inherit the body's implemented AI; no substitute AI.
        assert row[5] == base["ai"]
        level, hp, intelligence = int(row[3]), int(row[4]), int(row[6])
        assert base["min_floor"] <= level <= base["max_floor"]
        translation = "Monsters\\Monsters\\" + json.loads(row[2]) + ".TRN"
        leader = copy.deepcopy(base)
        leader.update(kind=50 + index, base_kind=base_kind, key=key, name=name,
                      role="leader", native_type=native_type, unique_flags=flags,
                      spawn_floor=level, pack_size=8, minion_kind=54 + index,
                      min_floor=level, max_floor=level, level=2 * level,
                      hp_min=hp, hp_max=hp, ai=row[5], intelligence=intelligence,
                      damage_min=int(row[7]), damage_max=int(row[8]), xp=base["xp"] * 2,
                      hit=variable if flags & 4 else base["hit"],
                      armor=variable if flags & 8 else base["armor"],
                      immune=sum(1 << i for i, element in enumerate(("MAGIC", "FIRE", "LIGHTNING")) if "IMMUNE_" + element in row[9]),
                      resist=sum(1 << i for i, element in enumerate(("MAGIC", "FIRE", "LIGHTNING")) if "RESIST_" + element in row[9]),
                      translation=translation,
                      translation_chain=([base["translation"]] if base["translation"] else []) + [translation],
                      native_unique_fields=row)
        member = copy.deepcopy(base)
        member.update(kind=54 + index, base_kind=base_kind, key=key + "_minion",
                      role="minion", leader_kind=50 + index, spawn_floor=level,
                      ai=row[5], intelligence=intelligence,
                      hp_min=base["hp_min"] * 2, hp_max=base["hp_max"] * 2,
                      hp_roll_rule="roll_base_range_then_multiply_by_1_single_or_2_coop")
        leaders.append(leader)
        members.append(member)
    records = leaders + members
    outputs = {
        "source_assets/monster_catalog/uniques.json": json.dumps({
            "source_revision": catalog["source_revision"],
            "base_visual_kinds": 50,
            "membership": "explicit persisted minion kind; never inferred from distance",
            "monsters": records,
        }, indent=2) + "\n",
        "generated/monster_unique_defs.inc": "/* Native unique leaders50..53, authored minions54..57. */\n" + ",\n".join(stats(d) for d in records) + "\n",
        "generated/monster_unique_undead.inc": ",".join(str(int(d["undead"])) for d in records) + "\n",
        "generated/monster_unique_metadata.inc":
            "static const uint8_t unique_base_kinds[4]={" + ",".join(str(d["base_kind"]) for d in leaders) + "};\n"
            "static const uint8_t unique_spawn_floors[4]={" + ",".join(str(d["spawn_floor"]) for d in leaders) + "};\n",
    }
    report = {
        "source_revision": catalog["source_revision"],
        "source_sha256": sha(source_path.read_bytes()),
        "base_catalog_sha256": sha(catalog_path.read_bytes()),
        "leaders": [{"kind": d["kind"], "name": d["name"], "base_kind": d["base_kind"],
                     "floor": d["spawn_floor"], "flags": d["unique_flags"], "minion_kind": d["minion_kind"]} for d in leaders],
        "runtime_kinds": 58,
        "base_visual_kinds": 50,
        "native_max_members": 8,
        "new_image_bytes": 0,
        "single_player_unique_hp": "existing port ceil-half convention",
        "minion_hp": "uniform original base roll, then single-player x1 or coop x2",
        "outputs": {path: sha(data.encode()) for path, data in outputs.items()},
    }
    outputs["generated/monster-uniques-report.json"] = json.dumps(report, indent=2) + "\n"
    return outputs


def append_definition_includes(text):
    if '#include "monster_unique_defs.inc"' not in text:
        before, after = text.split("\n};\n", 1)
        text = before + ',\n#include "monster_unique_defs.inc"\n};\n' + after
    if '#include "monster_unique_undead.inc"' not in text:
        match = re.search(r"(const uint8_t monster_undead\[MONSTER_TYPES\]=\{)([^}]+)(\};)", text)
        assert match
        values = match.group(2).strip()
        assert len(values.split(",")) == 50
        text = text[:match.start()] + match.group(1) + values + ',\n#include "monster_unique_undead.inc"\n};' + text[match.end():]
    return text


def generate(root=ROOT, check=False):
    outputs = build_outputs(root)
    path = root / "generated/monster_defs.inc"
    outputs["generated/monster_defs.inc"] = append_definition_includes(path.read_text())
    for name, data in outputs.items():
        target = root / name
        if check:
            assert target.read_text() == data, "stale unique data: " + name
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(data)
    return outputs


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    generate(check=arguments.check)
    print("PASS native four leaders and eight explicit kind records" if arguments.check else "Generated four leaders and four authored minion definitions")
