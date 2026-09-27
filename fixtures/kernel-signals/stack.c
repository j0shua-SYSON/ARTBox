// Original shared Linux sigaltstack wire contract. SPDX-License-Identifier: MIT
#include <stdint.h>
#include <stddef.h>
extern int64_t artbox_signal_syscall(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
#define P(x) ((uint64_t)(uintptr_t)(x))
#define CALL(a,b) artbox_signal_syscall(132,P(a),P(b),0,0,0,0)
#define CHECK(x) do { if(!(x)) return -__LINE__; ++cases; } while(0)
struct stack64 { uint64_t address; uint32_t flags,padding; uint64_t size; };
int64_t artbox_signal_stack_check(uint64_t page) {
    unsigned cases=0;
    int64_t mapped=artbox_signal_syscall(222,0,page+32768,3,0x22,UINT64_MAX,0);
    CHECK(mapped>0);
    struct data { struct stack64 saved,request,old,observed; } *d=(void *)(uintptr_t)mapped;
    *d=(struct data){0};
    CHECK(CALL(NULL,&d->saved)==0 && (d->saved.flags==2 ||
        (!d->saved.flags && d->saved.address && d->saved.size)));
    d->request=(struct stack64){(uint64_t)mapped+page,0,0,32768};
    CHECK(CALL(&d->request,&d->old)==0 && d->old.flags==d->saved.flags &&
        d->old.address==d->saved.address && d->old.size==d->saved.size);
    CHECK(CALL(NULL,&d->observed)==0 && d->observed.address==d->request.address &&
        d->observed.size==32768 && d->observed.flags==0);
    CHECK(CALL((void *)(uintptr_t)1,NULL)==-14);
    CHECK(CALL(NULL,&d->observed)==0 && d->observed.size==32768);
    d->request.size=32752;
    CHECK(CALL(&d->request,(void *)(uintptr_t)1)==-14);
    CHECK(CALL(NULL,&d->observed)==0 && d->observed.size==32752);
    d->request.size=0;
    CHECK(CALL(&d->request,NULL)==-12);
    CHECK(CALL(NULL,&d->observed)==0 && d->observed.size==32752);
    d->request.size=32768; d->request.flags=0x40000000;
    CHECK(CALL(&d->request,NULL)==-22);
    d->request.flags=1; // Linux accepts SS_ONSTACK as an input mode.
    CHECK(CALL(&d->request,NULL)==0);
    CHECK(CALL(NULL,&d->observed)==0 && d->observed.flags==0 && d->observed.size==32768);
    d->request=(struct stack64){1,2,0,UINT64_MAX};
    CHECK(CALL(&d->request,&d->old)==0 && d->old.address==(uint64_t)mapped+page && d->old.flags==0);
    CHECK(CALL(NULL,&d->observed)==0 && d->observed.address==0 && d->observed.size==0 && d->observed.flags==2);
    CHECK(CALL(&d->saved,NULL)==0);
    CHECK(artbox_signal_syscall(215,(uint64_t)mapped,page+32768,0,0,0,0)==0);
    return cases;
}
