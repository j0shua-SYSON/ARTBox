// Original private futex requeue wire contract. SPDX-License-Identifier: MIT
#include <stdint.h>
#include <stddef.h>
extern int64_t artbox_futex_syscall(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
extern int *artbox_futex_errno(void);
#define CHECK(c) do { if(!(c)) return -__LINE__; ++cases; } while(0)
#define ERROR(c,e) ((c)==-1 && *artbox_futex_errno()==(e))
static int64_t requeue(uint64_t from,uint64_t wake,uint64_t move,uint64_t to) {
    return artbox_futex_syscall(98,from,131,wake,move,to,UINT64_MAX);
}
int64_t artbox_futex_requeue_check(void *scratch) {
    unsigned cases=0;
    uint64_t *words=scratch;
    words[0]=UINT64_C(0x1122334455667788); words[1]=UINT64_C(0x8877665544332211);
    uint64_t source=(uintptr_t)words,target=source+8;
    CHECK(requeue(source,0,0,target)==0);
    CHECK(requeue(source,0,INT32_MAX,target)==0);
    CHECK(requeue(source,1,INT32_MAX,source)==0);
    CHECK(requeue(0,0,INT32_MAX,0)==0); // Private keys are not dereferenced.
    CHECK(ERROR(requeue(source+1,0,0,target),22));
    CHECK(ERROR(requeue(source,0,0,target+1),22));
    CHECK(ERROR(requeue(UINT64_MAX-3,0,0,target),14));
    CHECK(ERROR(requeue(source,0,0,UINT64_MAX-3),14));
    CHECK(ERROR(requeue(source+1,UINT64_MAX,0,target),22));
    CHECK(ERROR(requeue(source,0,UINT64_MAX,target),22));
    CHECK(requeue(source,UINT64_C(0x100000001),0,target)==0);
    CHECK(requeue(source,0,UINT64_C(0x100000000),target)==0);
    CHECK(artbox_futex_syscall(98,source,UINT64_C(0x100000083),0,0,target,0)==0);
    CHECK(ERROR(artbox_futex_syscall(98,source,131|256,0,0,target,0),38));
    CHECK(ERROR(requeue(UINT64_MAX-3,0,0,target+1),14)); // Source key checked first.
    CHECK(ERROR(requeue(source+1,0,0,UINT64_MAX-3),22));
    CHECK(requeue(source,INT32_MAX,INT32_MAX,target)==0);
    CHECK(words[0]==UINT64_C(0x1122334455667788) && words[1]==UINT64_C(0x8877665544332211));
    return cases;
}
