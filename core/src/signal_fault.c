// Original ARM64 fault-to-Linux classification. SPDX-License-Identifier: MIT
#include "artbox/signal_fault.h"
int artbox_signal_classify_fault(unsigned kind,uint32_t instruction,
    const artbox_arm64_signal_state *state,const artbox_vm_fault_info *memory,
    artbox_signal_fault *out) {
    if(!state || !out) return -22;
    artbox_signal_fault result={0,0,0};
    if(kind==ARTBOX_FAULT_BREAKPOINT) {
        if((instruction&UINT32_C(0xffe0001f))!=UINT32_C(0xd4200000)) return -95;
        result=(artbox_signal_fault){5,1,state->pc}; // TRAP_BRKPT.
    } else if(kind==ARTBOX_FAULT_UNDEFINED) {
        if((state->esr>>26)!=0 || instruction>>16) return -95;
        result=(artbox_signal_fault){4,1,state->pc}; // UDF -> ILL_ILLOPC.
    } else if(kind==ARTBOX_FAULT_DATA) {
        if((state->esr>>26)!=0x24 || (state->esr&0x400)) return -95;
        const unsigned status=(unsigned)(state->esr&63);
        if(status==0x21) result=(artbox_signal_fault){7,1,state->fault_address}; // BUS_ADRALN.
        else {
            if(!((status>=4 && status<=7) || (status>=13 && status<=15))) return -95;
            if(!memory || memory->mapped>1 || memory->file_backed>1 || (memory->protection&~3u)) return -22;
            const unsigned access=state->esr&0x40 ? 2u:1u;
            if(memory->mapped && (memory->protection&access)) return -95;
            result=(artbox_signal_fault){11,memory->mapped?2u:1u,state->fault_address}; // ACCERR/MAPERR.
        }
    } else return -95;
    *out=result;
    return 0;
}
