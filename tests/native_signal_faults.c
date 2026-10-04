// Original native fault classification reference. SPDX-License-Identifier: MIT
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#define _DARWIN_C_SOURCE 1
#include "artbox/native_signal_context.h"
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/ucontext.h>
#elif defined(__linux__)
#include <asm/sigcontext.h>
#include <ucontext.h>
#else
#error Native Linux or Darwin is required
#endif
#if !defined(__aarch64__) || defined(__arm64e__)
#error This reference requires ordinary native ARM64
#endif

enum { FAULTS=5 };
typedef struct fault_observation {
    uint64_t pc,address,far,esr;
    int number,code,alternate,errno_preserved;
} fault_observation;
static fault_observation observations[FAULTS];
static volatile sig_atomic_t active=-1,received,handler_error,drop_edit,drop_address;
static uintptr_t alternate_begin,alternate_end;
static const char *names[FAULTS]={"null-read","protected-read","readonly-write","unaligned-atomic","undefined-instruction"};
static void dump_observations(void) {
    // Normal-context diagnostics only, after the synchronous handler returns.
    for(unsigned i=0;i<FAULTS;++i) {
        const fault_observation *seen=&observations[i];
        if(seen->number) fprintf(stderr,"%s: signal=%d code=%d pc=%" PRIx64 " address=%" PRIx64
            " far=%" PRIx64 " esr=%" PRIx64 " alternate=%d errno=%d\n",names[i],seen->number,
            seen->code,seen->pc,seen->address,seen->far,seen->esr,seen->alternate,seen->errno_preserved);
    }
}
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"native fault line %d: %s\n",__LINE__,#x); dump_observations(); return 1; } } while(0)

static void handler(int number,siginfo_t *info,void *raw) {
    const int saved_errno=errno;
    const int index=active;
    if(index<0 || index>=FAULTS || received) _exit(125);
    ucontext_t *context=raw;
    uint64_t pc,far,esr=0;
#if defined(__APPLE__)
    pc=__darwin_arm_thread_state64_get_pc(context->uc_mcontext->__ss);
    // Even a failed observation must skip the deliberate fault instruction.
    __darwin_arm_thread_state64_set_pc_fptr(context->uc_mcontext->__ss,(void *)(uintptr_t)(pc+4));
    artbox_arm64_signal_state state;
    if(artbox_native_signal_capture(context,&state)) {
        handler_error=1; errno=saved_errno; return;
    }
    far=state.fault_address; esr=state.esr;
    if(!drop_edit) state.x[0]=0x5a;
    // PC was advanced before capture. The adapter also protects x18.
    if(artbox_native_signal_apply(context,&state)) handler_error=2;
#else
    pc=context->uc_mcontext.pc;
    context->uc_mcontext.pc=pc+4;
    far=context->uc_mcontext.fault_address;
    unsigned char *reserved=context->uc_mcontext.__reserved;
    for(size_t offset=0;offset+16<=sizeof(context->uc_mcontext.__reserved);) {
        struct _aarch64_ctx head;
        memcpy(&head,reserved+offset,sizeof(head));
        if(!head.magic && !head.size) break;
        if(head.size<16 || (head.size&15) || head.size>sizeof(context->uc_mcontext.__reserved)-offset) {
            handler_error=3; break;
        }
        if(head.magic==ESR_MAGIC) {
            if(head.size!=sizeof(struct esr_context)) { handler_error=4; break; }
            memcpy(&esr,reserved+offset+8,sizeof(esr));
        }
        offset+=head.size;
    }
    if(!drop_edit) context->uc_mcontext.regs[0]=0x5a;
#endif
    char marker;
    uintptr_t sp=(uintptr_t)&marker;
    observations[index]=(fault_observation){pc,(uintptr_t)info->si_addr,drop_address?0:far,esr,
        number,info->si_code,sp>=alternate_begin && sp<alternate_end,saved_errno==EDOM};
    if(info->si_signo!=number) handler_error=5;
    received=1;
    errno=saved_errno;
}

typedef struct resumed_state { uint64_t x18_before,x18_after,value,fault_pc; } resumed_state;
static resumed_state trigger(unsigned kind,uintptr_t address) {
    resumed_state result;
#define TRIGGER(instruction) __asm__ volatile("mov %0,x18\n\tadr %3,1f\n\tmov x0,#7\n\t1: " instruction \
    "\n\tmov %1,x0\n\tmov %2,x18" : "=&r"(result.x18_before),"=&r"(result.value), \
    "=&r"(result.x18_after),"=&r"(result.fault_pc) : "r"(address) : "x0","memory")
    if(kind<=1) { TRIGGER("ldr x0,[%4]"); }
    else if(kind==2) { TRIGGER("str x0,[%4]"); }
    else if(kind==3) { TRIGGER("ldxr x0,[%4]\n\tclrex"); }
    else { TRIGGER("udf #0"); }
#undef TRIGGER
    return result;
}

