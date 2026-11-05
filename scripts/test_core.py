"""Asset-free lobby packet regression test. Requires a C compiler."""
from pathlib import Path
import ast,os,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
for f in r.rglob('*.py'):ast.parse(f.read_text(),filename=str(f))
with tempfile.TemporaryDirectory() as d:
 exe=Path(d)/'test-lobby'
 subprocess.run([os.environ.get('CC','cc'),'-std=c11','-DLINK_HOST_TEST','-I',str(r/'game-source/src'),str(r/'tests/test_lobby.c'),str(r/'game-source/src/link.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
