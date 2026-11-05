"""Snapshot freshly generated pre-reindexing inputs for the palette conversion."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--project', type=Path, required=True)
a = p.parse_args()
names = '''bg_palette.bin world_l1.rle world_l2.rle world_l3.rle world_l4.rle
dungeon_art_raw.rle floor_variants.rle water_quest.h prop_frames.lz door_frames.lz
explosive_barrels.lz wall_traps.lz floor_decals.rle prop_highlight.h
control_panel_dungeon.lz dungeon_font.lz dungeon_font_large.lz
dungeon_overlay_font.lz dungeon_status_frame.lz'''.split()
out = a.project / 'source_assets/bg_stage41'
out.mkdir(parents=True, exist_ok=True)
rows = []
for name in names:
    source = a.project / 'generated' / name
    if not source.is_file():
        raise FileNotFoundError(source)
    shutil.copy2(source, out / name)
    rows.append(dict(file=name, sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
(out / 'snapshot.json').write_text(json.dumps(rows, indent=2) + '\n')
