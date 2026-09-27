// Original signal-context conversion contract. SPDX-License-Identifier: MIT
#include "artbox/signal_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"signal context line %d: %s\n",__LINE__,#x); abort(); } } while (0)
static uint64_t read_le(const unsigned char *p,unsigned bytes) {
    uint64_t value=0; for(unsigned i=0;i<bytes;++i) value|=(uint64_t)p[i]<<(8*i); return value;
}
static void write_le(unsigned char *p,uint64_t value,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i) p[i]=(unsigned char)(value>>(8*i));
}
int main(void) {
    artbox_arm64_signal_state before={0},after,unchanged;
    artbox_signal_frame_info info={UINT64_MAX,UINT64_C(0x1234567890),65536,1};
    unsigned char storage[ARTBOX_ARM64_UCONTEXT_BYTES+2], saved[ARTBOX_ARM64_UCONTEXT_BYTES];
    unsigned char *frame=storage+1; // Exercise byte encoding without a host struct cast.
    for(unsigned i=0;i<31;++i) before.x[i]=UINT64_C(0xdead000000000000)+i;
    for(unsigned i=0;i<32;++i) for(unsigned j=0;j<16;++j) before.v[i][j]=(unsigned char)(i*7+j);
    before.pc=UINT64_C(0x100012340); before.sp=UINT64_C(0x200056780);
    before.pstate=UINT64_C(0xa0001000); before.fault_address=UINT64_C(0x300012341);
    before.esr=UINT64_C(0x92000006); before.fpsr=0x08000001; before.fpcr=0x00400000;
    memset(storage,0xa5,sizeof(storage));
    CHECK(artbox_signal_context_encode(frame,sizeof(saved)-1,&before,&info)==-22);
    for(size_t i=0;i<sizeof(storage);++i) CHECK(storage[i]==0xa5);
    CHECK(artbox_signal_context_encode(NULL,sizeof(saved),&before,&info)==-22);
    CHECK(artbox_signal_context_encode(frame,sizeof(saved),&before,&info)==0);
    CHECK(storage[0]==0xa5 && storage[sizeof(storage)-1]==0xa5);
    CHECK(read_le(frame,8)==0 && read_le(frame+8,8)==0);
    CHECK(read_le(frame+16,8)==info.stack_address && read_le(frame+24,4)==1 && read_le(frame+32,8)==65536);
    CHECK(read_le(frame+40,8)==UINT64_MAX);
    for(size_t i=48;i<176;++i) CHECK(frame[i]==0);
    CHECK(read_le(frame+176,8)==before.fault_address);
    for(unsigned i=0;i<31;++i) CHECK(read_le(frame+184+8*i,8)==before.x[i]);
    CHECK(read_le(frame+432,8)==before.sp && read_le(frame+440,8)==before.pc && read_le(frame+448,8)==before.pstate);
    CHECK(read_le(frame+464,4)==0x46508001 && read_le(frame+468,4)==528);
    CHECK(read_le(frame+472,4)==before.fpsr && read_le(frame+476,4)==before.fpcr);
    CHECK(!memcmp(frame+480,before.v,sizeof(before.v)));
    CHECK(read_le(frame+992,4)==0x45535201 && read_le(frame+996,4)==16 && read_le(frame+1000,8)==before.esr);
    for(size_t i=1008;i<sizeof(saved);++i) CHECK(frame[i]==0);
    memcpy(saved,frame,sizeof(saved));
    uint64_t mask=0;
    CHECK(artbox_signal_context_resume(frame,sizeof(saved),&before,&after,&mask)==0);
    CHECK(!memcmp(&before,&after,sizeof(before)) && mask==(UINT64_MAX&~UINT64_C(0x40100)));
    write_le(frame+184,123,8); // Handler changes x0, PC, SP, flags and vectors.
    write_le(frame+184+18*8,0,8); // A guest cannot clobber Apple's reserved x18.
    write_le(frame+432,before.sp+16,8); write_le(frame+440,before.pc+4,8);
    write_le(frame+448,UINT64_MAX,8); frame[480]=17;
    write_le(frame+176,0,8); write_le(frame+1000,UINT64_MAX,8);
    write_le(frame+472,0x08000010,4); write_le(frame+476,0x00800000,4);
    CHECK(artbox_signal_context_resume(frame,sizeof(saved),&before,&after,&mask)==0);
    CHECK(after.x[0]==123 && after.x[18]==before.x[18] && after.pc==before.pc+4 && after.sp==before.sp+16);
    CHECK(after.pstate==UINT64_C(0xf0001000) && after.v[0][0]==17);
    CHECK(after.fpsr==0x08000010 && after.fpcr==0x00800000);
    CHECK(after.fault_address==before.fault_address && after.esr==before.esr);
    unchanged=after;
    const unsigned corrupt_offsets[]={468,996,1008};
    for(unsigned i=0;i<3;++i) {
        memcpy(frame,saved,sizeof(saved)); frame[corrupt_offsets[i]]^=1;
        mask=123;
        CHECK(artbox_signal_context_resume(frame,sizeof(saved),&before,&after,&mask)==-95);
        CHECK(!memcmp(&after,&unchanged,sizeof(after)) && mask==123);
    }
    memcpy(frame,saved,sizeof(saved)); write_le(frame+464,0x53564501,4); // SVE is not silently discarded.
    CHECK(artbox_signal_context_resume(frame,sizeof(saved),&before,&after,&mask)==-95);
    memcpy(frame,saved,sizeof(saved)); frame[432]|=1;
    CHECK(artbox_signal_context_resume(frame,sizeof(saved),&before,&after,&mask)==-22);
    memcpy(frame,saved,sizeof(saved)); frame[440]|=2;
    CHECK(artbox_signal_context_resume(frame,sizeof(saved),&before,&after,&mask)==-22);
    CHECK(artbox_signal_context_resume(frame,sizeof(saved)-1,&before,&after,&mask)==-22);
    CHECK(!memcmp(&after,&unchanged,sizeof(after)) && mask==123);
    puts("ARM64 signal frame: wire layout, handler edits, x18 preservation and rejected extensions pass");
    return 0;
}
