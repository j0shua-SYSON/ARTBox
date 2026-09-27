// Original Darwin signal return test. SPDX-License-Identifier: MIT
#define _DARWIN_C_SOURCE 1
#include "artbox/native_signal_context.h"
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ucontext.h>
#if !defined(__APPLE__) || !defined(__aarch64__) || defined(__arm64e__)
#error Native Apple arm64 is required
#endif
static volatile sig_atomic_t observation,drop_register_edit;
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"Darwin context line %d: %s\n",__LINE__,#x); return 1; } } while(0)
#define OBSERVE(x) do { if(!(x)) { observation=-__LINE__; return; } } while(0)
static void put(unsigned char *p,uint64_t value,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i) p[i]=(unsigned char)(value>>(8*i));
}
static void handler(int number,siginfo_t *info,void *raw) {
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
    struct sigaction action={0},saved;
    action.sa_sigaction=handler; action.sa_flags=SA_SIGINFO;
    CHECK(!sigemptyset(&action.sa_mask) && !sigaction(SIGTRAP,&action,&saved));
    uint64_t original_x18,resumed_x18,value,vector;
    __asm__ volatile("mov %0,x18\n\tmov x0,#7\n\tmovi v0.16b,#0x11\n\tbrk #0x42\n\t"
        "mov %1,x0\n\tumov %2,v0.d[0]\n\tmov %3,x18"
        : "=&r"(original_x18),"=&r"(value),"=&r"(vector),"=&r"(resumed_x18) : : "x0","v0","memory");
    CHECK(!sigaction(SIGTRAP,&saved,NULL));
    CHECK(observation==1 && original_x18==resumed_x18);
    if(value!=0x5a || vector!=UINT64_C(0x2222222222222222)) {
        fputs("handler register edits were not resumed\n",stderr); return 1;
    }
    puts("{\"frame_bytes\":4560,\"native_resume\":true,\"general_register_edit\":true,\"vector_edit\":true,\"x18_preserved\":true}");
    return 0;
}
