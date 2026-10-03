.text
.p2align 2
.globl _artbox_call7
_artbox_call7:
    mov x16, x0
    mov x0, x1
    mov x1, x2
    mov x2, x3
    mov x3, x4
    mov x4, x5
    mov x5, x6
    mov x6, x7
    br x16

.p2align 2
.globl _artbox_call_on_stack
_artbox_call_on_stack:
    // x19 is callee-saved by both ABIs and retains our host stack frame.
    stp x19, x20, [sp, #-32]!
    stp x29, x30, [sp, #16]
    mov x19, sp
    mov x29, sp
    mov x16, x0
    mov x0, x1
    mov x1, x2
    mov x2, x3
    mov sp, x4
    blr x16
    mov sp, x19
    ldp x29, x30, [sp, #16]
    ldp x19, x20, [sp], #32
    ret
