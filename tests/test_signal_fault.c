// Original measured ARM64 fault translation contract. SPDX-License-Identifier: MIT
#include "artbox/signal_fault.h"
#include <stdio.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"fault classification line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(void) {
    artbox_arm64_signal_state state={0}; state.pc=0x10000;
    artbox_vm_fault_info memory={0,0,0};
    artbox_signal_fault out={99,98,97};
    CHECK(artbox_signal_classify_fault(1,0,NULL,&memory,&out)==-22 && out.number==99);
    CHECK(artbox_signal_classify_fault(1,0,&state,&memory,NULL)==-22);
    state.esr=0x92000006; // Measured Darwin null read (native code 2).
    CHECK(!artbox_signal_classify_fault(1,0,&state,&memory,&out) && out.number==11 && out.code==1 && !out.address);
    state.fault_address=0x40000; state.esr=0x92000007; memory.mapped=1;
    CHECK(!artbox_signal_classify_fault(1,0,&state,&memory,&out) && out.number==11 && out.code==2 && out.address==0x40000);
    state.esr=0x9200004f; memory.protection=1; // Measured read-only write.
    CHECK(!artbox_signal_classify_fault(1,0,&state,&memory,&out) && out.number==11 && out.code==2);
    memory.file_backed=1;
    CHECK(!artbox_signal_classify_fault(1,0,&state,&memory,&out) && out.number==11 && out.code==2);
    memory.protection=3; out=(artbox_signal_fault){99,98,97};
    CHECK(artbox_signal_classify_fault(1,0,&state,&memory,&out)==-95 && out.number==99 && out.address==97);
    state.esr=0x92000007; // A readable mapped file fault needs an EOF/I/O contract.
    CHECK(artbox_signal_classify_fault(1,0,&state,&memory,&out)==-95 && out.code==98);
    state.esr=0x92000021; state.fault_address=0x40009;
    CHECK(!artbox_signal_classify_fault(1,0,&state,NULL,&out) && out.number==7 && out.code==1 && out.address==0x40009);
    state.esr=0x92000421; out=(artbox_signal_fault){99,98,97}; // FAR invalid.
    CHECK(artbox_signal_classify_fault(1,0,&state,NULL,&out)==-95 && out.number==99);
    state.esr=0x92000010; // External abort, not a measured translation fault.
    CHECK(artbox_signal_classify_fault(1,0,&state,&memory,&out)==-95 && out.number==99);
    state.esr=0x92000007;
    CHECK(artbox_signal_classify_fault(1,0,&state,NULL,&out)==-22 && out.number==99);
    memory.protection=8;
    CHECK(artbox_signal_classify_fault(1,0,&state,&memory,&out)==-22 && out.number==99);
    state.esr=0x02000000;
    CHECK(!artbox_signal_classify_fault(2,0,&state,NULL,&out) && out.number==4 && out.code==1 && out.address==state.pc);
    CHECK(!artbox_signal_classify_fault(2,0xffff,&state,NULL,&out) && out.number==4);
    out=(artbox_signal_fault){99,98,97};
    CHECK(artbox_signal_classify_fault(2,0xd503201f,&state,NULL,&out)==-95 && out.number==99);
    state.esr=0x92000007;
    CHECK(artbox_signal_classify_fault(2,0,&state,NULL,&out)==-95 && out.number==99);
    CHECK(!artbox_signal_classify_fault(3,0xd4200840,&state,NULL,&out) && out.number==5 && out.code==1 && out.address==state.pc);
    out=(artbox_signal_fault){99,98,97};
    CHECK(artbox_signal_classify_fault(3,0xd503201f,&state,NULL,&out)==-95 && out.number==99);
    CHECK(artbox_signal_classify_fault(4,0,&state,NULL,&out)==-95 && out.number==99);
    printf("ARM64 fault classification: %u checks\n",checks);
    return 0;
}
