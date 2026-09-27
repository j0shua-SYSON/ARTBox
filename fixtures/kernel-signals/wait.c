// Original ARTBox blocked-signal contract. SPDX-License-Identifier: MIT
#include <stddef.h>
#include <stdint.h>
extern int64_t artbox_signal_syscall(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
#define P(x) ((uint64_t)(uintptr_t)(x))
#define CALL(n,a,b,c,d) artbox_signal_syscall(n,a,b,c,d,0,0)
#define CHECK(x) do { if (!(x)) return -__LINE__; ++cases; } while (0)
static uint32_t word(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
int64_t artbox_signal_wait_check(uint64_t page, uint64_t pid, uint64_t tid, uint32_t uid) {
    unsigned cases = 0;
    int64_t mapped = artbox_signal_syscall(222,0,page,3,0x22,UINT64_MAX,0);
    CHECK(mapped > 0);
    struct data {
        uint64_t saved, mask, first, empty;
        int64_t zero[2], invalid[2];
        unsigned char info[130];
    };
    struct data *d = (void *)(uintptr_t)mapped;
    *d = (struct data){0, UINT64_C(0xa00), UINT64_C(0x200), 0, {0,0}, {0,1000000000}, {0}};
    unsigned char *info = d->info + 1; // Linux accepts an unaligned copyout buffer.
    d->info[0] = d->info[129] = 0xa5;
    CHECK(CALL(135,0,P(&d->mask),P(&d->saved),8) == 0);
    CHECK(CALL(137,P(&d->mask),P(info),P(d->zero),8) == -11);
    CHECK(CALL(137,P(&d->mask),P(info),P(d->invalid),8) == -22);
    CHECK(CALL(137,0,P(info),P(d->invalid),8) == -14);
    CHECK(CALL(137,0,0,0,0) == -22);
    CHECK(CALL(137,P(&d->mask),0,1,8) == -14);
    CHECK(CALL(131,0,tid,0,0) == -22);
    CHECK(CALL(131,pid,0,0,0) == -22);
    CHECK(CALL(131,pid,tid,65,0) == -22);
    CHECK(CALL(131,UINT64_C(0x7fffffff),tid,65,0) == -3); // Target lookup precedes signal validation.
    CHECK(CALL(131,pid,UINT64_C(0x7fffffff),0,0) == -3);
    CHECK(CALL(131,pid,tid,0,0) == 0);
    CHECK(CALL(137,P(&d->mask),P(info),P(d->zero),8) == -11); // Probe did not enqueue.
    CHECK(CALL(131,pid,tid,12,0) == 0);
    CHECK(CALL(131,pid,tid,10,0) == 0);
    CHECK(CALL(131,pid,tid,10,0) == 0);
    CHECK(CALL(137,P(&d->mask),P(info),P(d->zero),8) == 10); // Lowest standard signal first.
    CHECK(word(info)==10 && word(info+4)==0 && (int32_t)word(info+8)==-6 &&
          word(info+16)==pid && word(info+20)==uid);
    CHECK(d->info[0]==0xa5 && d->info[129]==0xa5);
    CHECK(CALL(137,P(&d->first),P(info),P(d->zero),8) == -11); // Standard signals coalesce.
    CHECK(CALL(137,P(&d->mask),P(info),P(d->zero),8) == 12);
    CHECK(word(info)==12 && word(info+16)==pid && word(info+20)==uid);
    CHECK(CALL(137,P(&d->mask),P(info),P(d->zero),8) == -11);
    CHECK(CALL(131,pid,tid,10,0) == 0);
    CHECK(CALL(137,P(&d->mask),1,P(d->zero),8) == -14);
    CHECK(CALL(137,P(&d->mask),P(info),P(d->zero),8) == -11); // Failed copyout consumed it.
    CHECK(CALL(131,pid,tid,10,0) == 0);
    CHECK(CALL(137,P(&d->mask),0,P(d->invalid),8) == -22); // Invalid timeout did not consume it.
    CHECK(CALL(137,P(&d->mask),0,P(d->zero),8) == 10);
    CHECK(CALL(137,P(&d->empty),0,P(d->zero),8) == -11);
    CHECK(CALL(135,2,P(&d->saved),0,8) == 0);
    CHECK(CALL(215,(uint64_t)mapped,page,0,0) == 0);
    return cases;
}
