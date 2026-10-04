// Original caller of pinned AOSP sigchain. SPDX-License-Identifier: MIT
#include "sigchain.h"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <ucontext.h>
#if defined(__BIONIC__)
#include <bionic/reserved_signals.h>
#endif
#if !defined(__aarch64__)
#error This contract requires native ARM64
#endif

namespace {
using Action = int (*)(int, const struct sigaction*, struct sigaction*);
using Mask = int (*)(int, const sigset_t*, sigset_t*);
Mask chain_mask;
volatile sig_atomic_t phase, special_calls, user_calls, failed, checks;
volatile uint32_t failed_bits,mask_count;
volatile uint64_t masks[32];
uintptr_t stack_base;
int* owner_errno;
constexpr size_t stack_bytes=65536;
constexpr uint64_t usr1=UINT64_C(1)<<9, usr2=UINT64_C(1)<<11, trap=UINT64_C(1)<<4;
#if defined(__BIONIC__)
// Pinned Bionic forces its POSIX-timer signal blocked through sigprocmask.
// Assert that bit as well as the requested bits; do not mask it out of reads.
constexpr uint64_t libc_required=UINT64_C(1)<<(BIONIC_SIGNAL_POSIX_TIMERS-1);
#else
constexpr uint64_t libc_required=0;
#endif
constexpr uint64_t baseline_bits=usr1|libc_required;

void observe(bool passed) {
    if(!passed) { failed=1; failed_bits=failed_bits|(UINT32_C(1)<<checks); }
    checks=checks+1;
}
uint64_t current_mask() {
    uint64_t value=UINT64_MAX;
    if(syscall(SYS_rt_sigprocmask,0,nullptr,&value,8)) failed=1;
    if(mask_count<32) { masks[mask_count]=value; mask_count=mask_count+1; }
    return value;
}
void handler_context(int signal,siginfo_t* info,void* raw) {
    ucontext_t* context=static_cast<ucontext_t*>(raw);
    uint64_t saved=0;
    std::memcpy(&saved,&context->uc_sigmask,8);
    char local;
    uintptr_t sp=reinterpret_cast<uintptr_t>(&local);
    observe(signal==SIGTRAP && info->si_code==TRAP_BRKPT && saved==baseline_bits);
    observe(sp>=stack_base && sp-stack_base<stack_bytes && &errno==owner_errno);
}
void resume(void* raw,uint64_t value) {
    auto* context=static_cast<ucontext_t*>(raw);
    context->uc_mcontext.pc+=4;
    context->uc_mcontext.regs[0]=value;
    errno=EDOM;
}
bool special(int signal,siginfo_t* info,void* raw) {
    special_calls=special_calls+1;
    handler_context(signal,info,raw);
    observe(current_mask()==(usr2|libc_required));
    sigset_t self;
    sigemptyset(&self); sigaddset(&self,SIGTRAP);
    // AOSP's scoped TLS bit lets the real wrapper block a claimed signal here.
    observe(chain_mask(SIG_BLOCK,&self,nullptr)==0 && current_mask()==(usr2|trap|libc_required));
    if(phase==1) { resume(raw,0x51); return true; }
    return false;
}
void user(int signal,siginfo_t* info,void* raw) {
    user_calls=user_calls+1;
    handler_context(signal,info,raw);
    observe(current_mask()==(baseline_bits|usr2|trap));
    resume(raw,0x52);
}
uint64_t breakpoint() {
    uint64_t value;
    errno=EDOM;
    __asm__ volatile("mov x0,#7\n\tbrk #0x42\n\tmov %0,x0" : "=&r"(value) : : "x0","memory");
    return value;
}
}

