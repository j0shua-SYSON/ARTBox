// Original ARTBox TLSDESC bridge, MIT. No host TP access or runtime codegen.
// Preserve all non-result GPRs and full SIMD registers across the Bionic call.
.text
.p2align 2
.global artbox_tlsdesc_absolute
.type artbox_tlsdesc_absolute,%function
artbox_tlsdesc_absolute:
    .cfi_startproc
    sub sp, sp, #672
    .cfi_def_cfa_offset 672
    stp x29, x30, [sp]
    .cfi_offset x29, -672
    .cfi_offset x30, -664
    stp x1, x2, [sp, #16]
    stp x3, x4, [sp, #32]
    stp x5, x6, [sp, #48]
    stp x7, x8, [sp, #64]
    stp x9, x10, [sp, #80]
    stp x11, x12, [sp, #96]
    stp x13, x14, [sp, #112]
    stp x15, x16, [sp, #128]
    str x17, [sp, #144]
    mrs x1, NZCV
    str x1, [sp, #152]
    stp q0, q1, [sp, #160]
    stp q2, q3, [sp, #192]
    stp q4, q5, [sp, #224]
    stp q6, q7, [sp, #256]
    stp q8, q9, [sp, #288]
    stp q10, q11, [sp, #320]
    stp q12, q13, [sp, #352]
    stp q14, q15, [sp, #384]
    stp q16, q17, [sp, #416]
    stp q18, q19, [sp, #448]
    stp q20, q21, [sp, #480]
    stp q22, q23, [sp, #512]
    stp q24, q25, [sp, #544]
    stp q26, q27, [sp, #576]
    stp q28, q29, [sp, #608]
    stp q30, q31, [sp, #640]
    ldr x0, [x0, #8]
    ldr x1, [x0]
    cbnz x1, 1f
    ldr x0, [x0, #8]
    b 2f
1:
    bl __tls_get_addr
2:
    ldp q0, q1, [sp, #160]
    ldp q2, q3, [sp, #192]
    ldp q4, q5, [sp, #224]
    ldp q6, q7, [sp, #256]
    ldp q8, q9, [sp, #288]
    ldp q10, q11, [sp, #320]
    ldp q12, q13, [sp, #352]
    ldp q14, q15, [sp, #384]
    ldp q16, q17, [sp, #416]
    ldp q18, q19, [sp, #448]
    ldp q20, q21, [sp, #480]
    ldp q22, q23, [sp, #512]
    ldp q24, q25, [sp, #544]
    ldp q26, q27, [sp, #576]
    ldp q28, q29, [sp, #608]
    ldp q30, q31, [sp, #640]
    ldr x1, [sp, #152]
    msr NZCV, x1
    ldr x17, [sp, #144]
    ldp x1, x2, [sp, #16]
    ldp x3, x4, [sp, #32]
    ldp x5, x6, [sp, #48]
    ldp x7, x8, [sp, #64]
    ldp x9, x10, [sp, #80]
    ldp x11, x12, [sp, #96]
    ldp x13, x14, [sp, #112]
    ldp x15, x16, [sp, #128]
    ldp x29, x30, [sp]
    add sp, sp, #672
    .cfi_def_cfa_offset 0
    ret
    .cfi_endproc
.size artbox_tlsdesc_absolute, .-artbox_tlsdesc_absolute
.section .note.GNU-stack,"",%progbits
