// Original Linux/Bionic asynchronous interruption contract. SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#if !defined(__aarch64__)
#error This fixture requires native ARM64
#endif
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"Handler observations require lock-free atomics");
#define REQUIRE(x) do { if(!(x)) return -__LINE__; } while(0)
static const uint64_t interruption=UINT64_C(1)<<33,usr1=UINT64_C(1)<<9,usr2=UINT64_C(1)<<11;
static uint64_t expected_mask;
static pid_t expected_pid,expected_tid;
static uintptr_t stack_top;
static int *expected_errno;
static int errno_value,wait_result;
static _Atomic unsigned handled,bad,phase;
static _Atomic int futex_word;
static long mask(int how,const uint64_t *input,uint64_t *output) {
    return syscall(SYS_rt_sigprocmask,how,input,output,8);
}
static uint64_t now(void) {
    struct timespec time;
    return clock_gettime(CLOCK_MONOTONIC,&time) ? 0 : (uint64_t)time.tv_sec*UINT64_C(1000000000)+(uint64_t)time.tv_nsec;
}
static void capture(int number) {
    int saved=errno;
    unsigned char local;
    uint64_t current=0;
    const unsigned active=atomic_load_explicit(&phase,memory_order_acquire);
    if(active<=1 && (number!=34 || getpid()!=expected_pid || gettid()!=expected_tid ||
       &errno!=expected_errno || (saved!=errno_value && !(active==1 && saved==EINTR)) ||
       mask(0,NULL,&current) || current!=expected_mask ||
       (uintptr_t)&local>=stack_top || stack_top-(uintptr_t)&local>1024*1024))
        atomic_fetch_add_explicit(&bad,1,memory_order_relaxed);
    atomic_fetch_add_explicit(&handled,1,memory_order_release);
    errno=saved;
}
static void *waiter(void *unused) {
    (void)unused;
    unsigned char local;
    expected_tid=gettid(); expected_errno=&errno; errno_value=ERANGE;
    stack_top=(uintptr_t)&local+1;
    struct timespec timeout={2,0};
    errno=ERANGE;
    atomic_store_explicit(&phase,1,memory_order_release);
    long result=syscall(SYS_futex,&futex_word,128,0,&timeout,0,0);
    wait_result=result==-1 && errno==EINTR;
    atomic_store_explicit(&phase,2,memory_order_release);
    return NULL;
}
int artbox_signal_interrupt_check(int drop_send) {
    uint64_t saved_mask,working,current;
    struct sigaction action={0},saved_action;
    unsigned char local;
    REQUIRE(mask(0,NULL,&saved_mask)==0 && sigaction(34,NULL,&saved_action)==0);
    action.sa_handler=capture;
    REQUIRE(sigemptyset(&action.sa_mask)==0 && sigaddset(&action.sa_mask,SIGUSR2)==0);
    REQUIRE(sigaction(34,&action,NULL)==0);
    working=saved_mask|usr1|interruption;
    REQUIRE(mask(SIG_SETMASK,&working,NULL)==0);
    expected_mask=working|usr2;
    expected_pid=getpid(); expected_tid=gettid(); expected_errno=&errno; errno_value=EDOM;
    stack_top=(uintptr_t)&local+1;
    atomic_store(&handled,0); atomic_store(&bad,0); atomic_store(&phase,0); atomic_store(&futex_word,0);
    for(unsigned i=0;i<3;++i)
        if(!(drop_send && i==1)) REQUIRE(syscall(SYS_tgkill,expected_pid,expected_tid,34)==0);
    REQUIRE(atomic_load(&handled)==0); // Blocked realtime signals must remain queued.
    errno=EDOM;
    REQUIRE(mask(SIG_UNBLOCK,&interruption,NULL)==0 && errno==EDOM);
    // Linux delivers before returning to userspace; the Apple transport also
    // drains the accepted count before returning from the mask operation.
    const int queued=atomic_load(&handled)==3 && atomic_load(&bad)==0;
    REQUIRE(mask(0,NULL,&current)==0 && current==(working&~interruption));
    if(drop_send) {
        REQUIRE(sigaction(34,&saved_action,NULL)==0 && mask(SIG_SETMASK,&saved_mask,NULL)==0);
        return queued ? 4 : -1008;
    }
    REQUIRE(queued);
    pthread_t worker;
    REQUIRE(pthread_create(&worker,NULL,waiter,NULL)==0);
    const uint64_t started=now(),deadline=started+UINT64_C(3000000000);
    REQUIRE(started);
    while(!atomic_load_explicit(&phase,memory_order_acquire) && now()<deadline) __asm__ volatile("yield");
    REQUIRE(atomic_load(&phase)==1);
    uint64_t next=0;
    unsigned sent=0;
    while(atomic_load_explicit(&phase,memory_order_acquire)==1 && now()<deadline) {
        uint64_t time=now();
        if(time>=next) {
            int error=pthread_kill(worker,34);
            REQUIRE(error==0 || error==EAGAIN);
            if(!error) ++sent;
            next=time+UINT64_C(1000000);
        }
        __asm__ volatile("yield");
    }
    REQUIRE(pthread_join(worker,NULL)==0 && atomic_load(&phase)==2);
    REQUIRE(sent && wait_result && atomic_load(&handled)>3 && atomic_load(&bad)==0);
    REQUIRE(mask(0,NULL,&current)==0 && current==(working&~interruption));
    REQUIRE(sigaction(34,&saved_action,NULL)==0 && mask(SIG_SETMASK,&saved_mask,NULL)==0);
    return 4;
}
