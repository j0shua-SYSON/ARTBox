// Original virtual-root getcwd contract. SPDX-License-Identifier: MIT
#include <stdint.h>
#include <stddef.h>
extern int64_t artbox_file_syscall(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
extern int *artbox_file_errno(void);
#define P(x) ((uint64_t)(uintptr_t)(x))
#define CALL(n,a,b,c,d) artbox_file_syscall(n,a,b,c,d,0,0)
#define ERROR(c,e) ((c)==-1 && *artbox_file_errno()==(e))
#define CHECK(c) do { if(!(c)) return -__LINE__; ++cases; } while(0)
int64_t artbox_files_cwd_check(uint64_t page) {
    unsigned cases=0;
    int64_t memory=artbox_file_syscall(222,0,2*page,3,0x22,UINT64_MAX,0);
    CHECK(memory>0);
    unsigned char *buffer=(void *)(uintptr_t)memory;
    buffer[0]=buffer[1]=buffer[2]=buffer[3]=0x5a;
    CHECK(ERROR(CALL(17,P(buffer),0,0,0),34));
    CHECK(ERROR(CALL(17,P(buffer),1,0,0),34));
    CHECK(buffer[0]==0x5a && buffer[1]==0x5a && buffer[2]==0x5a && buffer[3]==0x5a);
    CHECK(ERROR(CALL(17,0,0,0,0),34));
    CHECK(ERROR(CALL(17,0,1,0,0),34));
    CHECK(ERROR(CALL(17,0,2,0,0),14));
    CHECK(ERROR(CALL(17,1,2,0,0),14));
    CHECK(CALL(17,P(buffer),2,0,0)==2);
    CHECK(buffer[0]=='/' && !buffer[1] && buffer[2]==0x5a && buffer[3]==0x5a);
    CHECK(CALL(17,P(buffer+1),3,0,0)==2);
    CHECK(buffer[0]=='/' && buffer[1]=='/' && !buffer[2] && buffer[3]==0x5a);
    CHECK(CALL(17,P(buffer),UINT64_MAX,0,0)==2);
    CHECK(CALL(17,P(buffer),UINT64_C(1)<<32,0,0)==2);
    CHECK(CALL(226,P(buffer+page),page,0,0)==0);
    CHECK(ERROR(CALL(17,P(buffer+page-1),2,0,0),14));
    CHECK(CALL(226,P(buffer),page,1,0)==0);
    CHECK(ERROR(CALL(17,P(buffer),2,0,0),14));
    CHECK(CALL(226,P(buffer),page,3,0)==0);
    CHECK(CALL(17,P(buffer+page-2),UINT64_MAX,0,0)==2);
    CHECK(buffer[page-2]=='/' && !buffer[page-1]);
    CHECK(CALL(215,P(buffer),2*page,0,0)==0);
    return cases;
}
