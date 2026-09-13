// Linux-only test harness: seed callee-saved registers, preserve x18, and
// return both the result and a count of damaged register/frame values.
.text
.p2align 2
.global artbox_invoke_stub
.type artbox_invoke_stub, %function
artbox_invoke_stub:
    stp x29, x30, [sp, #-112]!
    stp x19, x20, [sp, #16]
    stp x21, x22, [sp, #32]
    stp x23, x24, [sp, #48]
    stp x25, x26, [sp, #64]
    stp x27, x28, [sp, #80]
    str x18, [sp, #96]
    mov x29, sp
    mov x16, x0
    mov x17, x1
    mov x19, #0x119
    mov x20, #0x120
    mov x21, #0x121
    mov x22, #0x122
    mov x23, #0x123
    mov x24, #0x124
    mov x25, #0x125
    mov x26, #0x126
    mov x27, #0x127
    mov x28, #0x128
    ldp x0, x1, [x17]
    ldp x2, x3, [x17, #16]
    ldp x4, x5, [x17, #32]
    ldr x6, [x17, #48]
    blr x16
    mov x1, #0
    cmp x19, #0x119
    cinc x1, x1, ne
    cmp x20, #0x120
    cinc x1, x1, ne
    cmp x21, #0x121
    cinc x1, x1, ne
    cmp x22, #0x122
    cinc x1, x1, ne
    cmp x23, #0x123
    cinc x1, x1, ne
    cmp x24, #0x124
    cinc x1, x1, ne
    cmp x25, #0x125
    cinc x1, x1, ne
    cmp x26, #0x126
    cinc x1, x1, ne
    cmp x27, #0x127
    cinc x1, x1, ne
    cmp x28, #0x128
    cinc x1, x1, ne
    mov x16, sp
    cmp x29, x16
    cinc x1, x1, ne
    ldr x16, [sp, #96]
    cmp x18, x16
    cinc x1, x1, ne
    ldp x19, x20, [sp, #16]
    ldp x21, x22, [sp, #32]
    ldp x23, x24, [sp, #48]
    ldp x25, x26, [sp, #64]
    ldp x27, x28, [sp, #80]
    ldp x29, x30, [sp], #112
    ret
.size artbox_invoke_stub, .-artbox_invoke_stub
.section .note.GNU-stack,"",%progbits
