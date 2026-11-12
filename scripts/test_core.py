"""Asset-free lobby, save, floor-validation and codec regressions. Requires a C compiler."""
from pathlib import Path
import ast,os,subprocess,tempfile,sys
r=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s', str(r/'tests'), '-p', 'test_build_environment.py'], check=True)
for f in r.rglob('*.py'):ast.parse(f.read_text(),filename=str(f))
with tempfile.TemporaryDirectory() as d:
 exe=Path(d)/'test-lobby'
 subprocess.run([os.environ.get('CC','cc'),'-std=c11','-DLINK_HOST_TEST','-I',str(r/'game-source/src'),str(r/'tests/test_lobby.c'),str(r/'game-source/src/link.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)

# Run each distributed flash implementation against the same fault scenarios.
with tempfile.TemporaryDirectory() as d:
 for source in ('game-source/src/flash.c','variants/flash-standard.c','variants/flash-ez4.c'):
  exe=Path(d)/'test-flash'
  subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-DFLASH_HOST_TEST',
                  '-I',str(r/'game-source/src'),str(r/'tests/test_flash.c'),str(r/source),
                  '-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)

# Compile the actual Game/archive declarations and validator without MPQ assets.
# Only ITEM_COUNT is supplied as a boundary fixture; no generated item art/catalog
# is needed. Pull layouts from production text so tests cannot silently drift.
import re
game=(r/'game-source/src/game.h').read_text()
menu=(r/'game-source/src/menu.inc').read_text()
def extract(pattern,text):
 match=re.search(pattern,text,re.S)
 if not match:raise RuntimeError('Production declaration changed: '+pattern)
 return match.group(0)
declarations='\n'.join([
 '#include <stdint.h>\n#include <stddef.h>\n#define GRAPHICS_ASSETS\n#define UI_FN\ntypedef int32_t GameGold;',
 extract(r'enum \{ MAP_W=80,.*?\};',game),
 *[extract(r'typedef struct \{[^{}]*\} '+name+r';',game) for name in ('Enemy','Drop','Prop')],
 extract(r'typedef struct \{\s*uint8_t map\[.*?\} Game;',game),
 extract(r'#define MONSTER_TYPES \d+',game),
 *[extract(r'typedef struct \{[^{}]*\} '+name+r';',menu) for name in ('SavedEnemy','SavedDrop','SavedProp')],
 *[extract(r'#define '+name+r' [^\n]+',menu) for name in ('FLOOR_MIDDLE_OFFSET','FLOOR_MIDDLE_BYTES','FLOOR_END_OFFSET')],
 extract(r'typedef struct \{SavedEnemy.*?\} FloorState;',menu),
])
with tempfile.TemporaryDirectory() as d:
 harness=Path(d)/'floor.c'
 harness.write_text(declarations+'\n'+(r/'tests/test_floor_validation.c').read_text())
 for count in (200,256):
  exe=Path(d)/'test-floor'
  subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-Wall','-Wextra',
                  '-DITEM_COUNT='+str(count),'-I',str(r/'game-source/src'),str(harness),
                  '-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)

with tempfile.TemporaryDirectory() as d:
 exe=Path(d)/'test-codecs'
 subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-fno-strict-aliasing',
                 '-I',str(r/'game-source/src'),str(r/'tests/test_codecs.c'),
                 str(r/'game-source/src/lz.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
