#!/usr/bin/env python3
"""Compare current theme architecture with an independently extracted reference.

The extracted Devilution functions are modified only to replace two undefined
out-of-bounds reads with bounded zero-padding. See generated notice and license.
No original or production source file is edited by this tool.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
ORIGINAL = ROOT / 'source_assets/worldgen/gendung.cpp'
PRODUCTION = ROOT / 'src/worldgen/theme_arch.inc'
DRIVER = ROOT / 'tests/stage64-theme-architecture-driver.cpp'
LICENSE = ROOT / 'source_assets/stage64-reference/LICENSE.md'
PROVENANCE = ROOT / 'source_assets/stage64-reference/provenance.json'

HEAD = r'''#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#define BOOL int
#define TRUE 1
#define FALSE 0
#define DMAXX 40
#define DMAXY 40
#define DTYPE_CATACOMBS 2
#define DTYPE_CAVES 3
#define DTYPE_HELL 4
'''
SHARED = r'''static uint8_t dungeon[40][40],trans[112][112];
static int themeCount,leveltype,TransVal;
static uint32_t seed,calls;
static int random_(int,int v){if(v<=0)return 0;calls++;seed=seed*0x015a4e35u+1;uint32_t n=seed;if(n&0x80000000u)n=0u-n;return (int)((v<65535?n>>16:n)%(unsigned)v);}
static void rect(int x1,int y1,int x2,int y2){for(int x=x1;x<=x2;x++)for(int y=y1;y<=y2;y++){assert(x>=0&&x<112&&y>=0&&y<112);trans[x][y]=(uint8_t)TransVal;}TransVal++;}
static void mrect(int x1,int y1,int x2,int y2){rect(2*x1+17,2*y1+17,2*x2+16,2*y2+16);}
'''
ORIGINAL_SHIM = r'''struct Room{int x,y,width,height,ttval;};static Room themeLoc[512];
static int floor_at(int x,int y){return (unsigned)x<40&&(unsigned)y<40?dungeon[x][y]:0;}
static void DRLG_RectTrans(int a,int b,int c,int d){rect(a,b,c,d);}
static void DRLG_MRectTrans(int a,int b,int c,int d){mrect(a,b,c,d);}
int SkipThemeRoom(int,int);
'''
PRODUCTION_SHIM = r'''enum{WORLD_THEME_LIMIT=50};struct WorldThemeRoom{uint8_t x,y,width,height,room;};
struct State{WorldThemeRoom theme_rooms[50];};static State state,*ws=&state;
static uint8_t*game_rooms_candidate(void){return &trans[0][0];}
static void game_rooms_rect(int a,int b,int c,int d,int){rect(a,b,c,d);}
static void game_rooms_macro_rect(int a,int b,int c,int d,int){mrect(a,b,c,d);}
'''


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'generated/stage64-validation/theme-architecture')
    parser.add_argument('--compiler', default='g++')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    build = ROOT / 'build/stage64-theme-architecture-oracle'
    build.mkdir(parents=True, exist_ok=True)
    sources = [ORIGINAL, PRODUCTION, DRIVER, LICENSE, PROVENANCE, Path(__file__).resolve()]
    hashes = {str(p.relative_to(ROOT)): digest(p) for p in sources}
    provenance = json.loads(PROVENANCE.read_text())
    recorded = provenance['existing_local_sources']['source_assets/worldgen/gendung.cpp']['sha256']
    assert digest(ORIGINAL) == recorded, 'Reference changed: update provenance deliberately before comparing'
    native = ORIGINAL.read_text()
    first = native.index('BOOL DRLG_WillThemeRoomFit')
    end = native.index('\n#endif', native.index('void DRLG_PlaceThemeRooms', first))
    architecture = native[first:end]
    assert architecture.count('dungeon[xx][y + ii]') == 2  # comment plus actual expression
    assert architecture.count('dungeon[x + ii][yy]') == 1
    architecture = architecture.replace('dungeon[xx][y + ii]', 'floor_at(xx,y + ii)')
    architecture = architecture.replace('dungeon[x + ii][yy]', 'floor_at(x + ii,yy)')
    first = native.index('BOOL SkipThemeRoom')
    spacing = native[first:native.index('\nvoid InitLevels', first)]
    notice = '''/* MODIFIED REFERENCE for Stage64 validation.
 * Original functions: source_assets/worldgen/gendung.cpp, SHA-256 %s.
 * Local source commit identity is not asserted by source provenance.
 * Devilution Sustainable Use License: included LICENSE.md in this directory.
 * Two undefined out-of-bounds floor reads use bounded zero-padding; native
 * rejection conditions, scan order, RNG, sizing, and tile writes are retained.
 * Current production adaptation and independent driver occupy separate namespaces.
 */
''' % digest(ORIGINAL)
    wrapper = (notice + HEAD + 'namespace orig {\n' + SHARED + ORIGINAL_SHIM +
               architecture + '\n' + spacing + '\n}\nnamespace port {\n' + SHARED +
               PRODUCTION_SHIM + PRODUCTION.read_text() + '\n}\n' + DRIVER.read_text())
    cpp = out / 'architecture-oracle.cpp'
    cpp.write_text(wrapper)
    shutil.copyfile(LICENSE, out / 'LICENSE.md')
    executable = build / 'architecture-oracle'
    command = [args.compiler, '-std=c++17', '-O1', '-g', '-Wno-narrowing',
               '-fsanitize=address,undefined', str(cpp), '-o', str(executable)]
    compile_result = subprocess.run(command, text=True, capture_output=True)
    (out / 'compile.log').write_text(compile_result.stdout + compile_result.stderr)
    compile_result.check_returncode()
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '') + ':detect_leaks=0'
    result = subprocess.run([str(executable)], env=env, text=True, capture_output=True)
    (out / 'result.txt').write_text(result.stdout)
    (out / 'sanitizer.log').write_text(result.stderr)
    result.check_returncode()
    counts = json.loads(result.stdout.splitlines()[-1])
    assert counts == {'fitCases': 460800, 'fitMismatches': 0, 'scanCases': 1152, 'scanMismatches': 0}
    assert hashes == {str(p.relative_to(ROOT)): digest(p) for p in sources}, 'Sources changed during validation'
    report = {
        'result': 'PASS',
        'method': 'Independent source extraction oracle. Original functions remain unchanged except two undefined out-of-bounds floor reads use zero-padded bounded reads. Native early rejection, absolute-coordinate comparisons, nested sizing RNG, scan order, and tile writes remain. Production theme_arch.inc is compiled alongside in an isolated namespace.',
        'comparison': ['Fit success and room dimensions at every 40x40 coordinate',
                       'Full 40x40 dungeon tile maps after scans',
                       'Full 112x112 transmission room IDs',
                       'Room count and every descriptor including ttval/room identity',
                       'RNG state, RNG call count, and next room allocation ID'],
        'coverage': {'families': ['Catacombs', 'Caves', 'Hell'], 'shapes': 8,
                     'scenesPerShape': 12, 'fullScanSeedsPerScene': 4, **counts},
        'sanitizers': ['AddressSanitizer', 'UndefinedBehaviorSanitizer'],
        'leakDetection': False,
        'leakDetectionReason': 'LeakSanitizer unsupported under container ptrace; harness uses no dynamic allocation.',
        'sourceSha256': hashes,
        'generatedOracleSha256': digest(cpp),
        'reference': {'repository': provenance['repository'], 'localFile': str(ORIGINAL.relative_to(ROOT)),
                      'localFileSha256': digest(ORIGINAL), 'localFileCommit': None,
                      'localFileCommitNote': 'Existing local source; commit identity not asserted.',
                      'license': 'Sustainable Use License 1.0',
                      'licenseCopiedFromCommit': provenance['commit'],
                      'licenseSha256': digest(LICENSE)},
        'reviewFinding': 'Native lower-right early rejection was missing during independent review and was restored by implementation owner before this successful comparison.',
        'limits': ['Bounded reference reads intentionally replace undefined original out-of-bounds behavior.',
                   'Eight synthetic geometry families; not every generated campaign.',
                   'Reference array-cap overflow behavior is outside native-valid equivalence coverage.'],
        'reproduce': 'python3 tools/stage64_theme_architecture_oracle.py',
    }
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'result': 'PASS', **counts, 'report': str(out / 'report.json')}))


if __name__ == '__main__':
    main()
