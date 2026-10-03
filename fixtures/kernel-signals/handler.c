// Original Linux/Bionic signal delivery contract. SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include <asm/sigcontext.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <ucontext.h>
#if !defined(__aarch64__)
#error This fixture executes a native ARM64 breakpoint
#endif
_Static_assert(sizeof(ucontext_t)==4560,"Linux ARM64 context");
_Static_assert(sizeof(siginfo_t)==128,"Linux siginfo");
_Static_assert(offsetof(ucontext_t,uc_mcontext.pc)==440,"Linux PC offset");
static volatile sig_atomic_t delivered,observation,drop_edit;
static pid_t expected_pid,expected_tid;
static int *expected_errno;
static uint64_t original_mask;
static void capture(int number,siginfo_t *info,void *raw) {
    ucontext_t *context=raw;
    int saved=errno;
    context->uc_mcontext.pc+=4; // Failed observations must not loop on BRK.
    uint64_t mask=0;
    struct fpsimd_context *fp=(void *)context->uc_mcontext.__reserved;
    ++delivered;
    observation=number==SIGTRAP && info->si_signo==SIGTRAP && info->si_code==TRAP_BRKPT &&
        (uintptr_t)info->si_addr==context->uc_mcontext.pc-4 &&
        getpid()==expected_pid && gettid()==expected_tid && &errno==expected_errno && saved==EDOM &&
        syscall(SYS_rt_sigprocmask,SIG_SETMASK,NULL,&mask,8)==0 &&
        mask==(original_mask|(UINT64_C(1)<<(SIGTRAP-1))) &&
        fp->head.magic==FPSIMD_MAGIC && fp->head.size==sizeof(*fp);
    if(!drop_edit) {
        context->uc_mcontext.regs[0]=0x5a;
        memset(&fp->vregs[0],0x22,16);
    }
    errno=saved;
}
#define CHECK(x) do { if(!(x)) return -__LINE__; ++cases; } while(0)
int artbox_signal_handler_check(int drop) {
    unsigned cases=0;
    struct sigaction action={0},saved,observed;
    struct { uint64_t handler,flags,restorer,mask; } raw={0,0,0,0};
    CHECK(sigaction(SIGTRAP,NULL,&saved)==0);
    CHECK(syscall(SYS_rt_sigaction,SIGTRAP,NULL,NULL,0)==-1 && errno==EINVAL);
    CHECK(syscall(SYS_rt_sigaction,SIGTRAP,(void *)(uintptr_t)1,NULL,8)==-1 && errno==EFAULT);
    CHECK(syscall(SYS_rt_sigaction,0,&raw,NULL,8)==-1 && errno==EINVAL);
    CHECK(syscall(SYS_rt_sigaction,SIGKILL,NULL,&raw,8)==0 && raw.handler==0);
    CHECK(syscall(SYS_rt_sigaction,SIGKILL,&raw,NULL,8)==-1 && errno==EINVAL);
    CHECK(syscall(SYS_rt_sigaction,SIGSTOP,&raw,NULL,8)==-1 && errno==EINVAL);
    action.sa_sigaction=capture; action.sa_flags=SA_SIGINFO|SA_RESTART;
    CHECK(sigemptyset(&action.sa_mask)==0 && sigaction(SIGTRAP,&action,NULL)==0);
    CHECK(sigaction(SIGTRAP,NULL,&observed)==0 && observed.sa_sigaction==capture &&
        observed.sa_flags==action.sa_flags && sigismember(&observed.sa_mask,SIGTRAP)==0);
    raw.handler=(uintptr_t)capture; raw.flags=SA_SIGINFO;
    CHECK(syscall(SYS_rt_sigaction,SIGTRAP,&raw,(void *)(uintptr_t)1,8)==-1 && errno==EFAULT);
    CHECK(sigaction(SIGTRAP,NULL,&observed)==0 && observed.sa_sigaction==capture && observed.sa_flags==SA_SIGINFO);
    CHECK(sigaction(SIGTRAP,&action,NULL)==0);
    original_mask=0;
    CHECK(syscall(SYS_rt_sigprocmask,SIG_SETMASK,NULL,&original_mask,8)==0 &&
        !(original_mask&(UINT64_C(1)<<(SIGTRAP-1))));
    expected_pid=getpid(); expected_tid=gettid(); expected_errno=&errno;
    delivered=observation=0; drop_edit=drop; errno=EDOM;
    uint64_t value,vector;
    __asm__ volatile("mov x0,#7\n\tmovi v0.16b,#0x11\n\tbrk #0x42\n\t"
        "mov %0,x0\n\tumov %1,v0.d[0]"
        : "=&r"(value),"=&r"(vector) : : "x0","v0","memory");
    CHECK(delivered==1 && observation==1 && errno==EDOM);
    CHECK(sigaction(SIGTRAP,&saved,NULL)==0);
    CHECK(sigaction(SIGTRAP,NULL,&observed)==0 && observed.sa_handler==saved.sa_handler);
    if(value!=0x5a || vector!=UINT64_C(0x2222222222222222)) return -1000;
    return (int)cases;
}
