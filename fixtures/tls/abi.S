// Original TLSDESC register-preservation acceptance, MIT.
.text
.p2align 2
.global artbox_tls_abi_check
.type artbox_tls_abi_check,%function
artbox_tls_abi_check:
    sub sp, sp, #672
    stp x29, x30, [sp, #128]
    stp q8, q9, [sp, #0]
    stp q10, q11, [sp, #32]
    stp q12, q13, [sp, #64]
    stp q14, q15, [sp, #96]
    adrp x0, :tlsdesc:artbox_tls_initialized
    ldr x1, [x0, :tlsdesc_lo12:artbox_tls_initialized]
    add x0, x0, :tlsdesc_lo12:artbox_tls_initialized
    mov x2, #0x42
    mov x3, #0x42
    mov x4, #0x42
    mov x5, #0x42
    mov x6, #0x42
    mov x7, #0x42
    mov x8, #0x42
    mov x9, #0x42
    mov x10, #0x42
    mov x11, #0x42
    mov x12, #0x42
    mov x13, #0x42
    mov x14, #0x42
    mov x15, #0x42
    mov x16, #0x42
    mov x17, #0x42
    movi v0.16b, #0x5a
    movi v1.16b, #0x5a
    movi v2.16b, #0x5a
    movi v3.16b, #0x5a
    movi v4.16b, #0x5a
    movi v5.16b, #0x5a
    movi v6.16b, #0x5a
    movi v7.16b, #0x5a
    movi v8.16b, #0x5a
    movi v9.16b, #0x5a
    movi v10.16b, #0x5a
    movi v11.16b, #0x5a
    movi v12.16b, #0x5a
    movi v13.16b, #0x5a
    movi v14.16b, #0x5a
    movi v15.16b, #0x5a
    movi v16.16b, #0x5a
    movi v17.16b, #0x5a
    movi v18.16b, #0x5a
    movi v19.16b, #0x5a
    movi v20.16b, #0x5a
    movi v21.16b, #0x5a
    movi v22.16b, #0x5a
    movi v23.16b, #0x5a
    movi v24.16b, #0x5a
    movi v25.16b, #0x5a
    movi v26.16b, #0x5a
    movi v27.16b, #0x5a
    movi v28.16b, #0x5a
    movi v29.16b, #0x5a
    movi v30.16b, #0x5a
    movi v31.16b, #0x5a
    .tlsdesccall artbox_tls_initialized
    blr x1
    mov w0, #1
    cmp x2, #0x42
    b.ne 3f
    cmp x3, #0x42
    b.ne 3f
    cmp x4, #0x42
    b.ne 3f
    cmp x5, #0x42
    b.ne 3f
    cmp x6, #0x42
    b.ne 3f
    cmp x7, #0x42
    b.ne 3f
    cmp x8, #0x42
    b.ne 3f
    cmp x9, #0x42
    b.ne 3f
    cmp x10, #0x42
    b.ne 3f
    cmp x11, #0x42
    b.ne 3f
    cmp x12, #0x42
    b.ne 3f
    cmp x13, #0x42
    b.ne 3f
    cmp x14, #0x42
    b.ne 3f
    cmp x15, #0x42
    b.ne 3f
    cmp x16, #0x42
    b.ne 3f
    cmp x17, #0x42
    b.ne 3f
    stp q0, q1, [sp, #144]
    stp q2, q3, [sp, #176]
    stp q4, q5, [sp, #208]
    stp q6, q7, [sp, #240]
    stp q8, q9, [sp, #272]
    stp q10, q11, [sp, #304]
    stp q12, q13, [sp, #336]
    stp q14, q15, [sp, #368]
    stp q16, q17, [sp, #400]
    stp q18, q19, [sp, #432]
    stp q20, q21, [sp, #464]
    stp q22, q23, [sp, #496]
    stp q24, q25, [sp, #528]
    stp q26, q27, [sp, #560]
    stp q28, q29, [sp, #592]
    stp q30, q31, [sp, #624]
    mov w0, #2
    add x2, sp, #144
    mov x3, #64
    mov x4, #0x5a5a
    movk x4, #0x5a5a, lsl #16
    movk x4, #0x5a5a, lsl #32
    movk x4, #0x5a5a, lsl #48
1:
    ldr x1, [x2], #8
    cmp x1, x4
    b.ne 3f
    subs x3, x3, #1
    b.ne 1b
    mov w0, #0
3:
    ldp q8, q9, [sp, #0]
    ldp q10, q11, [sp, #32]
    ldp q12, q13, [sp, #64]
    ldp q14, q15, [sp, #96]
    ldp x29, x30, [sp, #128]
    add sp, sp, #672
    ret
.size artbox_tls_abi_check, .-artbox_tls_abi_check
.section .note.GNU-stack,"",%progbits
