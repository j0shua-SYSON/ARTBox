ENTRY(_start)
PHDRS { image PT_LOAD FLAGS(5); }
SECTIONS {
    . = 0x4000;
    .text : { *(.text.start) *(.text) } :image
    .rodata : { *(.rodata) } :image
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
