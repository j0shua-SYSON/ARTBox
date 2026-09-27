/* SPDX-License-Identifier: MIT */
.text
.global artbox_vfork_artbox_bionic_syscall
.hidden artbox_vfork_artbox_bionic_syscall
.hidden artbox_vfork_capture
.hidden artbox_vfork_poison_thread
.type artbox_vfork_artbox_bionic_syscall, %function
artbox_vfork_artbox_bionic_syscall:
    .cfi_startproc
    stp x29, x30, [sp, #-16]!
    .cfi_def_cfa_offset 16
    .cfi_offset x29, -16
    .cfi_offset x30, -8
    mov x29, sp
    bl artbox_vfork_capture
    /* These are caller-saved registers. Point x9 into owned poison storage. */
    adrp x9, artbox_vfork_poison_thread
    add x9, x9, :lo12:artbox_vfork_poison_thread
    mov w10, #0x4321
    ldp x29, x30, [sp], #16
    .cfi_def_cfa_offset 0
    .cfi_restore x29
    .cfi_restore x30
    ret
    .cfi_endproc
.size artbox_vfork_artbox_bionic_syscall, .-artbox_vfork_artbox_bionic_syscall
.section .note.GNU-stack,"",%progbits
