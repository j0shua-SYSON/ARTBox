// Original Linux ARM64 signal wire codec. SPDX-License-Identifier: MIT
#include "artbox/signal_context.h"
#include <string.h>

enum { MCONTEXT=176, REGISTERS=184, SP=432, PC=440, PSTATE=448,
       FPSIMD=464, FPSIMD_BYTES=528, ESR=992, TERMINATOR=1008 };
static uint64_t get(const unsigned char *p,unsigned bytes) {
    uint64_t value=0;
    for(unsigned i=0;i<bytes;++i) value|=(uint64_t)p[i]<<(8*i);
    return value;
}
static void put(unsigned char *p,uint64_t value,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i) p[i]=(unsigned char)(value>>(8*i));
}
int artbox_signal_context_encode(void *buffer,size_t size,
    const artbox_arm64_signal_state *state,const artbox_signal_frame_info *info) {
    if(!buffer || size<ARTBOX_ARM64_UCONTEXT_BYTES || !state || !info) return -22;
    unsigned char *frame=buffer;
    memset(frame,0,ARTBOX_ARM64_UCONTEXT_BYTES);
    put(frame+16,info->stack_address,8);
    put(frame+24,info->stack_flags,4);
    put(frame+32,info->stack_size,8);
    put(frame+40,info->mask,8);
    put(frame+MCONTEXT,state->fault_address,8);
    for(unsigned i=0;i<31;++i) put(frame+REGISTERS+8*i,state->x[i],8);
    put(frame+SP,state->sp,8); put(frame+PC,state->pc,8); put(frame+PSTATE,state->pstate,8);
    put(frame+FPSIMD,UINT32_C(0x46508001),4); put(frame+FPSIMD+4,FPSIMD_BYTES,4);
    put(frame+FPSIMD+8,state->fpsr,4); put(frame+FPSIMD+12,state->fpcr,4);
    memcpy(frame+FPSIMD+16,state->v,sizeof(state->v));
    put(frame+ESR,UINT32_C(0x45535201),4); put(frame+ESR+4,16,4); put(frame+ESR+8,state->esr,8);
    return 0;
}
int artbox_signal_context_resume(const void *buffer,size_t size,
    const artbox_arm64_signal_state *interrupted,artbox_arm64_signal_state *resume,uint64_t *mask) {
    if(!buffer || size<ARTBOX_ARM64_UCONTEXT_BYTES || !interrupted || !resume || !mask) return -22;
    const unsigned char *frame=buffer;
    if(get(frame+FPSIMD,4)!=UINT32_C(0x46508001) || get(frame+FPSIMD+4,4)!=FPSIMD_BYTES ||
       get(frame+ESR,4)!=UINT32_C(0x45535201) || get(frame+ESR+4,4)!=16 ||
       get(frame+TERMINATOR,8)!=0) return -95;
    uint64_t pc=get(frame+PC,8),sp=get(frame+SP,8);
    if(!pc || !sp || (pc&3) || (sp&15)) return -22;
    artbox_arm64_signal_state result=*interrupted;
    for(unsigned i=0;i<31;++i) if(i!=18) result.x[i]=get(frame+REGISTERS+8*i,8);
    result.pc=pc; result.sp=sp;
    const uint64_t nzcv=UINT64_C(0xf0000000);
    result.pstate=(interrupted->pstate&~nzcv)|(get(frame+PSTATE,8)&nzcv);
    result.fpsr=(uint32_t)get(frame+FPSIMD+8,4); result.fpcr=(uint32_t)get(frame+FPSIMD+12,4);
    memcpy(result.v,frame+FPSIMD+16,sizeof(result.v));
    *resume=result;
    *mask=get(frame+40,8)&~UINT64_C(0x40100);
    return 0;
}
