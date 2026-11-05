#pragma once
#include "game.h"

/* Three-byte archive adapter. Normal props keep their original 7+7+6+4
 * representation. A saved x >= 80 is an escape, never a live coordinate.
 * Wall traps retain the exact target slot and spent bit without extra RAM.
 * Kinds59..63 use the normal form, including shrine animation states2..13.
 * With x < 80, saved y >= 80 independently encodes extended kinds64..95;
 * no valid live coordinate or wall-trap escape aliases this range. */
enum { PROP_ARCHIVE_WALL_FIRST=53, PROP_ARCHIVE_WALL_LAST=58,
       PROP_ARCHIVE_WALL_RECORDS=80*80*6*128,
       PROP_ARCHIVE_EXTENDED_FIRST=64,PROP_ARCHIVE_EXTENDED_LAST=95,
       PROP_ARCHIVE_EXTENDED_KINDS=32,
       PROP_ARCHIVE_EXTENDED_RECORDS=80*80*32*16 };
_Static_assert(PROP_FLAME_LEVER<=PROP_ARCHIVE_EXTENDED_LAST,"theme kind archive range");
static __attribute__((section(".text.ui"),minsize,noinline)) uint32_t game_prop_archive_pack(const Prop *p){
 if(p->x>=80||p->y>=80||p->kind>PROP_ARCHIVE_EXTENDED_LAST)return 0;
 if(p->kind>=PROP_ARCHIVE_EXTENDED_FIRST){
  if(p->state>15)return 0;
  unsigned ordinal=(((unsigned)p->y*80+p->x)*PROP_ARCHIVE_EXTENDED_KINDS+p->kind-PROP_ARCHIVE_EXTENDED_FIRST)*16+p->state;
  unsigned x=ordinal%80,upper=ordinal/80;
  return x|((80+upper%48)<<7)|((upper/48)<<14);
 }
 if(p->kind>=PROP_ARCHIVE_WALL_FIRST&&p->kind<=PROP_ARCHIVE_WALL_LAST){
  if(p->state>127)return 0;
  unsigned ordinal=(((unsigned)p->y*80+p->x)*6+p->kind-PROP_ARCHIVE_WALL_FIRST)*128+p->state;
  return ((ordinal/48)<<7)|(80+ordinal%48);
 }
 if(p->state>15)return 0;
 return p->x|((unsigned)p->y<<7)|((unsigned)p->kind<<14)|((unsigned)p->state<<20);
}
static __attribute__((section(".text.ui"),minsize,noinline)) Prop game_prop_archive_unpack(uint32_t bits){
 Prop empty={0};if(bits>>24)return empty;
 unsigned x=bits&127;
 if(x>=80){
  unsigned ordinal=(bits>>7)*48+x-80;
  if(ordinal>=PROP_ARCHIVE_WALL_RECORDS)return empty;
  unsigned state=ordinal%128;ordinal/=128;
  unsigned kind=PROP_ARCHIVE_WALL_FIRST+ordinal%6;ordinal/=6;
  return (Prop){ordinal%80,ordinal/80,kind,state};
 }
 unsigned y=(bits>>7)&127;
 if(y>=80){
  unsigned ordinal=(((bits>>14)*48+y-80)*80+x);
  if(ordinal>=PROP_ARCHIVE_EXTENDED_RECORDS)return empty;
  unsigned state=ordinal%16;ordinal/=16;
  unsigned kind=PROP_ARCHIVE_EXTENDED_FIRST+ordinal%PROP_ARCHIVE_EXTENDED_KINDS;ordinal/=PROP_ARCHIVE_EXTENDED_KINDS;
  return (Prop){ordinal%80,ordinal/80,kind,state};
 }
 Prop p={x,y,(bits>>14)&63,bits>>20};
 if(p.kind>=PROP_ARCHIVE_WALL_FIRST&&p.kind<=PROP_ARCHIVE_WALL_LAST)return empty;
 return p;
}
