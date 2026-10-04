// Original signed Android synchronous fault delivery. SPDX-License-Identifier: MIT
#define _DARWIN_C_SOURCE 1
#include "artbox/native_signal_delivery.h"
#include "artbox/native_signal_binding.h"
#include "artbox/native_signal_context.h"
#include "artbox/native_call.h"
#include "artbox/signal_fault.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

typedef struct host_stack {
    void *mapping;
    size_t mapping_size;
    stack_t active,previous;
} host_stack;
static int stack_error(void) { return errno==ENOMEM?-12:errno==EPERM?-1:errno==EINVAL?-22:-5; }
static int restore_host_stack(const host_stack *state) {
    stack_t previous=state->previous;
    // Older Darwin libc checks the size even for SS_DISABLE. The disabled
    // registration has no active storage; satisfy that public API's minimum.
    if((previous.ss_flags&SS_DISABLE) && previous.ss_size<(size_t)MINSIGSTKSZ)
        previous.ss_size=(size_t)MINSIGSTKSZ;
    return sigaltstack(&previous,NULL);
}
int artbox_native_signal_thread_attach(artbox_native_signal_thread *thread) {
    if(!thread || !thread->kernel || !thread->guest_tls || thread->platform_state) return -22;
    if(!artbox_signals_handler_mask_support() || !artbox_vm_fault_snapshot_support()) return -95;
    if(artbox_native_signal_thread_context()) return -17;
    size_t page=artbox_vm_page_size(thread->kernel->vm);
    if(!page || page>65536 || (page&(page-1))) return -22;
    host_stack *state=calloc(1,sizeof(*state));
    if(!state) return -12;
    size_t size=128*1024;
    if(size<(size_t)MINSIGSTKSZ) size=(size_t)MINSIGSTKSZ;
    size=(size+page-1)&~(page-1);
    state->mapping_size=size+2*page;
    state->mapping=mmap(NULL,state->mapping_size,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(state->mapping==MAP_FAILED) { free(state); return -12; }
    state->active.ss_sp=(unsigned char *)state->mapping+page;
    state->active.ss_size=size;
    if(mprotect(state->active.ss_sp,size,PROT_READ|PROT_WRITE)) {
        munmap(state->mapping,state->mapping_size); free(state); return -12;
    }
    if(sigaltstack(&state->active,&state->previous)) {
        int error=stack_error(); munmap(state->mapping,state->mapping_size); free(state); return error;
    }
    thread->platform_state=state;
    int error=artbox_native_signal_attach(thread);
    if(error) {
        if(restore_host_stack(state)) return -5; // Keep live storage if restoration fails.
        thread->platform_state=NULL;
        munmap(state->mapping,state->mapping_size); free(state);
    }
    return error;
}
int artbox_native_signal_thread_detach(artbox_native_signal_thread *thread) {
    if(!thread || artbox_native_signal_thread_context()!=thread || !thread->platform_state) return -22;
    if(artbox_native_signal_current()) return -16;
    host_stack *state=thread->platform_state;
    if(restore_host_stack(state)) return stack_error();
    int error=artbox_native_signal_detach();
    if(error) {
        if(sigaltstack(&state->active,NULL)) return -5;
        return error;
    }
    thread->platform_state=NULL;
    int unmapped=munmap(state->mapping,state->mapping_size);
    free(state);
    return unmapped?-5:0;
}

static int within(uint64_t base,uint64_t size,uint64_t address,uint64_t length) {
    return address>=base && address-base<=size && length<=size-(address-base);
}
static int executable(const artbox_native_signal_thread *thread,uint64_t address) {
    if(address&3) return 0;
    for(size_t i=0;i<thread->code_count;++i)
        if(within(thread->code[i].address,thread->code[i].size,address,4)) return 1;
    return 0;
}
int artbox_native_signal_validate_fault(void *context,unsigned number,const artbox_signal_action *action) {
    const artbox_native_signal_thread *thread=context;
    if(!thread || !action) return -22;
    if((number!=4 && number!=5 && number!=7 && number!=11) ||
       action->flags&~UINT64_C(0x18000004) || action->restorer) return -95;
    if(!action->handler) return 0;
    if(action->handler==1 || !(action->flags&4)) return -95;
    return executable(thread,action->handler) ? 0 : -22;
}
typedef struct delivery_scope {
    const artbox_native_signal_thread *thread;
    artbox_signal_stack alternate;
} delivery_scope;
static int guest_stack(const delivery_scope *delivery,uint64_t address,uint64_t size) {
    return within(delivery->thread->stack_address,delivery->thread->stack_size,address,size) ||
        (delivery->alternate.size && within(delivery->alternate.address,delivery->alternate.size,address,size));
}
static int guest_buffer(const delivery_scope *delivery,uint64_t address,uint64_t size,int write) {
    if(guest_stack(delivery,address,size)) return 1;
    const artbox_native_signal_thread *thread=delivery->thread;
    for(size_t i=0;i<thread->data_count;++i)
        if(within(thread->data[i].address,thread->data[i].size,address,size)) return 1;
    if(!write) for(size_t i=0;i<thread->code_count;++i)
        if(within(thread->code[i].address,thread->code[i].size,address,size)) return 1;
    return 0;
}
static void word(unsigned char *bytes,uint64_t value,unsigned size) {
    for(unsigned i=0;i<size;++i) bytes[i]=(unsigned char)(value>>(8*i));
}
static uint64_t read_word(const unsigned char *bytes,unsigned size) {
    uint64_t result=0;
    for(unsigned i=0;i<size;++i) result|=(uint64_t)bytes[i]<<(8*i);
    return result;
}
static int64_t signal_call(void *raw,uint64_t number,uint64_t a0,uint64_t a1,
    uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5) {
    const delivery_scope *delivery=raw;
    const artbox_native_signal_thread *thread=delivery->thread;
    (void)a4; (void)a5;
    if(number==172) return thread->kernel->pid;
    if(number==178) return thread->kernel->tid;
    if(number==132) {
        // Linux copies the input before checking whether the stack is active.
        if(a0 && !guest_buffer(delivery,a0,24,0)) return -14;
        unsigned char marker;
        artbox_signal_stack stack;
        int error=artbox_signals_stack_snapshot(thread->kernel,(uintptr_t)&marker,&stack);
        if(error) return error;
        if(a0) return stack.flags==1?-1:-95; // No allocating updates from a handler.
        if(!a1) return 0;
        if(!guest_buffer(delivery,a1,24,1)) return -14;
        unsigned char *bytes=(void *)(uintptr_t)a1;
        word(bytes,stack.address,8); word(bytes+8,stack.flags,8); word(bytes+16,stack.size,8);
        return 0;
    }
    if(number!=135) return -38;
    if(a3!=8) return -22;
    uint64_t requested=0,previous;
    if(a1) {
        if(!guest_buffer(delivery,a1,8,0)) return -14;
        requested=read_word((const void *)(uintptr_t)a1,8);
    }
    int error=artbox_signals_mask_update(thread->kernel,(uint32_t)a0,a1?&requested:NULL,&previous);
    if(error || !a2) return error;
    if(!guest_buffer(delivery,a2,8,1)) return -14; // Mutation precedes copyout failure.
    word((void *)(uintptr_t)a2,previous,8);
    return 0;
}
static int deliver(artbox_native_signal_thread *thread,int host_number,const siginfo_t *host_info,void *host_context) {
    if(!thread || !thread->kernel || !thread->actions || !thread->guest_tls || !thread->platform_state) return -22;
    if(!host_info || host_info->si_signo!=host_number) return -22;
    const host_stack *host=thread->platform_state;
    unsigned char host_marker;
    if(!within((uintptr_t)host->active.ss_sp,host->active.ss_size,(uintptr_t)&host_marker,1)) return -22;
    artbox_arm64_signal_state interrupted,resumed;
    int error=artbox_native_signal_capture(host_context,&interrupted);
    if(error) return error;
    artbox_signal_stack alternate;
    error=artbox_signals_stack_snapshot(thread->kernel,interrupted.sp,&alternate);
    if(error) return error;
    const delivery_scope delivery={thread,alternate};
    if(!executable(thread,interrupted.pc) || !guest_stack(&delivery,interrupted.sp,0)) return -22;
    uint32_t instruction;
    memcpy(&instruction,(const void *)(uintptr_t)interrupted.pc,sizeof(instruction));
    unsigned kind;
    artbox_vm_fault_info memory={0,0,0};
    if(host_number==SIGTRAP) kind=ARTBOX_FAULT_BREAKPOINT;
    else if(host_number==SIGILL) {
        // Darwin reports UDF as ILL_ILLTRP (2); its ILL_ILLOPN is 4.
        // Linux's identically numbered code 2 has a different symbolic name.
        if(host_info->si_code!=ILL_ILLTRP || (uintptr_t)host_info->si_addr!=interrupted.pc) return -95;
        kind=ARTBOX_FAULT_UNDEFINED;
    } else if(host_number==SIGSEGV || host_number==SIGBUS) {
        if((host_info->si_code!=1 && host_info->si_code!=2) ||
           (uintptr_t)host_info->si_addr!=interrupted.fault_address) return -95;
        kind=ARTBOX_FAULT_DATA;
        if((interrupted.esr&63)!=0x21) {
            error=artbox_vm_fault_snapshot(thread->kernel->vm,interrupted.fault_address,&memory);
            if(error) return error;
        }
    } else return -95;
    artbox_signal_fault event;
    error=artbox_signal_classify_fault(kind,instruction,&interrupted,&memory,&event);
    if(error) return error;
    artbox_signal_action action;
    error=artbox_signal_actions_snapshot(thread->actions,event.number,&action);
    if(error) return error;
    if(!action.handler) return -95; // Caller owns the fatal/default disposition.
    error=artbox_native_signal_validate_fault(thread,event.number,&action);
    if(error) return error;
    const uint64_t self=UINT64_C(1)<<(event.number-1);
    uint64_t original_mask;
    error=artbox_signals_mask_snapshot(thread->kernel,&original_mask);
    if(error) return error;
    if(original_mask&self) return -95;
    const artbox_signal_frame_info meta={original_mask,alternate.address,alternate.size,alternate.flags};
    uint64_t base=thread->stack_address,top=interrupted.sp;
    if(alternate.flags==1 || ((action.flags&UINT64_C(0x08000000)) && alternate.size)) {
        base=alternate.address;
        if(alternate.flags!=1) top=alternate.address+alternate.size;
    }
    // Keep Darwin's red zone untouched when resuming the interrupted stack.
    // The Linux frame is placed on the selected guest stack; conversion stays
    // on the separately guarded host alternate stack.
    top&=~UINT64_C(15);
    const uint64_t frame_size=ARTBOX_ARM64_UCONTEXT_BYTES+128;
    if(top<base || top-base<frame_size+128+2048) return -12;
    uint64_t frame_address=top-128-frame_size;
    unsigned char *info=(void *)(uintptr_t)frame_address,*frame=info+128;
    error=artbox_signal_context_encode(frame,ARTBOX_ARM64_UCONTEXT_BYTES,&interrupted,&meta);
    if(error) return error;
    memset(info,0,128);
    word(info,event.number,4); word(info+8,event.code,4); word(info+16,event.address,8);
    uint64_t added_mask=action.mask|self,return_mask=original_mask;
    error=artbox_signals_mask_update(thread->kernel,0,&added_mask,NULL);
    if(error) return error;
    const delivery_scope active={thread,alternate};
    const artbox_native_signal_scope scope={{signal_call,(void *)&active},thread->guest_tls};
    const artbox_native_signal_scope *previous,*replaced;
    error=artbox_native_signal_scope_swap(&scope,&previous);
    if(error) goto restore_mask;
    artbox_call_on_stack((void *)(uintptr_t)action.handler,event.number,(uintptr_t)info,(uintptr_t)frame,frame_address);
    error=artbox_native_signal_scope_swap(previous,&replaced);
    if(error || replaced!=&scope) { error=-22; goto restore_mask; }
    uint64_t mask;
    if(read_word(frame+16,8)!=meta.stack_address || read_word(frame+24,4)!=meta.stack_flags ||
        read_word(frame+32,8)!=meta.stack_size) { error=-95; goto restore_mask; }
    error=artbox_signal_context_resume(frame,ARTBOX_ARM64_UCONTEXT_BYTES,&interrupted,&resumed,&mask);
    if(error) goto restore_mask;
    if(!executable(thread,resumed.pc) || !guest_stack(&delivery,resumed.sp,0)) { error=-22; goto restore_mask; }
    return_mask=mask;
restore_mask:;
    int restored=artbox_signals_mask_update(thread->kernel,2,&return_mask,NULL);
    if(restored) return restored; // Never discard a signal queued while the handler ran.
    if(error) return error;
    return artbox_native_signal_apply(host_context,&resumed);
}
int artbox_native_signal_deliver_fault(artbox_native_signal_thread *thread,int host_number,
    const void *host_info,void *host_context) {
    int saved=errno;
    int result=deliver(thread,host_number,host_info,host_context);
    errno=saved;
    return result;
}
