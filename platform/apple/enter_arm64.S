.text
.p2align 2
.globl _artbox_enter_arm64
_artbox_enter_arm64:
    /* The caller's setjmp owns restoration. The fixture cannot return or
     * use x18/x27/x28; guest exit longjmps through the C dispatcher. */
    mov x16, x0
    mov x27, x1
    mov x28, x2
    mov sp, x3
    br x16
