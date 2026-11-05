#!/usr/bin/env python3
"""Run the existing independent light oracle plus incremental publication cases."""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

EXTRA = r'''
static unsigned incremental_checks, incremental_partial, incremental_same;
static void check_incremental(void){
 uint32_t oracle[161],before[161];const uint32_t *old=light_window_live;
 if(old)memcpy(before,old,sizeof(before));
 reference(oracle);lighting_hardware_prepare();
 if(old)assert(!memcmp(before,old,sizeof(before))); /* inactive table only */
 if(game.area||!light_count){
  lighting_hardware_commit();assert(!light_window_live);return;
 }
 assert(lighting_hardware_active());
 unsigned different=0;if(old)for(unsigned y=0;y<160;y++)different+=before[y]!=oracle[y];
 if(old&&!different)incremental_same++;
 if(old&&different&&different<100)incremental_partial++;
 R16(0x04000006)=161;R16(0x04000208)=1;lighting_hardware_commit();
 assert(light_window_live);assert(R16(0x04000208)==1);
 assert(!memcmp(oracle,light_window_live,sizeof(oracle)));
 assert(light_window_live[160]==light_window_live[0]);
 /* IRQ restart may only point inside the currently published table. */
 for(unsigned y=0;y<=160;y+=40){R16(0x04000006)=y;lighting_hardware_vblank_irq();
  unsigned line=y>=160?0:y;
  assert(R32(0x04000040)==light_window_live[line]);
  assert(R32(0x040000b0)==(uint32_t)(uintptr_t)(light_window_live+line+1));
 }
 incremental_checks++;
}
static void incremental_sequence(void){
 memset(light_sources,0,sizeof(light_sources));cam_x=cam_y=0;game.area=0;
 lighting_hardware_stop();light_count=2;
 light_sources[0]=(LightSource){.x=120,.y=68,.radius=10};
 light_sources[1]=(LightSource){.x=120,.y=68,.radius=3};
 /* A missile leaves containment, crosses tangent pixels and exits screen. */
 for(int x=120;x<295;x++){light_sources[1].x=(int16_t)x;check_incremental();}
 for(int x=294;x>=-40;x--){light_sources[1].x=(int16_t)x;check_incremental();}
 light_count=1;check_incremental();
 /* Duplicate, zero-radius and minimum-gap rows; insertion/removal/reordering. */
 for(unsigned i=1;i<LIGHT_CAPACITY;i++){
  light_sources[i]=(LightSource){.x=(int16_t)(15+i*18),.y=(int16_t)(10+i*11),.radius=(uint8_t)(i%5)};
  light_count=i+1;check_incremental();
 }
 for(unsigned n=0;n<450;n++){
  unsigned choice=random_u()%12,i=random_u()%light_count;
  if(choice<4){light_sources[i].x+=(int)(random_u()%15)-7;light_sources[i].y+=(int)(random_u()%9)-4;}
  else if(choice==4){light_sources[i].radius=(uint8_t)(random_u()%16);}
  else if(choice==5){LightSource tmp=light_sources[i];unsigned j=random_u()%light_count;light_sources[i]=light_sources[j];light_sources[j]=tmp;}
  else if(choice==6){if(light_count>1)light_count--;}
  else if(choice==7){if(light_count<LIGHT_CAPACITY){light_sources[light_count]=(LightSource){.x=(int16_t)(cam_x+(int)(random_u()%380)-70),.y=(int16_t)(cam_y+(int)(random_u()%230)-35),.radius=(uint8_t)(random_u()%16)};light_count++;}}
  else if(choice==8){int dx=(int)(random_u()%11)-5,dy=(int)(random_u()%7)-3;cam_x+=dx;cam_y+=dy;for(unsigned j=0;j<light_count;j++){light_sources[j].x+=dx;light_sources[j].y+=dy;}}
  else if(choice==9){cam_x+=(int)(random_u()%15)-7;cam_y+=(int)(random_u()%9)-4;}
  else if(choice==10){lighting_hardware_stop();assert(!light_window_live);}
  else{game.area=1;check_incremental();assert(!light_window_live);game.area=0;}
  check_incremental();
 }
 /* Shrinking to empty, coming back and a bounded negative-camera scene. */
 light_count=0;check_incremental();light_count=1;light_sources[0]=(LightSource){.x=-100,.y=-90,.radius=15};cam_x=-220;cam_y=-158;check_incremental();
 assert(incremental_checks>900&&incremental_partial>100&&incremental_same>100);
 printf("PASS: %u incremental oracle scenes (%u bounded-row changes, %u unchanged masks); contained/offscreen transitions, source insertion/removal/reorder, camera motion, menu/town teardown, live-table immutability, IRQ restart\n",incremental_checks,incremental_partial,incremental_same);
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--output', type=Path, required=True)
    opts = parser.parse_args()
    source_root = opts.source_root.resolve()
    output = opts.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    original = (ROOT / 'tests/check-hardware-light.py').read_text()
    parsed = ast.parse(original)
    source = next(ast.literal_eval(n.value.args[0]) for n in parsed.body
                  if isinstance(n, ast.Expr) and isinstance(n.value, ast.Call)
                  and isinstance(n.value.func, ast.Attribute)
                  and n.value.func.attr == 'write_text')
    source = source.replace('#include "src/hardware_light.inc"',
                            '#include "' + str(source_root / 'src/hardware_light.inc') + '"')
    source = source.replace('int main(void){', EXTRA + '\nint main(void){')
    source = source.replace(' (void)in_window;', ' incremental_sequence();\n (void)in_window;')
    c = output / 'light-oracle.c'
    c.write_text(source)
    binary = output / 'light-oracle'
    command = [os.environ.get('CC', 'gcc'), '-std=c11', '-O2', '-g', '-Wall',
               '-Wextra', '-Werror', '-Wno-misleading-indentation',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
               '-I', str(ROOT), str(c), '-o', str(binary)]
    subprocess.run(command, check=True)
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
    run = subprocess.run([str(binary)], env=env, text=True, capture_output=True)
    (output / 'light-oracle.log').write_text(run.stdout + run.stderr)
    report = {'result': 'PASS' if run.returncode == 0 else 'FAIL',
              'source': str(source_root / 'src/hardware_light.inc'),
              'source_sha256': hashlib.sha256((source_root / 'src/hardware_light.inc').read_bytes()).hexdigest(),
              'harness_sha256': hashlib.sha256(source.encode()).hexdigest(),
              'base_harness_sha256': hashlib.sha256(original.encode()).hexdigest(),
              'compiler_command': command, 'returncode': run.returncode,
              'stdout': run.stdout, 'stderr': run.stderr}
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(run.stdout, end='')
    print(run.stderr, end='')
    assert run.returncode == 0, report


if __name__ == '__main__':
    main()
