ENTRY(_start)
MEMORY {
 ROM (rx) : ORIGIN = 0x08000000, LENGTH = 32M
 RAM (rwx) : ORIGIN = 0x02000000, LENGTH = 256K
 FAST (rwx) : ORIGIN = 0x03000000, LENGTH = 24K
}
SECTIONS {
 .text : { KEEP(*(.gbaheader)) KEEP(*(.save_signature)) *(.text.start) *(.text.ui) *coop.o(.text*) *coop_wire.o(.text*) *link.o(.text*) *audio.o(.text*) *arrows.o(.text*) *progress.o(.text*) *spells.o(.text*) *items.o(.text*) *controller.o(.text*) *potions.o(.text*) *save.o(.text*) *flash.o(.text*) *monster_ai.o(.text*) *objects.o(.text*) *exploration.o(.text*) *world.o(.text*) *runtime.o(.text*) *l2.o(.text*) *l3.o(.text*) *l4.o(.text*) *dungeon.o(.text*) *dungeon_visual.o(.text*) *(.rodata*) . = ALIGN(4); } > ROM
 .iwram : { __fast_start = .; *(.iwram.coop) *(.iwram.info) *(.text*) *(.iwram.diag) . = ALIGN(4); __fast_end = .; } > FAST AT> ROM
 __fast_load = LOADADDR(.iwram);
 .data : { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > RAM AT> ROM
 __data_load = LOADADDR(.data);
 .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > RAM
 /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment*) *(.note*) }
}
