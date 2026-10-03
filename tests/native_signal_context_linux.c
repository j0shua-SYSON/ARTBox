// Original native kernel-frame comparison. SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include "artbox/signal_context.h"
#include <asm/sigcontext.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#if !defined(__aarch64__) || !defined(__linux__)
#error Native Linux ARM64 is required
#endif
_Static_assert(sizeof(ucontext_t)==ARTBOX_ARM64_UCONTEXT_BYTES,"Linux ARM64 ucontext size");
_Static_assert(offsetof(ucontext_t,uc_sigmask)==40,"Linux signal mask offset");
_Static_assert(offsetof(ucontext_t,uc_mcontext)==176,"Linux machine context offset");
_Static_assert(offsetof(ucontext_t,uc_mcontext.regs)==184,"Linux general register offset");
_Static_assert(offsetof(ucontext_t,uc_mcontext.sp)==432,"Linux SP offset");
_Static_assert(offsetof(ucontext_t,uc_mcontext.pc)==440,"Linux PC offset");
_Static_assert(offsetof(ucontext_t,uc_mcontext.__reserved)==464,"Linux context record offset");
_Static_assert(sizeof(struct fpsimd_context)==528,"Linux FPSIMD context size");
_Static_assert(sizeof(struct esr_context)==16,"Linux ESR context size");
static volatile sig_atomic_t observation,drop_register_edit;
#define OBSERVE(x) do { if(!(x)) { observation=-__LINE__; return; } } while(0)
static void handler(int number,siginfo_t *info,void *raw) {
    ucontext_t *kernel=raw;
    // Always advance the deliberate BRK, including failed probes, so a bad
    // assertion is observable without an endless synchronous signal loop.
    uint64_t interrupted_pc=kernel->uc_mcontext.pc;
    kernel->uc_mcontext.pc+=4;
    OBSERVE(number==SIGTRAP && info->si_signo==SIGTRAP && info->si_code==TRAP_BRKPT);
    artbox_arm64_signal_state before={0},after;
    artbox_signal_frame_info meta={0};
    memcpy(before.x,kernel->uc_mcontext.regs,sizeof(before.x));
    before.pc=interrupted_pc; before.sp=kernel->uc_mcontext.sp;
    before.pstate=kernel->uc_mcontext.pstate; before.fault_address=kernel->uc_mcontext.fault_address;
    memcpy(&meta.mask,&kernel->uc_sigmask,sizeof(meta.mask));
    meta.stack_address=(uintptr_t)kernel->uc_stack.ss_sp;
    meta.stack_size=kernel->uc_stack.ss_size; meta.stack_flags=(uint32_t)kernel->uc_stack.ss_flags;
    unsigned char *reserved=kernel->uc_mcontext.__reserved;
    struct fpsimd_context *native_fp=NULL;
    for(size_t offset=0;offset+16<=sizeof(kernel->uc_mcontext.__reserved);) {
        struct _aarch64_ctx head;
        memcpy(&head,reserved+offset,sizeof(head));
        if(!head.magic && !head.size) break;
        OBSERVE(head.size>=16 && !(head.size&15) && head.size<=sizeof(kernel->uc_mcontext.__reserved)-offset);
        if(head.magic==FPSIMD_MAGIC) {
            OBSERVE(head.size==sizeof(struct fpsimd_context) && !native_fp);
            native_fp=(void *)(reserved+offset);
            before.fpsr=native_fp->fpsr; before.fpcr=native_fp->fpcr;
            memcpy(before.v,native_fp->vregs,sizeof(before.v));
        }
        if(head.magic==ESR_MAGIC) {
            OBSERVE(head.size==sizeof(struct esr_context));
            memcpy(&before.esr,reserved+offset+8,sizeof(before.esr));
        }
        offset+=head.size;
    }
    OBSERVE(native_fp!=NULL);
    ucontext_t guest_storage;
    ucontext_t *guest=&guest_storage; // Validate using real Linux/Bionic declarations.
    OBSERVE(artbox_signal_context_encode(guest,sizeof(*guest),&before,&meta)==0);
    OBSERVE(guest->uc_mcontext.pc==before.pc && guest->uc_mcontext.sp==before.sp &&
        !memcmp(guest->uc_mcontext.regs,before.x,sizeof(before.x)));
    struct fpsimd_context *guest_fp=(void *)guest->uc_mcontext.__reserved;
    OBSERVE(guest_fp->head.magic==FPSIMD_MAGIC && guest_fp->head.size==sizeof(*guest_fp) &&
        !memcmp(guest_fp->vregs,before.v,sizeof(before.v)));
    guest->uc_mcontext.regs[0]=0x5a;
    guest->uc_mcontext.regs[18]=~before.x[18];
    guest->uc_mcontext.pc+=4;
    memset(&guest_fp->vregs[0],0x22,16);
    uint64_t mask;
    OBSERVE(artbox_signal_context_resume(guest,sizeof(*guest),&before,&after,&mask)==0);
    OBSERVE(after.x[18]==before.x[18] && after.pc==interrupted_pc+4 &&
        mask==(meta.mask&~UINT64_C(0x40100)));
    kernel->uc_mcontext.pc=after.pc;
    kernel->uc_mcontext.regs[18]=after.x[18];
    if(!drop_register_edit) {
        kernel->uc_mcontext.regs[0]=after.x[0];
        memcpy(&native_fp->vregs[0],after.v[0],16);
    }
    observation=1;
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--drop-register-edit")) drop_register_edit=1;
    else if(argc!=1) return 64;
    struct sigaction action={0},saved;
    action.sa_sigaction=handler; action.sa_flags=SA_SIGINFO;
    if(sigemptyset(&action.sa_mask) || sigaction(SIGTRAP,&action,&saved)) return 2;
    uint64_t original_x18,resumed_x18,value,vector;
    __asm__ volatile("mov %0,x18\n\tmov x0,#7\n\tmovi v0.16b,#0x11\n\tbrk #0x42\n\t"
        "mov %1,x0\n\tumov %2,v0.d[0]\n\tmov %3,x18"
        : "=&r"(original_x18),"=&r"(value),"=&r"(vector),"=&r"(resumed_x18) : : "x0","v0","memory");
    if(sigaction(SIGTRAP,&saved,NULL)) return 2;
    if(observation!=1) { fprintf(stderr,"signal context handler failed: %d\n",observation); return 1; }
    if(original_x18!=resumed_x18) { fputs("reserved register changed\n",stderr); return 1; }
    if(value!=0x5a || vector!=UINT64_C(0x2222222222222222)) {
        fputs("handler register edits were not resumed\n",stderr); return 1;
    }
    puts("{\"frame_bytes\":4560,\"native_resume\":true,\"general_register_edit\":true,\"vector_edit\":true,\"x18_preserved\":true}");
    return 0;
}
