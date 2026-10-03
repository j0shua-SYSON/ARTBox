// Original Darwin signal return test. SPDX-License-Identifier: MIT
#define _DARWIN_C_SOURCE 1
#include "artbox/native_signal_context.h"
#include "artbox/native_signal_binding.h"
#include "artbox/native_tls.h"
#include "artbox/native_vm.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ucontext.h>
#if !defined(__APPLE__) || !defined(__aarch64__) || defined(__arm64e__)
#error Native Apple arm64 is required
#endif
static volatile sig_atomic_t observation,drop_register_edit;
typedef struct test_thread { void **tls; } test_thread;
typedef struct fault_result { uint64_t original_x18,resumed_x18,value,vector; } fault_result;
typedef struct ordinary_context { artbox_vm *vm; uint64_t address; volatile sig_atomic_t calls; } ordinary_context;
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"Darwin context line %d: %s\n",__LINE__,#x); return 1; } } while(0)
#define OBSERVE(x) do { if(!(x)) { observation=-__LINE__; return; } } while(0)
static void put(unsigned char *p,uint64_t value,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i) p[i]=(unsigned char)(value>>(8*i));
}
static int64_t ordinary_dispatch(void *raw,uint64_t n,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f) {
    (void)n; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
    ordinary_context *context=raw;
    ++context->calls;
    unsigned char byte;
    // This would deadlock if the signal path accidentally enters it while
    // trigger_locked owns the VM lock. The child-process test has a timeout.
    return artbox_vm_read(context->vm,context->address,&byte,1) ? -14 : 7;
}
static int64_t signal_dispatch(void *context,uint64_t n,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
    errno=EIO; // Returning syscall bridges must preserve host errno.
    return context==artbox_native_signal_thread_context() && n==172 ? 10000 : -22;
}
static void *binding_worker(void *unused) {
    (void)unused;
    void *tls[1]={0};
    test_thread thread={tls};
    const artbox_native_signal_scope scope={{signal_dispatch,&thread},tls},*previous,*removed;
    if(artbox_native_signal_current() || artbox_native_signal_thread_context() || artbox_bionic_get_tls() ||
       artbox_native_signal_attach(&thread) || artbox_native_signal_scope_swap(&scope,&previous) || previous ||
       artbox_bionic_get_tls()!=tls || artbox_bionic_syscall(172,0,0,0,0,0,0)!=10000 ||
       artbox_native_signal_scope_swap(NULL,&removed) || removed!=&scope || artbox_bionic_get_tls() ||
       artbox_native_signal_detach()) return NULL;
    return (void *)(uintptr_t)1;
}
static void handler(int number,siginfo_t *info,void *raw) {
    int saved_errno=errno;
    ucontext_t *context=raw;
    uint64_t pc=__darwin_arm_thread_state64_get_pc(context->uc_mcontext->__ss);
    // Bypass the deliberate BRK even if a conversion assertion fails.
    __darwin_arm_thread_state64_set_pc_fptr(context->uc_mcontext->__ss,(void *)(uintptr_t)(pc+4));
    OBSERVE(number==SIGTRAP && info->si_signo==SIGTRAP);
    artbox_arm64_signal_state before,after;
    OBSERVE(artbox_native_signal_capture(context,&before)==0);
    before.pc=pc;
    artbox_signal_frame_info meta={0}; // Host and guest signal masks are separate.
    unsigned char frame[ARTBOX_ARM64_UCONTEXT_BYTES];
    OBSERVE(artbox_signal_context_encode(frame,sizeof(frame),&before,&meta)==0);
    test_thread *thread=artbox_native_signal_thread_context();
    OBSERVE(thread!=NULL);
    const artbox_native_signal_scope scope={{signal_dispatch,thread},thread->tls};
    const artbox_native_signal_scope *previous,*removed;
    OBSERVE(artbox_native_signal_scope_swap(&scope,&previous)==0 && previous==NULL);
    errno=EDOM;
    int scope_ok=artbox_native_signal_current()==&scope && artbox_bionic_get_tls()==thread->tls &&
        artbox_bionic_set_tls(NULL)==-95 && artbox_bionic_get_tls()==thread->tls &&
        artbox_bionic_syscall(172,0,0,0,0,0,0)==10000 && errno==EDOM;
    int restore=artbox_native_signal_scope_swap(previous,&removed);
    errno=saved_errno;
    OBSERVE(scope_ok && restore==0 && removed==&scope && artbox_native_signal_current()==NULL);
    put(frame+184,0x5a,8); put(frame+184+18*8,~before.x[18],8);
    put(frame+440,pc+4,8); memset(frame+480,0x22,16);
    uint64_t mask;
    OBSERVE(artbox_signal_context_resume(frame,sizeof(frame),&before,&after,&mask)==0);
    OBSERVE(after.x[18]==before.x[18] && mask==0);
    after.x[18]=~before.x[18]; // The native adapter must defend x18 independently.
    if(drop_register_edit) {
        after.x[0]=before.x[0]; memcpy(after.v[0],before.v[0],16);
    }
    OBSERVE(artbox_native_signal_apply(context,&after)==0);
    observation=1;
}
static int64_t trigger_locked(void *raw,void *buffer,size_t length) {
    (void)buffer;
    fault_result *result=raw;
    uint64_t original_x18,resumed_x18,value,vector;
    __asm__ volatile("mov %0,x18\n\tmov x0,#7\n\tmovi v0.16b,#0x11\n\tbrk #0x42\n\t"
        "mov %1,x0\n\tumov %2,v0.d[0]\n\tmov %3,x18"
        : "=&r"(original_x18),"=&r"(value),"=&r"(vector),"=&r"(resumed_x18) : : "x0","v0","memory");
    *result=(fault_result){original_x18,resumed_x18,value,vector};
    return (int64_t)length;
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--drop-register-edit")) drop_register_edit=1;
    else if(argc!=1) return 64;
    // Validate output stability before touching a real process handler.
    _STRUCT_MCONTEXT machine={0},saved_machine;
    ucontext_t synthetic={0};
    synthetic.uc_mcontext=&machine; synthetic.uc_mcsize=sizeof(machine);
    artbox_arm64_signal_state state={0},unchanged={0};
    CHECK(artbox_native_signal_capture(NULL,&state)==-22);
    synthetic.uc_mcsize=sizeof(machine)-1;
    CHECK(artbox_native_signal_capture(&synthetic,&state)==-22 && !memcmp(&state,&unchanged,sizeof(state)));
    synthetic.uc_mcsize=sizeof(machine); state.sp=0x2000; state.pc=0x1002;
    saved_machine=machine;
    CHECK(artbox_native_signal_apply(&synthetic,&state)==-22 && !memcmp(&machine,&saved_machine,sizeof(machine)));
    state.pc=0x1000; state.sp=0x2001;
    CHECK(artbox_native_signal_apply(&synthetic,&state)==-22 && !memcmp(&machine,&saved_machine,sizeof(machine)));
    void *normal_tls[1]={0},*guest_tls[1]={0},*nested_tls[1]={0};
    void **previous_tls=artbox_native_tls_swap(normal_tls);
    test_thread thread={guest_tls};
    CHECK(artbox_native_signal_current()==NULL && artbox_native_signal_thread_context()==NULL);
    CHECK(artbox_native_signal_attach(&thread)==0 && artbox_native_signal_attach(&thread)==-17);
    const artbox_native_signal_scope outer={{signal_dispatch,&thread},guest_tls},inner={{signal_dispatch,&thread},nested_tls};
    const artbox_native_signal_scope *previous,*removed;
    CHECK(artbox_native_signal_scope_swap(&outer,&previous)==0 && previous==NULL);
    CHECK(artbox_native_signal_detach()==-16);
    pthread_t worker; void *worker_result;
    CHECK(pthread_create(&worker,NULL,binding_worker,NULL)==0 && pthread_join(worker,&worker_result)==0);
    CHECK(worker_result==(void *)(uintptr_t)1 && artbox_native_signal_current()==&outer && artbox_bionic_get_tls()==guest_tls);
    CHECK(artbox_native_signal_scope_swap(&inner,&previous)==0 && previous==&outer && artbox_bionic_get_tls()==nested_tls);
    CHECK(artbox_native_signal_scope_swap(previous,&removed)==0 && removed==&inner && artbox_bionic_get_tls()==guest_tls);
    CHECK(artbox_native_signal_scope_swap(NULL,&removed)==0 && removed==&outer && artbox_bionic_get_tls()==normal_tls);
    artbox_vm_ops memory=artbox_native_vm();
    artbox_vm *vm=artbox_vm_create(&memory,1024*1024,4);
    CHECK(vm!=NULL);
    int64_t address=artbox_vm_mmap(vm,0,memory.page_size,3,0x22,-1,0);
    CHECK(address>0);
    ordinary_context normal={vm,(uint64_t)address,0};
    const artbox_syscall_binding binding={ordinary_dispatch,&normal};
    const artbox_syscall_binding *old_binding=artbox_native_syscall_swap(&binding);
    CHECK(artbox_bionic_syscall(172,0,0,0,0,0,0)==7 && normal.calls==1);
    struct sigaction action={0},saved;
    action.sa_sigaction=handler; action.sa_flags=SA_SIGINFO;
    CHECK(!sigemptyset(&action.sa_mask) && !sigaction(SIGTRAP,&action,&saved));
    fault_result result;
    errno=ERANGE;
    CHECK(artbox_vm_transfer(vm,(uint64_t)address,1,1,trigger_locked,&result)==1 && errno==ERANGE);
    CHECK(!sigaction(SIGTRAP,&saved,NULL));
    CHECK(observation==1 && result.original_x18==result.resumed_x18 && normal.calls==1);
    CHECK(artbox_native_signal_current()==NULL && artbox_bionic_get_tls()==normal_tls);
    CHECK(artbox_bionic_syscall(172,0,0,0,0,0,0)==7 && normal.calls==2);
    CHECK(artbox_native_signal_detach()==0 && artbox_native_signal_detach()==-22);
    CHECK(artbox_native_signal_thread_context()==NULL && artbox_native_signal_scope_swap(&outer,&previous)==-22);
    CHECK(artbox_native_syscall_swap(old_binding)==&binding && artbox_native_tls_swap(previous_tls)==normal_tls);
    CHECK(artbox_vm_munmap(vm,(uint64_t)address,memory.page_size)==0 && artbox_vm_destroy(vm)==0);
    if(result.value!=0x5a || result.vector!=UINT64_C(0x2222222222222222)) {
        fputs("handler register edits were not resumed\n",stderr); return 1;
    }
    puts("{\"frame_bytes\":4560,\"native_resume\":true,\"general_register_edit\":true,\"vector_edit\":true,\"x18_preserved\":true,\"signal_binding\":true,\"mapper_lock_held\":true}");
    return 0;
}
