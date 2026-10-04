// Original Linux/Bionic native memory-fault contract. SPDX-License-Identifier: MIT
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <ucontext.h>
#if !defined(__aarch64__)
#error This fixture requires native ARM64
#endif
_Static_assert(sizeof(ucontext_t)==4560,"Linux ARM64 context");
enum { FAULTS=5 };
typedef struct observation {
    uint64_t pc,address,far;
    int number,code,alternate,mask,errno_ok;
} observation;
static observation seen[FAULTS];
static volatile sig_atomic_t active,received,handler_error,mutation;
static uintptr_t alternate_begin,alternate_end;
static uint64_t original_mask;
static pid_t expected_pid,expected_tid;
static int *expected_errno;
static void capture(int number,siginfo_t *info,void *raw) {
    int saved=errno,index=active;
    ucontext_t *context=raw;
    uint64_t pc=context->uc_mcontext.pc;
    context->uc_mcontext.pc=pc+4;
    if(index<0 || index>=FAULTS || received) { handler_error=1; errno=saved; return; }
    char marker;
    uint64_t mask=0,saved_mask=0;
    memcpy(&saved_mask,&context->uc_sigmask,8);
    int queried=syscall(SYS_rt_sigprocmask,SIG_SETMASK,NULL,&mask,8)==0;
    uintptr_t sp=(uintptr_t)&marker;
    seen[index]=(observation){pc,mutation==2?0:(uintptr_t)info->si_addr,context->uc_mcontext.fault_address,
        number,info->si_code,sp>=alternate_begin && sp<alternate_end,
        queried && number>0 && number<=64 && saved_mask==original_mask &&
        mask==(original_mask|(UINT64_C(1)<<(SIGUSR2-1))|(UINT64_C(1)<<(number-1))),
        saved==EDOM && &errno==expected_errno && getpid()==expected_pid && gettid()==expected_tid};
    if(info->si_signo!=number) handler_error=2;
    if(mutation!=1) context->uc_mcontext.regs[0]=0x5a;
    received=1;
    errno=saved;
}
typedef struct resumed { uint64_t value,pc; } resumed;
static resumed trigger(unsigned kind,uintptr_t address) {
    resumed result;
#define TRIGGER(instruction) __asm__ volatile("adr %1,1f\n\tmov x0,#7\n\t1: " instruction \
    "\n\tmov %0,x0" : "=&r"(result.value),"=&r"(result.pc) : "r"(address) : "x0","memory")
    if(kind<=1) { TRIGGER("ldr x0,[%2]"); }
    else if(kind==2) { TRIGGER("str x0,[%2]"); }
    else if(kind==3) { TRIGGER("ldxr x0,[%2]\n\tclrex"); }
    else { TRIGGER("udf #0"); }
#undef TRIGGER
    return result;
}
#define CHECK(x) do { if(!(x)) return -__LINE__; } while(0)
int artbox_signal_fault_check(int mode) {
    if(mode<0 || mode>2) return -22;
    memset(seen,0,sizeof(seen)); active=-1; received=handler_error=0; mutation=mode;
    long native_page=sysconf(_SC_PAGESIZE);
    CHECK(native_page>=32 && !(native_page&(native_page-1)));
    size_t page=(size_t)native_page,alt_bytes=(65536+page-1)&~(page-1),pool_bytes=alt_bytes+2*page;
    unsigned char *region=mmap(NULL,3*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    unsigned char *pool=mmap(NULL,pool_bytes,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(region!=MAP_FAILED && pool!=MAP_FAILED && !((uintptr_t)region&15));
    region[2*page]=0x7c;
    CHECK(!mprotect(region+page,page,PROT_NONE) && !mprotect(region+2*page,page,PROT_READ));
    CHECK(!mprotect(pool+page,alt_bytes,PROT_READ|PROT_WRITE));
    alternate_begin=(uintptr_t)(pool+page); alternate_end=alternate_begin+alt_bytes;
    stack_t alternate={0},saved_stack;
    alternate.ss_sp=pool+page; alternate.ss_size=alt_bytes;
    CHECK(!sigaltstack(&alternate,&saved_stack));
    const int signals[]={SIGSEGV,SIGBUS,SIGILL};
    struct sigaction action={0},saved[3];
    action.sa_sigaction=capture; action.sa_flags=SA_SIGINFO|SA_ONSTACK|SA_RESTART;
    CHECK(!sigemptyset(&action.sa_mask) && !sigaddset(&action.sa_mask,SIGUSR2));
    for(unsigned i=0;i<3;++i) CHECK(!sigaction(signals[i],&action,&saved[i]));
    CHECK(!syscall(SYS_rt_sigprocmask,SIG_SETMASK,NULL,&original_mask,8));
    for(unsigned i=0;i<3;++i) CHECK(!(original_mask&(UINT64_C(1)<<(signals[i]-1))));
    expected_pid=getpid(); expected_tid=gettid(); expected_errno=&errno;
    // Cross an aligned 16-byte boundary: FEAT_LSE2 permits contained unaligned loads.
    uintptr_t targets[]={0,(uintptr_t)(region+page),(uintptr_t)(region+2*page),(uintptr_t)(region+9),0};
    int missing_edit=0,bad_address=0;
    for(unsigned i=0;i<FAULTS;++i) {
        received=0; active=(sig_atomic_t)i; errno=EDOM;
        resumed result=trigger(i,targets[i]);
        int preserved=errno==EDOM;
        active=-1;
        const observation *got=&seen[i];
        CHECK(received==1 && !handler_error && got->alternate && got->mask && got->errno_ok && preserved);
        const int numbers[]={SIGSEGV,SIGSEGV,SIGSEGV,SIGBUS,SIGILL};
        const int codes[]={SEGV_MAPERR,SEGV_ACCERR,SEGV_ACCERR,BUS_ADRALN,ILL_ILLOPC};
        CHECK(got->number==numbers[i] && got->code==codes[i] && got->pc==result.pc);
        missing_edit|=result.value!=0x5a;
        bad_address|=got->address!=(i==4?result.pc:targets[i]);
        if(i<4) CHECK(got->far==targets[i]);
        uint64_t restored=0;
        CHECK(!syscall(SYS_rt_sigprocmask,SIG_SETMASK,NULL,&restored,8) && restored==original_mask);
    }
    CHECK(region[2*page]==0x7c);
    for(unsigned i=0;i<3;++i) CHECK(!sigaction(signals[i],&saved[i],NULL));
    CHECK(!sigaltstack(&saved_stack,NULL));
    CHECK(!munmap(pool,pool_bytes) && !munmap(region,3*page));
    if(missing_edit) return -1006;
    if(bad_address) return -1007;
    return FAULTS;
}