// Explicit function pointers must name libart's wrappers, not libc's symbols
// earlier in the dependency group. The Linux executable binds its own wrappers.
extern "C" __attribute__((visibility("default")))
int artbox_sigchain_check(uint64_t action_address,uint64_t mask_address,int drop_special) {
    auto chain_action=reinterpret_cast<Action>(action_address);
    chain_mask=reinterpret_cast<Mask>(mask_address);
    phase=0; special_calls=0; user_calls=0; failed=0; checks=0;
    failed_bits=0; mask_count=0;
    owner_errno=&errno;
    uint64_t saved_mask;
    struct sigaction saved_action{},action{};
    stack_t saved_stack{},alternate{};
    if(!chain_action || !chain_mask || syscall(SYS_rt_sigprocmask,0,nullptr,&saved_mask,8) ||
       chain_action(SIGTRAP,nullptr,&saved_action) || sigaltstack(nullptr,&saved_stack)) return -1;
    void* memory=mmap(nullptr,stack_bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(memory==MAP_FAILED) return -2;
    stack_base=reinterpret_cast<uintptr_t>(memory);
    alternate.ss_sp=memory; alternate.ss_size=stack_bytes;
    if(sigaltstack(&alternate,nullptr)) { munmap(memory,stack_bytes); return -3; }
    sigset_t baseline,self;
    sigemptyset(&baseline); sigaddset(&baseline,SIGUSR1);
    sigemptyset(&self); sigaddset(&self,SIGTRAP);
    // Allocate sigchain's pthread key before entering any host signal handler.
    if(chain_mask(SIG_SETMASK,&baseline,nullptr)) return -4;
    action.sa_sigaction=user; action.sa_flags=SA_SIGINFO|SA_ONSTACK;
    sigemptyset(&action.sa_mask); sigaddset(&action.sa_mask,SIGUSR2);
    if(chain_action(SIGTRAP,&action,nullptr)) return -5;
    art::SigchainAction special_action{};
    special_action.sc_sigaction=special;
    sigemptyset(&special_action.sc_mask); sigaddset(&special_action.sc_mask,SIGUSR2);
    art::AddSpecialSignalHandlerFn(SIGTRAP,&special_action);
    art::EnsureFrontOfChain(SIGTRAP);
    uint64_t kernel_action[4]={};
    observe(syscall(SYS_rt_sigaction,SIGTRAP,nullptr,kernel_action,8)==0 &&
        kernel_action[0]!=reinterpret_cast<uintptr_t>(user) && kernel_action[0]>1 &&
        (kernel_action[1]&UINT64_C(0x18000404))==UINT64_C(0x18000004));
    struct sigaction forwarded{};
    observe(chain_action(SIGTRAP,nullptr,&forwarded)==0 && forwarded.sa_sigaction==user);
    // Outside a handler the real sigchain wrapper strips a claimed signal.
    observe(chain_mask(SIG_BLOCK,&self,nullptr)==0 && current_mask()==baseline_bits);
    if(drop_special) art::RemoveSpecialSignalHandlerFn(SIGTRAP,special);
    phase=1;
    uint64_t first=breakpoint();
    observe(first==0x51 && errno==EDOM && current_mask()==baseline_bits);
    phase=2;
    uint64_t second=breakpoint();
    observe(second==0x52 && errno==EDOM && current_mask()==baseline_bits);
    // This also checks that the scoped handling TLS bit was cleared on return.
    observe(chain_mask(SIG_BLOCK,&self,nullptr)==0 && current_mask()==baseline_bits);
    if(!drop_special) art::RemoveSpecialSignalHandlerFn(SIGTRAP,special);
    phase=3;
    uint64_t third=breakpoint();
    observe(third==0x52 && errno==EDOM && current_mask()==baseline_bits);
    observe(special_calls==2 && user_calls==2);
    // AOSP retains the claimed chain after removing the last special handler.
    // Restore its user disposition; process teardown owns the kernel action.
    if(chain_action(SIGTRAP,&saved_action,nullptr) || sigaltstack(&saved_stack,nullptr) ||
       munmap(memory,stack_bytes) || syscall(SYS_rt_sigprocmask,SIG_SETMASK,&saved_mask,nullptr,8)) return -6;
    return failed ? -1005 : checks;
}

// Read only after the caller returns. Never format/log inside a handler.
extern "C" __attribute__((visibility("default")))
uint64_t artbox_sigchain_detail(unsigned index) {
    if(index==0) return failed_bits;
    if(index==1) return checks;
    if(index==2) return special_calls;
    if(index==3) return user_calls;
    if(index==4) return mask_count;
    return index-5<mask_count ? masks[index-5] : UINT64_MAX;
}
