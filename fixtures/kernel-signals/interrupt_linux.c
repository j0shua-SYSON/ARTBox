// Native Linux reference for realtime interruption. SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
extern int64_t artbox_signal_realtime_check(uint64_t,uint64_t,uint64_t,uint32_t);
extern int artbox_signal_interrupt_check(int);
int64_t artbox_signal_syscall(uint64_t n,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f) {
    errno=0;
    long result=syscall((long)n,a,b,c,d,e,f);
    return result==-1 ? -errno : result;
}
int main(int argc,char **argv) {
    if(argc>2 || (argc==2 && strcmp(argv[1],"--drop-send"))) return 2;
    int64_t queued=artbox_signal_realtime_check(sysconf(_SC_PAGESIZE),getpid(),syscall(SYS_gettid),getuid());
    if(queued!=26) { fprintf(stderr,"realtime queue contract: %" PRId64 "\n",queued); return 1; }
    int result=artbox_signal_interrupt_check(argc==2);
    if(result!=4) { fprintf(stderr,"signal interruption contract: %d\n",result); return 1; }
    puts("{\"realtime_cases\":26,\"interruption_groups\":4,\"workers\":1}");
    return 0;
}
