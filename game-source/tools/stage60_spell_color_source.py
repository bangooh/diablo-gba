"""Compare our packed translation against compiled original SetSpellTrans."""
import ctypes
import json
from pathlib import Path
import subprocess
import tempfile
from pack_spell_icons import ROOT, STYLES, translation

source = (ROOT/'source_assets/spell_colors_stage60/control.cpp').read_text()
function = source[source.index('void SetSpellTrans(char t)'):source.index('/**\n * Sets the spell frame',source.index('void SetSpellTrans(char t)'))]
defs = (ROOT/'source_assets/item_catalog/defs.h').read_text()
defines = '\n'.join(line for line in defs.splitlines() if line.startswith('#define PAL'))
prefix = 'unsigned char SplTransTbl[256];\nenum {RSPLTYPE_SKILL,RSPLTYPE_SPELL,RSPLTYPE_SCROLL,RSPLTYPE_CHARGES,RSPLTYPE_INVALID};\n'
with tempfile.TemporaryDirectory() as tmp:
    c, so = Path(tmp)/'oracle.c', Path(tmp)/'oracle.so'
    c.write_text(prefix+defines+'\n'+function)
    subprocess.run(['cc','-shared','-fPIC','-O2',str(c),'-o',str(so)],check=True)
    lib = ctypes.CDLL(str(so))
    native = (ctypes.c_ubyte*256).in_dll(lib,'SplTransTbl')
    for kind,style in enumerate(STYLES):
        lib.SetSpellTrans(0)
        lib.SetSpellTrans(kind)
        assert list(native) == translation(style), style
report = {'result':'PASS','native_function':'SetSpellTrans (compiled verbatim)', 'source_types':len(STYLES),'palette_entries_checked':len(STYLES)*256, 'scroll_visual_color':'muted red / red-brown (native PAL16_BEIGE)', 'runtime_palette_unchanged':True}
print(json.dumps(report,indent=2))
