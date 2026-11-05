"""Rebuild full monster/difficulty metadata without requiring monster artwork.

Recreates the base Cathedral catalog directly from pinned source tables.
"""
from pathlib import Path
import json
import subprocess
import sys
from pack_monster_uniques import generate
from pack_monsters_stage66 import catalog_records
from pack_monsters_stage71 import records

root = Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, str(root / 'tools/pack_cathedral_monsters.py'), '--catalog-only'], check=True)
generate(root)
source = root / 'source_assets/monster_stage66'
source.mkdir(parents=True, exist_ok=True)
(source / 'catalog.json').write_text(json.dumps({
    'source_revision': '9f01757243412bb154d02d9af65c804159e6e0ea',
    'records': catalog_records(), 'scope': 'metadata only; artwork geometry not built'
}, indent=2) + '\n')
(root / 'source_assets/monster_stage71').mkdir(parents=True, exist_ok=True)
records()
subprocess.run([sys.executable, str(root / 'tools/pack_difficulty.py')], check=True)
