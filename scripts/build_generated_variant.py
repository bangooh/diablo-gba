"""Build Standard or EZ4 from an already generated private MPQ project."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--project', type=Path, required=True)
p.add_argument('--variant', choices=('standard', 'ez4'), required=True)
p.add_argument('--zig', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
output = a.output.resolve()
project = a.project.resolve()
if output.exists():
    p.error('Use a new output directory')
if output == ROOT or ROOT in output.parents or output == project or project in output.parents:
    p.error('Output must be outside the public source tree and generated project')
if not (project / 'generated/packed_assets.h').is_file():
    p.error('Missing generated project; complete MPQ regeneration first')
staging = output / 'variant-source'
staging.mkdir(parents=True)
for name in ('src', 'generated'):
    shutil.copytree(project / name, staging / name)
shutil.copy2(project / 'link.ld', staging / 'link.ld')
shutil.copy2(ROOT / 'variants' / ('flash-' + a.variant + '.c'), staging / 'src/flash.c')
f = staging / 'src/frontend_ui.inc'
s = f.read_text().replace('V 0.9.6 EZ4"', 'V 0.9.6"')
if a.variant == 'ez4':
    s = s.replace('V 0.9.6"', 'V 0.9.6 EZ4"')
f.write_text(s)
subprocess.run([sys.executable, str(ROOT / 'game-source/build.py'), '--zig', str(a.zig.resolve()),
                '--source-root', str(staging), '--output', str(output)], check=True)
