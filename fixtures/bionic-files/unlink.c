// Original ARTBox unlink/lifetime contract. SPDX-License-Identifier: MIT
#include <stddef.h>
#include <stdint.h>
extern int64_t artbox_file_syscall(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int *artbox_file_errno(void);
#define P(x) ((uint64_t)(uintptr_t)(x))
#define CALL(n,a,b,c,d) artbox_file_syscall(n,a,b,c,d,0,0)
#define CHECK(c) do { if (!(c)) return -__LINE__; ++cases; } while (0)
#define ERROR(c,e) ((c)==-1 && *artbox_file_errno()==(e))
static uint64_t word(const unsigned char *p, unsigned count) {
    uint64_t value = 0;
    for (unsigned i=0; i<count; ++i) value |= (uint64_t)p[i] << (i*8);
    return value;
}
int64_t artbox_files_unlink_check(uint64_t page) {
    unsigned cases = 0;
    int64_t memory = artbox_file_syscall(222,0,page*2,3,0x22,UINT64_MAX,0);
    CHECK(memory > 0);
    uint64_t base = (uint64_t)memory, data = base + page;
    struct names { char path[24], dir[5], leaf[16], trailing[24], dot[24], empty[1], old[4], fresh[4]; };
    struct names *n = (void *)(uintptr_t)base;
    *n = (struct names){"data/unlink-case", "data", "unlink-case", "data/unlink-case/",
                        "data/unlink-case/.", "", "old", "new"};
    int64_t dir = CALL(56,UINT64_MAX-99,P(n->dir),0x4000,0);
    CHECK(dir >= 3);
    int64_t file = CALL(56,(uint64_t)dir,P(n->leaf),0xc2,0600);
    CHECK(file >= 3);
    CHECK(CALL(64,(uint64_t)file,P(n->old),3,0) == 3);
    CHECK(ERROR(CALL(35,(uint64_t)dir,P(n->leaf),1,0),22));
    CHECK(ERROR(CALL(35,(uint64_t)dir,0,0,0),14));
    CHECK(ERROR(CALL(35,(uint64_t)dir,P(n->empty),0,0),2));
    CHECK(ERROR(CALL(35,999,P(n->leaf),0,0),9));
    CHECK(ERROR(CALL(35,(uint64_t)file,P(n->leaf),0,0),20));
    CHECK(ERROR(CALL(35,UINT64_MAX-99,P(n->trailing),0,0),20));
    CHECK(ERROR(CALL(35,UINT64_MAX-99,P(n->dot),0,0),20));
    CHECK(ERROR(CALL(35,UINT64_MAX-99,P(n->dir),0,0),21));
    unsigned char *stat = (void *)(uintptr_t)data;
    CHECK(CALL(80,(uint64_t)file,data,0,0)==0 && word(stat+20,4)==1);
    CHECK(CALL(35,(uint64_t)dir,P(n->leaf),0,0)==0);
    CHECK(ERROR(CALL(56,(uint64_t)dir,P(n->leaf),0,0),2));
    CHECK(ERROR(CALL(35,(uint64_t)dir,P(n->leaf),0,0),2));
    CHECK(CALL(80,(uint64_t)file,data,0,0)==0 && word(stat+20,4)==0 && word(stat+48,8)==3);
    CHECK(CALL(62,(uint64_t)file,0,0,0)==0);
    CHECK(CALL(63,(uint64_t)file,data,3,0)==3 && stat[0]=='o' && stat[1]=='l' && stat[2]=='d');
    int64_t fresh = CALL(56,(uint64_t)dir,P(n->leaf),0xc2,0600);
    CHECK(fresh >= 3 && fresh != file);
    CHECK(CALL(64,(uint64_t)fresh,P(n->fresh),3,0)==3);
    CHECK(CALL(62,(uint64_t)file,0,0,0)==0);
    CHECK(CALL(63,(uint64_t)file,data,3,0)==3 && stat[0]=='o' && stat[1]=='l' && stat[2]=='d');
    CHECK(CALL(35,UINT64_MAX-99,P(n->path),0,0)==0);
    CHECK(CALL(80,(uint64_t)fresh,data,0,0)==0 && word(stat+20,4)==0 && word(stat+48,8)==3);
    CHECK(CALL(57,(uint64_t)file,0,0,0)==0);
    CHECK(CALL(57,(uint64_t)fresh,0,0,0)==0);
    CHECK(CALL(57,(uint64_t)dir,0,0,0)==0);
    CHECK(CALL(215,base,page*2,0,0)==0);
    return cases;
}
