// Native Linux runner for the shared ARM64 signal ABI caller. SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>
#if !defined(__aarch64__)
#error This raw syscall-number fixture requires native ARM64 Linux.
#endif
extern int64_t artbox_signal_wait_check(uint64_t,uint64_t,uint64_t,uint32_t);
int64_t artbox_signal_syscall(uint64_t n,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f) {
    errno=0;
    long result=syscall((long)n,a,b,c,d,e,f);
    return result==-1 ? -errno : result;
}
int main(void) {
    int64_t result=artbox_signal_wait_check((uint64_t)sysconf(_SC_PAGESIZE),
        (uint64_t)getpid(),(uint64_t)syscall(SYS_gettid),(uint32_t)getuid());
    if (result!=33) { fprintf(stderr,"signal wait contract: %" PRId64 "\n",result); return 1; }
    puts("{\"cases\":33,\"result\":33}");
    return 0;
}
