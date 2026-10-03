// Original Linux/Bionic alternate-stack delivery contract. SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <ucontext.h>
#if !defined(__aarch64__)
#error This fixture requires native ARM64
#endif
static uintptr_t expected_base;
static size_t expected_size;
static int expected_on;
static pid_t expected_tid;
static int *expected_errno;
static volatile sig_atomic_t observed;
static int contains(uintptr_t address,size_t length) {
    return address>=expected_base && address-expected_base<=expected_size &&
        length<=expected_size-(address-expected_base);
}
static void capture(int number,siginfo_t *info,void *raw) {
    ucontext_t *context=raw;
    context->uc_mcontext.pc+=4;
    context->uc_mcontext.regs[0]=0x5a;
    int saved=errno;
    char marker;
    stack_t stack,disabled={.ss_flags=SS_DISABLE};
    int ok=number==SIGTRAP && info->si_signo==SIGTRAP && info->si_code==TRAP_BRKPT &&
        gettid()==expected_tid && &errno==expected_errno && saved==EDOM &&
        contains((uintptr_t)&marker,1)==expected_on && contains((uintptr_t)raw,sizeof(*context))==expected_on &&
        sigaltstack(NULL,&stack)==0 && stack.ss_sp==(void *)expected_base && stack.ss_size==expected_size &&
        stack.ss_flags==(expected_on?SS_ONSTACK:0) &&
        context->uc_stack.ss_sp==(void *)expected_base && context->uc_stack.ss_size==expected_size &&
        context->uc_stack.ss_flags==0;
    if(expected_on) ok=ok && sigaltstack((const stack_t *)(uintptr_t)1,NULL)==-1 && errno==EFAULT &&
        sigaltstack(&disabled,NULL)==-1 && errno==EPERM;
    uint64_t mask=0;
    ok=ok && syscall(SYS_rt_sigprocmask,SIG_SETMASK,NULL,&mask,8)==0 && (mask&(UINT64_C(1)<<(SIGTRAP-1)));
    observed=ok;
    errno=saved;
}
static int probe(void *stack,size_t size,int on,uintptr_t low_sp) {
    expected_base=(uintptr_t)stack; expected_size=size; expected_on=on;
    expected_tid=gettid(); expected_errno=&errno; observed=0; errno=EDOM;
    uint64_t value;
    if(low_sp) {
        // No C frame is created while SP is near the lower stack boundary.
        // The kernel/host callback and guest handler must use alternate storage.
        __asm__ volatile("mov x19,sp\n\tmov sp,%1\n\tmov x0,#7\n\tbrk #0x42\n\t"
            "mov sp,x19\n\tmov %0,x0" : "=&r"(value) : "r"(low_sp) : "x0","x19","memory");
    } else {
        __asm__ volatile("mov x0,#7\n\tbrk #0x42\n\tmov %0,x0" : "=&r"(value) : : "x0","memory");
    }
    return observed && value==0x5a && errno==EDOM ? 0 : -1001;
}
#define CHECK(x) do { if(!(x)) return -__LINE__; ++cases; } while(0)
static int child_contract(void *parent_stack) {
    unsigned cases=0;
    stack_t old,observed_stack;
    // Bionic installs its own stack after clone; Linux libc may leave it disabled.
    CHECK(sigaltstack(NULL,&old)==0 && (old.ss_flags==SS_DISABLE ||
        (old.ss_flags==0 && old.ss_sp!=parent_stack)));
    void *memory=malloc(32768);
    CHECK(memory!=NULL);
    stack_t stack={.ss_sp=memory,.ss_size=32768};
    CHECK(sigaltstack(&stack,NULL)==0);
    CHECK(probe(memory,32768,1,0)==0);
    CHECK(sigaltstack(&old,NULL)==0);
    CHECK(sigaltstack(NULL,&observed_stack)==0 && observed_stack.ss_flags==old.ss_flags &&
        observed_stack.ss_sp==old.ss_sp && observed_stack.ss_size==old.ss_size);
    free(memory);
    return (int)cases;
}
static void *child(void *parent_stack) { return (void *)(intptr_t)child_contract(parent_stack); }
int artbox_signal_stack_handler_check(int drop_onstack,uintptr_t stack_base,size_t stack_size) {
    if(stack_size<32768 || stack_base>UINTPTR_MAX-512 || (stack_base&15)) return -1003;
    unsigned cases=0;
    stack_t saved,old,observed_stack,disabled={.ss_flags=SS_DISABLE};
    struct sigaction action={0},saved_action;
    CHECK(sigaltstack(NULL,&saved)==0 && (saved.ss_flags==SS_DISABLE ||
        (!saved.ss_flags && saved.ss_sp && saved.ss_size)));
    CHECK(sigaction(SIGTRAP,NULL,&saved_action)==0);
    void *memory=malloc(32768);
    CHECK(memory!=NULL);
    stack_t stack={.ss_sp=memory,.ss_size=32768};
    CHECK(sigaltstack(&stack,&old)==0 && old.ss_flags==saved.ss_flags &&
        old.ss_sp==saved.ss_sp && old.ss_size==saved.ss_size);
    action.sa_sigaction=capture; action.sa_flags=SA_SIGINFO|(drop_onstack?0:SA_ONSTACK);
    CHECK(sigemptyset(&action.sa_mask)==0 && sigaction(SIGTRAP,&action,NULL)==0);
    int result=probe(memory,32768,1,0);
    if(drop_onstack) {
        if(sigaction(SIGTRAP,&saved_action,NULL) || sigaltstack(&saved,NULL)) return -1002;
        free(memory);
        return result;
    }
    CHECK(result==0);
    CHECK(probe(memory,32768,1,stack_base+512)==0);
    CHECK(sigaltstack(NULL,&observed_stack)==0 && observed_stack.ss_sp==memory && observed_stack.ss_flags==0);
    action.sa_flags=SA_SIGINFO;
    CHECK(sigaction(SIGTRAP,&action,NULL)==0);
    CHECK(probe(memory,32768,0,0)==0); // Enabled alternate storage does not force its use.
    action.sa_flags|=SA_ONSTACK;
    CHECK(sigaction(SIGTRAP,&action,NULL)==0);
    pthread_t worker;
    CHECK(pthread_create(&worker,NULL,child,memory)==0);
    void *child_result=NULL;
    CHECK(pthread_join(worker,&child_result)==0 && (intptr_t)child_result==6);
    CHECK(sigaltstack(NULL,&observed_stack)==0 && observed_stack.ss_sp==memory && observed_stack.ss_size==32768);
    CHECK(sigaltstack(&disabled,NULL)==0);
    CHECK(sigaltstack(NULL,&observed_stack)==0 && observed_stack.ss_flags==SS_DISABLE);
    CHECK(sigaction(SIGTRAP,&saved_action,NULL)==0);
    CHECK(sigaltstack(&saved,NULL)==0 && sigaltstack(NULL,&observed_stack)==0 &&
        observed_stack.ss_flags==saved.ss_flags && observed_stack.ss_sp==saved.ss_sp && observed_stack.ss_size==saved.ss_size);
    free(memory);
    return (int)cases+(int)(intptr_t)child_result;
}