int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--drop-register-edit")) drop_edit=1;
    else if(argc==2 && !strcmp(argv[1],"--drop-fault-address")) drop_address=1;
    else if(argc!=1) return 64;
    long native_page=sysconf(_SC_PAGESIZE);
    CHECK(native_page>0 && !(native_page&(native_page-1)));
    size_t page=(size_t)native_page,alt_bytes=(65536+page-1)&~(page-1),pool_bytes=alt_bytes+2*page;
    unsigned char *region=mmap(NULL,3*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    unsigned char *pool=mmap(NULL,pool_bytes,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(region!=MAP_FAILED && pool!=MAP_FAILED);
    region[2*page]=0x7c;
    CHECK(!mprotect(region+page,page,PROT_NONE) && !mprotect(region+2*page,page,PROT_READ));
    CHECK(!mprotect(pool+page,alt_bytes,PROT_READ|PROT_WRITE));
    alternate_begin=(uintptr_t)(pool+page); alternate_end=alternate_begin+alt_bytes;
    stack_t alternate={0},saved_stack;
    alternate.ss_sp=pool+page; alternate.ss_size=alt_bytes;
    CHECK(!sigaltstack(&alternate,&saved_stack));
    const int signals[]={SIGSEGV,SIGBUS,SIGILL};
    struct sigaction action={0},saved[3];
    action.sa_sigaction=handler; action.sa_flags=SA_SIGINFO|SA_ONSTACK;
    CHECK(!sigemptyset(&action.sa_mask));
    for(unsigned i=0;i<3;++i) CHECK(!sigaction(signals[i],&action,&saved[i]));
    // FEAT_LSE2 permits an unaligned exclusive access contained within one
    // aligned 16-byte quantity. Cross that boundary while staying in mapped RW
    // memory, so this case requires an alignment fault on either CPU profile.
    CHECK(page>=32 && !((uintptr_t)region&15));
    uintptr_t targets[]={0,(uintptr_t)(region+page),(uintptr_t)(region+2*page),(uintptr_t)(region+9),0};
    int missing_edit=0,bad_address=0;
    for(unsigned i=0;i<FAULTS;++i) {
        received=0; active=(sig_atomic_t)i; errno=EDOM;
        resumed_state returned=trigger(i,targets[i]);
        int preserved_errno=errno==EDOM;
        active=-1;
        const fault_observation *seen=&observations[i];
        CHECK(received==1 && !handler_error && seen->alternate && seen->errno_preserved && preserved_errno);
        CHECK(seen->pc==returned.fault_pc && returned.x18_before==returned.x18_after && seen->code>0);
        missing_edit|=returned.value!=0x5a;
        if(i<4) bad_address|=seen->address!=targets[i] || seen->far!=targets[i];
        else CHECK(seen->address==returned.fault_pc);
#if defined(__linux__)
        const int expected_signals[]={SIGSEGV,SIGSEGV,SIGSEGV,SIGBUS,SIGILL};
        const int expected_codes[]={SEGV_MAPERR,SEGV_ACCERR,SEGV_ACCERR,BUS_ADRALN,ILL_ILLOPC};
        CHECK(seen->number==expected_signals[i] && seen->code==expected_codes[i]);
#else
        // Characterize actual Darwin values before implementing a translation.
        CHECK(i==4 ? seen->number==SIGILL : seen->number==SIGSEGV || seen->number==SIGBUS);
#endif
    }
    CHECK(region[2*page]==0x7c); // The failed store must not change read-only data.
    for(unsigned i=0;i<3;++i) CHECK(!sigaction(signals[i],&saved[i],NULL));
    stack_t restore=saved_stack,restored;
#if defined(__APPLE__)
    // Darwin checks the supplied size even for SS_DISABLE. It will not use this
    // storage when disabling, but requires a minimum size at the API boundary.
    if((restore.ss_flags&SS_DISABLE) && restore.ss_size<(size_t)MINSIGSTKSZ)
        restore.ss_size=(size_t)MINSIGSTKSZ;
#endif
    CHECK(!sigaltstack(&restore,NULL) && !sigaltstack(NULL,&restored));
    CHECK((restored.ss_flags&SS_DISABLE)==(saved_stack.ss_flags&SS_DISABLE));
    if(!(saved_stack.ss_flags&SS_DISABLE))
        CHECK(restored.ss_sp==saved_stack.ss_sp && restored.ss_size==saved_stack.ss_size);
    CHECK(!munmap(pool,pool_bytes) && !munmap(region,3*page));
    if(missing_edit) {
        if(!drop_edit) dump_observations();
        fputs("fault register edits were not resumed\n",stderr); return 1;
    }
    if(bad_address) {
        if(!drop_address) dump_observations();
        fputs("fault address metadata did not match\n",stderr); return 1;
    }
    printf("{\"fault_cases\":5,\"native_resume\":true,\"alternate_stack\":true,\"x18_preserved\":true,\"faults\":[");
    for(unsigned i=0;i<FAULTS;++i) {
        const fault_observation *seen=&observations[i];
        printf("%s{\"name\":\"%s\",\"signal\":%d,\"code\":%d,\"esr\":%" PRIu64 "}",
            i?",":"",names[i],seen->number,seen->code,seen->esr);
    }
    puts("]}");
    return 0;
}
