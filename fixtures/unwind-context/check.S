// SPDX-License-Identifier: MIT
// Exercise the real LLVM save/restore routines. A poisoned saved x18 must never
// replace the live platform register. The adjacent x19 and d8 must be restored.
// The original negative control is Linux-only and repairs x18 before returning.
.text
.p2align 2
.global artbox_check_unwind_context
.type artbox_check_unwind_context, %function
artbox_check_unwind_context:
    sub sp, sp, #576
    stp x29, x30, [sp]
    stp x19, x18, [sp, #16]
    str d8, [sp, #32]
    mov x29, sp
    add x0, sp, #48
    bl unw_getcontext
    cbnz w0, .Lsave_failed
    mvn x9, x18
    str x9, [sp, #192]  // context x18
    mov x9, #0x19bc
    str x9, [sp, #200]  // context x19
    mov x9, #0xd8d8
    str x9, [sp, #384]  // context d8
    adr x9, .Lresumed
    str x9, [sp, #304]  // context PC
    add x0, sp, #48
    bl __libunwind_Registers_arm64_jumpto
    mov w0, #32
    b .Lfinish
.Lresumed:
    ldr x9, [sp, #24]
    cmp x18, x9
    cset w0, ne
    mov x9, #0x19bc
    cmp x19, x9
    cset w10, ne
    orr w0, w0, w10, lsl #1
    fmov x9, d8
    mov x10, #0xd8d8
    cmp x9, x10
    cset w10, ne
    orr w0, w0, w10, lsl #2
    mov x9, sp
    cmp x29, x9
    cset w10, ne
    orr w0, w0, w10, lsl #3
    b .Lfinish
.Lsave_failed:
    mov w0, #16
.Lfinish:
    ldr d8, [sp, #32]
    ldr x19, [sp, #16]
#ifdef ARTBOX_LINUX_UNWIND_NEGATIVE
    ldr x18, [sp, #24]
#endif
    ldp x29, x30, [sp]
    add sp, sp, #576
    ret
.size artbox_check_unwind_context, .-artbox_check_unwind_context
.section .note.GNU-stack,"",%progbits
