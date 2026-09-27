// Original signed Android SIGTRAP delivery. SPDX-License-Identifier: MIT
#include "artbox/native_signal_delivery.h"
#include "artbox/native_signal_binding.h"
#include "artbox/native_signal_context.h"
#include "artbox/native_call.h"
#include <errno.h>
#include <string.h>

static int within(uint64_t base,uint64_t size,uint64_t address,uint64_t length) {
    return address>=base && address-base<=size && length<=size-(address-base);
}
static int executable(const artbox_native_signal_thread *thread,uint64_t address) {
    if(address&3) return 0;
    for(size_t i=0;i<thread->code_count;++i)
        if(within(thread->code[i].address,thread->code[i].size,address,4)) return 1;
    return 0;
}
int artbox_native_signal_validate_trap(void *context,unsigned number,const artbox_signal_action *action) {
    const artbox_native_signal_thread *thread=context;
    if(!thread || !action) return -22;
    if(number!=5 || action->flags&~UINT64_C(0x10000004) || action->restorer || action->mask) return -95;
    if(!action->handler) return 0;
    if(action->handler==1 || !(action->flags&4)) return -95;
    return executable(thread,action->handler) ? 0 : -22;
}
typedef struct delivery_scope {
    const artbox_native_signal_thread *thread;
    uint64_t mask;
} delivery_scope;
static int64_t signal_call(void *raw,uint64_t number,uint64_t a0,uint64_t a1,
    uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5) {
    const delivery_scope *delivery=raw;
    const artbox_native_signal_thread *thread=delivery->thread;
    (void)a0; (void)a4; (void)a5;
    if(number==172) return thread->kernel->pid;
    if(number==178) return thread->kernel->tid;
    if(number!=135) return -38;
    if(a3!=8) return -22;
    if(a1) return -95; // Mask changes require pending-delivery integration.
    if(!a2) return 0;
    if(!within(thread->stack_address,thread->stack_size,a2,8)) return -14;
    unsigned char *bytes=(void *)(uintptr_t)a2;
    for(unsigned i=0;i<8;++i) bytes[i]=(unsigned char)(delivery->mask>>(8*i));
    return 0;
}
static void word(unsigned char *bytes,uint64_t value,unsigned size) {
    for(unsigned i=0;i<size;++i) bytes[i]=(unsigned char)(value>>(8*i));
}
static int deliver(artbox_native_signal_thread *thread,void *host_context) {
    if(!thread || !thread->kernel || !thread->actions || !thread->guest_tls) return -22;
    artbox_signal_action action;
    int error=artbox_signal_actions_snapshot(thread->actions,5,&action);
    if(error) return error;
    if(!action.handler) return -95; // Caller owns the fatal/default disposition.
    error=artbox_native_signal_validate_trap(thread,5,&action);
    if(error) return error;
    artbox_arm64_signal_state interrupted,resumed;
    error=artbox_native_signal_capture(host_context,&interrupted);
    if(error) return error;
    if(!executable(thread,interrupted.pc) ||
        !within(thread->stack_address,thread->stack_size,interrupted.sp,0)) return -22;
    uint32_t instruction;
    memcpy(&instruction,(const void *)(uintptr_t)interrupted.pc,sizeof(instruction));
    if((instruction&UINT32_C(0xffe0001f))!=UINT32_C(0xd4200000)) return -95;
    uint64_t original_mask;
    error=artbox_signals_mask_snapshot(thread->kernel,&original_mask);
    if(error) return error;
    if(original_mask&16) return -95;
    const artbox_signal_frame_info meta={original_mask,0,0,2}; // Disabled alternate stack.
    _Alignas(16) unsigned char frame[ARTBOX_ARM64_UCONTEXT_BYTES],info[128]={0};
    if(!within(thread->stack_address,thread->stack_size,(uintptr_t)frame,sizeof(frame)) ||
        !within(thread->stack_address,thread->stack_size,(uintptr_t)info,sizeof(info))) return -14;
    error=artbox_signal_context_encode(frame,sizeof(frame),&interrupted,&meta);
    if(error) return error;
    word(info,5,4); word(info+8,1,4); word(info+16,interrupted.pc,8); // Linux TRAP_BRKPT.
    const delivery_scope delivery={thread,original_mask|16};
    const artbox_native_signal_scope scope={{signal_call,(void *)&delivery},thread->guest_tls};
    const artbox_native_signal_scope *previous,*replaced;
    error=artbox_native_signal_scope_swap(&scope,&previous);
    if(error) return error;
    artbox_call7((void *)(uintptr_t)action.handler,5,(uintptr_t)info,(uintptr_t)frame,0,0,0,0);
    error=artbox_native_signal_scope_swap(previous,&replaced);
    if(error || replaced!=&scope) return -22;
    uint64_t mask;
    error=artbox_signal_context_resume(frame,sizeof(frame),&interrupted,&resumed,&mask);
    if(error) return error;
    if(mask!=original_mask) return -95;
    if(!executable(thread,resumed.pc) ||
        !within(thread->stack_address,thread->stack_size,resumed.sp,0)) return -22;
    return artbox_native_signal_apply(host_context,&resumed);
}
int artbox_native_signal_deliver_trap(artbox_native_signal_thread *thread,void *host_context) {
    int saved=errno;
    int result=deliver(thread,host_context);
    errno=saved;
    return result;
}
