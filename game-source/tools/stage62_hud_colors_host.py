#!/usr/bin/env python3
"""Exercise the production font helpers against guarded native-address VRAM.

The helper definitions are extracted verbatim; actual packed fonts, palettes,
and LZ decoder are used. Check all VRAM bytes, gameplay actor scratch, paused
mixed-color text, and the inventory's staged map across every color transition.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--work', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
args.work.mkdir(parents=True, exist_ok=True)
args.out.mkdir(parents=True, exist_ok=True)
source = (ROOT/'src/gba_mode0.c').read_text()
helpers = source[source.index('static UI_FN unsigned info_color('):source.index('#include "../generated/dual_font.h"')]
text = source[source.index('static volatile uint16_t *hud_map='):source.index('static UI_FN void hud_title(')]
glyphs = source[source.index('static const uint8_t hud_glyph_index[256]='):]
glyphs = glyphs[:glyphs.index('};')+2]
prelude = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "lz.h"
#include "../generated/bg_water_palette.h"
#define UI_FN
static struct {unsigned area;} game;
static uint16_t base_palette[512];
static uint32_t load_buffer[4672];
static const uint8_t *raw_base[4];
static unsigned hud_transparent;
static uint8_t asset_town_font[8192],asset_dungeon_font[8192];
static unsigned copies;
static void dma(const void *from,void *to,unsigned length){memcpy(to,from,length);copies++;}
#include "hud_colors.inc"
'''
body = r'''
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static void read_asset(const char *path,void *dest,unsigned bytes){FILE *f=fopen(path,"rb");CHECK(f!=NULL);CHECK(fread(dest,1,bytes,f)==bytes);fclose(f);}
static int small(unsigned i){return (i>=0x9840&&i<0xa000)||(i>=0xc000&&i<0xc240);}
int main(void){
 uint8_t *vram=mmap((void*)0x06000000,0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 CHECK(vram==(void*)0x06000000);
 FILE *f=fopen("generated/town_font.lz","rb");CHECK(f);CHECK(fread(asset_town_font,1,sizeof(asset_town_font),f)>0);fclose(f);
 f=fopen("generated/dungeon_font.lz","rb");CHECK(f);CHECK(fread(asset_dungeon_font,1,sizeof(asset_dungeon_font),f)>0);fclose(f);
 uint8_t before[65536],scratch[sizeof(load_buffer)],white_font[2624];
 const unsigned indices[]={240,178,194,224},order[]={1,2,3,0,3,2,1,0};
 for(unsigned area=0;area<2;area++){
  game.area=area;read_asset(area?"generated/town_palette.bin":"generated/bg_palette.bin",base_palette,512);
  for(unsigned color=0;color<4;color++)CHECK(hud_color_index(color)==indices[color]);
  memset(vram,0x5a,65536);hud_transparent=1;unpack_font(area?asset_town_font:asset_dungeon_font);
  memset(load_buffer,0x73,sizeof(load_buffer));memcpy(scratch,load_buffer,sizeof(scratch));
  memcpy(before,vram,sizeof(before));
  for(unsigned k=0;k<8;k++){
   unsigned color=order[k],saved_copies=copies;for(unsigned n=0;n<4;n++)raw_base[n]=(void*)(uintptr_t)(n+1);
   hud_font_color(color);CHECK(copies==saved_copies);CHECK(!memcmp(scratch,load_buffer,sizeof(scratch)));
   for(unsigned n=0;n<4;n++)CHECK(raw_base[n]==(void*)(uintptr_t)(n+1));
   for(unsigned i=0;i<65536;i++)CHECK(vram[i]==(small(i)&&before[i]==240?indices[color]:before[i]));
   unsigned key=hud_font_ink;hud_font_color(color);CHECK(hud_font_ink==key);
  }
  /* Normal labels restore white without altering other allocations. */
  hud_text_color(4,18,"MAGIC RING",HUD_BLUE);hud_text(4,19,"TRISTRAM");
  for(unsigned i=0;i<40;i++)CHECK(!memcmp(vram+0x8000+font_tile(i)*64,before+0x8000+font_tile(i)*64,64));
  /* Inventory preserves its already staged map and original white font. */
  hud_font_color_copy((void*)0x06000000,HUD_WHITE);memcpy(white_font,vram,2624);
  for(unsigned color=0;color<4;color++){
   memset((uint8_t*)load_buffer+17408,0xb7,1280);
   for(unsigned n=0;n<4;n++)raw_base[n]=(void*)(uintptr_t)(n+1);
   hud_font_color_copy((void*)(0x06000000+41*64),color);
   for(unsigned n=0;n<4;n++)CHECK(raw_base[n]==NULL);
   CHECK(!memcmp(vram,white_font,2624));
   for(unsigned i=0;i<2624;i++)CHECK(vram[41*64+i]==(white_font[i]==240?indices[color]:white_font[i]));
   for(unsigned i=17408;i<18688;i++)CHECK(((uint8_t*)load_buffer)[i]==0xb7);
  }
  /* Paused confirmation keeps the resident menu text/font unmodified. */
  hud_transparent=0;unpack_font(area?asset_town_font:asset_dungeon_font);memcpy(before,vram,sizeof(before));
  for(unsigned color=0;color<4;color++){
   hud_text_color(3,6,"RING",color);unsigned count=copies;
   hud_text_color(3,7,"RING",color);CHECK(count==copies);
   hud_text(3,10,"DELETION IS PERMANENT");
   for(unsigned i=0;i<40;i++)CHECK(!memcmp(vram+0x8000+font_tile(i)*64,before+0x8000+font_tile(i)*64,64));
   for(unsigned i=0;i<2624;i++)CHECK(vram[0xc240+i]==(white_font[i]==240?indices[color]:white_font[i]));
   CHECK(hud_map[6*32+3]==266+('R'-'A'));CHECK(hud_map[10*32+3]==font_tile('D'-'A'));
  }
 }
 printf("{\"result\":\"PASS\",\"checks\":%u,\"scene_palettes\":2,\"colors\":4,\"cache_bytes\":%zu}\n",checks,sizeof(hud_font_ink));
 return 0;
}
'''
test_file = args.work/'hud-colors.c'
test_file.write_text(prelude+glyphs+helpers+text+body)
binary = args.work/'hud-colors'
subprocess.run(['gcc','-std=c11','-O1','-g','-fsanitize=undefined','-fno-sanitize-recover=all','-Wno-int-to-pointer-cast','-I'+str(ROOT/'src'),str(test_file),str(ROOT/'src/lz.c'),'-o',str(binary)],check=True)
result = subprocess.run([str(binary)],cwd=ROOT,check=True,capture_output=True,text=True)
report = json.loads(result.stdout)
report['sources']={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in ['src/gba_mode0.c','src/hud_colors.inc','src/lz.c']}
(args.out/'host-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(result.stdout.strip())
