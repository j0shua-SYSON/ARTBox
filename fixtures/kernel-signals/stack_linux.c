// Native Linux runner for alternate-stack behavior. SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/auxv.h>
#include <unistd.h>
extern int64_t artbox_signal_stack_check(uint64_t);
extern int artbox_signal_stack_handler_check(int,uintptr_t,size_t);
int64_t artbox_signal_syscall(uint64_t n,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f) {
    errno=0;
    long result=syscall((long)n,a,b,c,d,e,f);
    return result==-1?-errno:result;
}
int main(int argc,char **argv) {
    int drop=argc==2 && !strcmp(argv[1],"--drop-onstack");
    if(argc>2 || (argc==2 && !drop)) return 64;
    pthread_attr_t attributes;
    void *base;
    size_t size;
    if(pthread_getattr_np(pthread_self(),&attributes) || pthread_attr_getstack(&attributes,&base,&size) ||
        pthread_attr_destroy(&attributes)) return 2;
    int64_t wire=artbox_signal_stack_check((uint64_t)sysconf(_SC_PAGESIZE));
    int handler=artbox_signal_stack_handler_check(drop,(uintptr_t)base,size);
    if(wire!=17 || handler!=24) {
        fprintf(stderr,"signal stack contract: wire=%" PRId64 " handler=%d\n",wire,handler);
        return 1;
    }
    printf("{\"wire_cases\":17,\"handler_cases\":24,\"workers\":1,\"kernel_minimum_bytes\":%lu}\n",getauxval(51));
    return 0;
}
