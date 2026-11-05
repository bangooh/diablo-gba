"""Extract original inputs for the current stage73 spell geometry packer."""
from pathlib import Path
import sys
import mpyq
from extract_assets import read_mpq

root = Path(__file__).resolve().parents[1]
source = root / 'source_assets/spell_expansion'
source.mkdir(parents=True, exist_ok=True)
archive = mpyq.MPQArchive(sys.argv[1], listfile=False)
names = ['Fireba1', 'Lghning', 'Firewal2', 'Guard2', 'Inferno', 'Holy1', 'Flare',
         'Sklball1', 'Newexp', 'Portal2', 'Rportal2', 'Manashld', 'Bluexfr', 'Bluexbk',
         'Firewal1', 'Ressur1', 'Acidpud1', 'Acidpud2', 'Bigexp', 'Guard1', 'Guard3']
names += ['Firerun' + str(i) for i in range(1, 9)]
names += ['Acidbf' + str(i) for i in range(1, 17)]
names += ['Magball' + str(i) for i in range(1, 9)]
paths = ['Missiles\\' + name + '.CL2' for name in names]
paths += ['Monsters\\Golem\\' + name + '.CL2' for name in ['GolemW', 'GolemA']]
for path in paths:
    (source / path.replace('\\', '_')).write_bytes(read_mpq(archive, path))
archive.file.close()
print('Extracted', len(paths), 'native spell files')
