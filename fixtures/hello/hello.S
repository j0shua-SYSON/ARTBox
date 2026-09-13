/* Original ARTBox test, MIT. Static Linux/Android AArch64 entry, no libc.
 * This ELF runs unchanged under a native Linux AArch64 kernel as an oracle.
 * Build-time packaging replaces each svc before Apple code signing. */
.section .rodata,"a",@progbits
hello_message:
    .ascii "hello from Android ARM64\n"
.equ hello_length, .-hello_message

.section .text.start,"ax",@progbits
.global _start
.type _start,@function
_start:
    mov x0, #0
    mov x1, #16384
    mov x2, #3
    mov x3, #0x22
    mov x4, #-1
    mov x5, #0
    mov x8, #222
    svc #0
    cmp x0, #0
    b.lt .Lfailed
    mov x19, x0
    mov x9, #0x1234
    str x9, [x19]
    mov x0, x19
    mov x1, #16384
    mov x2, #1
    mov x8, #226
    svc #0
    cbnz x0, .Lfailed
    ldr x9, [x19]
    /* Compare via a register because 0x1234 exceeds imm12. */
    mov x10, #0x1234
    cmp x9, x10
    b.ne .Lfailed
    mov x0, x19
    mov x1, #16384
    mov x8, #215
    svc #0
    cbnz x0, .Lfailed
    mov x0, #1
    adr x1, hello_message
    mov x2, #hello_length
    mov x8, #64
    svc #0
    cmp x0, x2
    b.ne .Lfailed
    mov x0, #0
    mov x8, #93
    svc #0
.Lfailed:
    mov x0, #1
    mov x8, #93
    svc #0
    b .Lfailed
.size _start, .-_start
