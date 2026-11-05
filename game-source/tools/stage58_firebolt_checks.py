#!/usr/bin/env python3
"""ASan/UBSan properties of production projectile presentation; no game writes."""
import argparse,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
source=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define GRAPHICS_ASSETS
#include "src/game.h"
#include "src/hazards.h"
static Game game;static unsigned coop_active;
#define UI_FN
#include "src/projectile_view.inc"
static unsigned rng=0x58a134c9,checks;
static unsigned random_u(void){rng=rng*1664525u+1013904223u;return rng;}
static void projection(int x,int y,int *wx,int *wy){*wx=DUNGEON_ORIGIN+((x-y)*16)/256;*wy=72+((x+y)*8)/256;}
static void endpoint(const Arrow *a,unsigned spell){
 int x,y,wx,wy;Arrow before=*a;projectile_world(a,spell,&x,&y);projection(a->x,a->y,&wx,&wy);
 assert(x==wx&&y==wy);assert(!memcmp(&before,a,sizeof(*a)));checks++;
}
static void bounded(const Arrow *a,unsigned spell){
 int x,y,start_x,start_y,end_x,end_y;Arrow before=*a;projectile_world(a,spell,&x,&y);
 projection((int)a->x-a->vx,(int)a->y-a->vy,&start_x,&start_y);projection(a->x,a->y,&end_x,&end_y);
 int lo=start_x<end_x?start_x:end_x,hi=start_x>end_x?start_x:end_x;assert(x>=lo-1&&x<=hi+1);
 lo=start_y<end_y?start_y:end_y;hi=start_y>end_y?start_y:end_y;assert(y>=lo-1&&y<=hi+1);
 assert(!memcmp(&before,a,sizeof(*a)));checks++;
}
int main(void){
 const unsigned moving[]={0,SPELL_FIREBOLT,SPELL_LIGHTNING,SPELL_FIREBALL,SPELL_CHAIN,SPELL_WAVE,SPELL_NOVA,SPELL_ELEMENTAL,SPELL_CHARGED,SPELL_HOLY,SPELL_BLOOD_STAR,SPELL_BONE_SPIRIT};
 const unsigned stationary[]={SPELL_FIREWALL,SPELL_INFERNO,SPELL_GUARDIAN,SPELL_GOLEM,SPELL_STONE,SPELL_FLASH,SPELL_APOCALYPSE};
 for(unsigned i=0;i<20000;i++){
  Arrow a={0};a.x=256+(random_u()%19000);a.y=256+(random_u()%19000);a.vx=(int)(random_u()%1281)-640;a.vy=(int)(random_u()%1281)-640;a.life=2;a.start_x=a.start_y=1;
  unsigned spell=moving[random_u()%(sizeof(moving)/sizeof(*moving))];
  for(projectile_phase=0;projectile_phase<3;projectile_phase++){
   if(!projectile_phase)endpoint(&a,spell);else bounded(&a,spell);
   coop_active=1;endpoint(&a,spell);coop_active=0;game.paused=1;endpoint(&a,spell);game.paused=0;
   for(unsigned j=0;j<sizeof(stationary)/sizeof(*stationary);j++)endpoint(&a,stationary[j]);
   a.direction=HOSTILE_PROJECTILE|HOSTILE_BURST;endpoint(&a,spell);a.direction=0;
  }
 }
 /* New/reused slots remain at their source until a real simulation move.
  * Zero velocity and static golem HP stored in vx never become fake motion. */
 Arrow a={.x=25*256,.y=31*256,.vx=128,.vy=-96,.start_x=25,.start_y=31,.life=255,.direction=HOSTILE_PROJECTILE};
 for(projectile_phase=0;projectile_phase<3;projectile_phase++)endpoint(&a,SPELL_FIREBOLT);
 a.vx=a.vy=0;for(projectile_phase=0;projectile_phase<3;projectile_phase++)endpoint(&a,SPELL_FIREBOLT);
 a.vx=30000;for(projectile_phase=0;projectile_phase<3;projectile_phase++)endpoint(&a,SPELL_GOLEM);
 /* A9px/tick Firebolt produces bounded small steps across consecutive ticks.
  * One tick's endpoint is the next tick's start: no forward prediction. */
 a=(Arrow){.x=9*256,.y=41*256,.vx=128,.vy=0,.start_x=9,.start_y=41,.life=2,.direction=128};
 int old_x,old_y;projection(a.x,a.y,&old_x,&old_y);
 for(unsigned tick=0;tick<30;tick++){
  a.x+=a.vx;
  for(unsigned sample=1;sample<=3;sample++){
   projectile_phase=sample%3;int x,y;projectile_world(&a,SPELL_FIREBOLT,&x,&y);
   assert(x>=old_x&&x-old_x<=3);assert(y>=old_y&&y-old_y<=2);old_x=x;old_y=y;checks++;
  }
 }
 printf("PASS: %u projectile presentation properties; endpoint, bounded backward interpolation, all moving spells, negative slopes, source birth/reuse, static/golem/burst, pause/co-op, byte-identical Arrow state\n",checks);
}
'''
c=a.output/'projectile-view.c';c.write_text(source);exe=a.output/'projectile-view'
subprocess.run(['gcc','-std=c11','-O2','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(a.source_root.resolve()),str(c),'-o',str(exe)],check=True)
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1');result=subprocess.run([str(exe)],capture_output=True,text=True,env=env,check=True);(a.output/'projectile-view.txt').write_text(result.stdout+result.stderr);print(result.stdout,end='')
(a.output/'host-summary.json').write_text(json.dumps({'result':'PASS','sanitizers':['address','undefined'],'source':str(a.source_root.resolve()),'output':result.stdout.strip()},indent=2)+'\n')
