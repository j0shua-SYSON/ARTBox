PHDRS {
    image PT_LOAD FILEHDR PHDRS FLAGS(5);
    writable PT_LOAD FLAGS(6);
    dynamic PT_DYNAMIC FLAGS(6);
    stack PT_GNU_STACK FLAGS(6);
}
SECTIONS {
    . = SIZEOF_HEADERS;
    .text : ALIGN(16) { *(.text .text.*) *(.plt) } :image
    .rodata : ALIGN(16) { *(.rodata .rodata.*) } :image
    .eh_frame_hdr : { *(.eh_frame_hdr) } :image
    .eh_frame : { *(.eh_frame) } :image
    .hash : { *(.hash) } :image
    .gnu.hash : { *(.gnu.hash) } :image
    .dynsym : { *(.dynsym) } :image
    .dynstr : { *(.dynstr) } :image
    .gnu.version : { *(.gnu.version) } :image
    .gnu.version_d : { *(.gnu.version_d) } :image
    .gnu.version_r : { *(.gnu.version_r) } :image
    .rela.dyn : { *(.rela.dyn) } :image
    .rela.plt : { *(.rela.plt) } :image
    .relr.dyn : { *(.relr.dyn) } :image
    .note : { *(.note.*) } :image
    . = ALIGN(16384);
    .dynamic : { *(.dynamic) } :writable :dynamic
    .got : { *(.got .got.plt) } :writable
    .data : { *(.data .data.*) } :writable
    .init_array : { KEEP(*(.init_array .init_array.*)) } :writable
    .fini_array : { KEEP(*(.fini_array .fini_array.*)) } :writable
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } :writable
}
