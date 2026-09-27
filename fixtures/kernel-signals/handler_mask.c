// Original Linux/Bionic handler mask contract. SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <ucontext.h>
#if !defined(__aarch64__)
#error This fixture requires native ARM64
#endif
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"Handler synchronization must be lock-free");
#define USR1 (UINT64_C(1)<<9)
#define USR2 (UINT64_C(1)<<11)
#define TRAP (UINT64_C(1)<<4)
#define WINCH (UINT64_C(1)<<27)
#define UNMASKABLE UINT64_C(0x40100)
static const uint64_t zero=0,inside=USR1|USR2|TRAP|UNMASKABLE,usr2=USR2,winch=WINCH;
static uint64_t original,global_old;
static pid_t process_id,owner_tid;
static int *owner_errno;
static int drop_change,queued_result;
static _Atomic int ready,stage;
static volatile sig_atomic_t observed,observed_cases;
static long mask(int how,const void *input,void *output,unsigned size) {
    return syscall(SYS_rt_sigprocmask,how,input,output,size);
}
static int spin_until(int value) {
    for(unsigned n=0;n<1000000000;++n) {
        if(atomic_load_explicit(&stage,memory_order_acquire)==value) return 1;
        __asm__ volatile("yield");
    }
    return 0;
}
static void *sender(void *unused) {
    (void)unused;
    atomic_store_explicit(&ready,1,memory_order_release);
    queued_result=spin_until(1)?(int)syscall(SYS_tgkill,process_id,owner_tid,SIGUSR2):-2;
    atomic_store_explicit(&stage,2,memory_order_release);
    return NULL;
}
#define OBS(x) do { int passed=(x); ok=ok && passed; ++checks; } while(0)
static void capture(int number,siginfo_t *info,void *raw) {
    ucontext_t *context=raw;
    context->uc_mcontext.pc+=4; context->uc_mcontext.regs[0]=0x5a;
    int saved=errno,ok=1,checks=0;
    uint64_t current=0,previous=77,saved_context;
    memcpy(&saved_context,&context->uc_sigmask,8);
    OBS(number==SIGTRAP && info->si_code==TRAP_BRKPT && gettid()==owner_tid && &errno==owner_errno && saved==EDOM);
    OBS(mask(999,NULL,&current,8)==0 && current==(UINT64_MAX&~UNMASKABLE));
    OBS(saved_context==original);
    OBS(mask(999,(const void *)(uintptr_t)1,&previous,7)==-1 && errno==EINVAL && previous==77);
    OBS(mask(999,(const void *)(uintptr_t)1,&previous,8)==-1 && errno==EFAULT && previous==77);
    OBS(mask(999,&zero,&previous,8)==-1 && errno==EINVAL && previous==77);
    // Input lives in signed RO data; output lives in the stable guest RW image.
    OBS(mask(SIG_SETMASK,&inside,&global_old,8)==0 && global_old==(UINT64_MAX&~UNMASKABLE));
    OBS(mask(SIG_SETMASK,NULL,&current,8)==0 && current==(USR1|USR2|TRAP));
    OBS(mask(SIG_BLOCK,&winch,(void *)(uintptr_t)1,8)==-1 && errno==EFAULT);
    OBS(mask(0,NULL,&current,8)==0 && current==(USR1|USR2|TRAP|WINCH));
    OBS(drop_change || mask(SIG_UNBLOCK,&usr2,NULL,8)==0);
    OBS(mask(0,NULL,&current,8)==0 && current==(USR1|TRAP|WINCH));
    OBS(mask(SIG_BLOCK,&usr2,&previous,8)==0 && previous==(USR1|TRAP|WINCH));
    OBS(mask(0,NULL,&current,8)==0 && current==(USR1|USR2|TRAP|WINCH));
    atomic_store_explicit(&stage,1,memory_order_release);
    OBS(spin_until(2) && queued_result==0);
    OBS(mask(0,NULL,&current,8)==0 && current==(USR1|USR2|TRAP|WINCH));
    memcpy(&saved_context,&context->uc_sigmask,8);
    OBS(saved_context==original); // Handler syscalls do not rewrite the saved return mask.
    OBS(mask(0,NULL,(void *)&zero,8)==-1 && errno==EFAULT && global_old==(UINT64_MAX&~UNMASKABLE));
    uint64_t returned=original|USR2;
    memcpy(&context->uc_sigmask,&returned,8); // Preserve the queued signal until ordinary-context wait.
    observed=ok; observed_cases=checks; errno=saved;
}
#define REQUIRE(x) do { if(!(x)) return -__LINE__; } while(0)
int artbox_signal_mask_handler_check(int drop) {
    uint64_t saved_mask,current,returned;
    struct sigaction action={0},saved_action;
    REQUIRE(mask(0,NULL,&saved_mask,8)==0);
    original=(saved_mask|USR1)&~(USR2|TRAP);
    REQUIRE(mask(SIG_SETMASK,&original,NULL,8)==0);
    REQUIRE(sigaction(SIGTRAP,NULL,&saved_action)==0);
    action.sa_sigaction=capture; action.sa_flags=SA_SIGINFO|SA_ONSTACK;
    memset(&action.sa_mask,0xff,sizeof(action.sa_mask));
    REQUIRE(sigaction(SIGTRAP,&action,NULL)==0);
    uint64_t raw_action[4];
    REQUIRE(syscall(SYS_rt_sigaction,SIGTRAP,NULL,raw_action,8)==0 && raw_action[3]==(UINT64_MAX&~UNMASKABLE));
    process_id=getpid(); owner_tid=gettid(); owner_errno=&errno;
    drop_change=drop; observed=observed_cases=0; queued_result=-3;
    atomic_store(&ready,0); atomic_store(&stage,0);
    pthread_t worker;
    REQUIRE(pthread_create(&worker,NULL,sender,NULL)==0);
    unsigned waiting=0;
    while(!atomic_load_explicit(&ready,memory_order_acquire) && waiting++<1000000000) __asm__ volatile("yield");
    REQUIRE(atomic_load(&ready));
    errno=EDOM;
    __asm__ volatile("mov x0,#7\n\tbrk #0x42\n\tmov %0,x0" : "=&r"(returned) : : "x0","memory");
    REQUIRE(returned==0x5a && errno==EDOM && observed_cases==18);
    REQUIRE(pthread_join(worker,NULL)==0 && queued_result==0);
    REQUIRE(mask(0,NULL,&current,8)==0 && current==(original|USR2));
    int64_t timeout[2]={0,0};
    siginfo_t info;
    REQUIRE(syscall(SYS_rt_sigtimedwait,&usr2,&info,timeout,8)==SIGUSR2 && info.si_signo==SIGUSR2 && info.si_pid==process_id);
    REQUIRE(sigaction(SIGTRAP,&saved_action,NULL)==0 && mask(SIG_SETMASK,&saved_mask,NULL,8)==0);
    return observed?observed_cases:-1004;
}
