// Original Darwin ARM64 signal-context adapter. SPDX-License-Identifier: MIT
#define _DARWIN_C_SOURCE 1
#include "artbox/native_signal_context.h"
#include <stdint.h>
#include <string.h>
#include <sys/ucontext.h>
#if !defined(__APPLE__) || !defined(__aarch64__) || defined(__arm64e__)
#error This context adapter requires the ordinary Apple arm64 ABI
#endif
static int valid(const ucontext_t *context) {
    return context && context->uc_mcontext && context->uc_mcsize>=sizeof(*context->uc_mcontext);
}
int artbox_native_signal_capture(const void *raw,artbox_arm64_signal_state *state) {
    const ucontext_t *context=raw;
    if(!valid(context) || !state) return -22;
    const _STRUCT_MCONTEXT *machine=context->uc_mcontext;
    artbox_arm64_signal_state result={0};
    for(unsigned i=0;i<29;++i) result.x[i]=machine->__ss.__x[i];
    result.x[29]=__darwin_arm_thread_state64_get_fp(machine->__ss);
    result.x[30]=__darwin_arm_thread_state64_get_lr(machine->__ss);
    result.sp=__darwin_arm_thread_state64_get_sp(machine->__ss);
    result.pc=__darwin_arm_thread_state64_get_pc(machine->__ss);
    result.pstate=machine->__ss.__cpsr;
    result.fault_address=machine->__es.__far; result.esr=machine->__es.__esr;
    result.fpsr=machine->__ns.__fpsr; result.fpcr=machine->__ns.__fpcr;
    _Static_assert(sizeof(result.v)==sizeof(machine->__ns.__v),"ARM64 NEON context");
    memcpy(result.v,machine->__ns.__v,sizeof(result.v));
    *state=result;
    return 0;
}
int artbox_native_signal_apply(void *raw,const artbox_arm64_signal_state *state) {
    ucontext_t *context=raw;
    if(!valid(context) || !state || !state->pc || !state->sp || (state->pc&3) || (state->sp&15)) return -22;
    _STRUCT_MCONTEXT *machine=context->uc_mcontext;
    void (*pc)(void),(*lr)(void);
    _Static_assert(sizeof(pc)==sizeof(state->pc),"ARM64 function address");
    memcpy(&pc,&state->pc,sizeof(pc)); memcpy(&lr,&state->x[30],sizeof(lr));
    for(unsigned i=0;i<29;++i) if(i!=18) machine->__ss.__x[i]=state->x[i];
    __darwin_arm_thread_state64_set_fp(machine->__ss,(uintptr_t)state->x[29]);
    __darwin_arm_thread_state64_set_lr_fptr(machine->__ss,lr);
    __darwin_arm_thread_state64_set_sp(machine->__ss,(uintptr_t)state->sp);
    __darwin_arm_thread_state64_set_pc_fptr(machine->__ss,pc);
    const uint32_t nzcv=UINT32_C(0xf0000000);
    machine->__ss.__cpsr=(machine->__ss.__cpsr&~nzcv)|((uint32_t)state->pstate&nzcv);
    machine->__ns.__fpsr=state->fpsr; machine->__ns.__fpcr=state->fpcr;
    memcpy(machine->__ns.__v,state->v,sizeof(state->v));
    return 0;
}
