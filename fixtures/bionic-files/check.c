// Original fixed-word file caller, MIT. Reuse one NDK object on both hosts.
#include <stdint.h>
#include <stddef.h>
#if defined(__aarch64__) && defined(__linux__)
#include <fcntl.h>
_Static_assert(O_DIRECTORY == 0x4000, "ARM64 directory flag");
_Static_assert(O_NOFOLLOW == 0x8000, "ARM64 no-follow flag");
_Static_assert(O_DIRECT == 0x10000, "ARM64 direct IO flag");
_Static_assert(O_LARGEFILE == 0x20000, "ARM64 large-file flag");
#endif
extern int64_t artbox_file_syscall(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int *artbox_file_errno(void);
#define P(x) ((uint64_t)(uintptr_t)(x))
#define CALL(n,a,b,c,d) artbox_file_syscall(n,a,b,c,d,0,0)
#define CHECK(c) do { if (!(c)) return -__LINE__; ++cases; } while (0)
#define ERROR(c,e) ((c)==-1 && *artbox_file_errno()==(e))
static uint64_t word(const unsigned char *p, unsigned length) {
    uint64_t value = 0;
    for (unsigned i = 0; i < length; ++i) value |= (uint64_t)p[i] << (i*8);
    return value;
}
int64_t artbox_files_check(uint64_t page) {
    unsigned cases = 0;
    int64_t memory = artbox_file_syscall(222,0,page*3,3,0x22,UINT64_MAX,0);
    CHECK(memory > 0);
    uint64_t base = (uint64_t)memory, data = base + page, boundary = data + page;
    struct names { char path[14], dir[5], leaf[9], dev[10], text[11], xy[3]; };
    struct names *n = (void *)(uintptr_t)base;
    *n = (struct names){"data/syscalls", "data", "syscalls", "/dev/null", "abcdefghij", "XY"};
    int64_t file = CALL(56, UINT64_MAX-99, P(n->path), 0xc2, 0600);
    CHECK(file >= 3);
    CHECK(ERROR(CALL(56, UINT64_MAX-99, P(n->path), 0xc2, 0600), 17));
    CHECK(CALL(64, (uint64_t)file, P(n->text), 10, 0) == 10);
    CHECK(CALL(62, (uint64_t)file, 0, 1, 0) == 10);
    CHECK(CALL(80, (uint64_t)file, data, 0, 0) == 0);
    const unsigned char *stat = (void *)(uintptr_t)data;
    CHECK(word(stat+16,4)==0100600 && word(stat+48,8)==10 && word(stat+20,4)==1);
    CHECK(CALL(62, (uint64_t)file, 0, 0, 0) == 0);
    CHECK(ERROR(CALL(63,(uint64_t)file,0,1,0),14));
    CHECK(CALL(62, (uint64_t)file, 0, 1, 0) == 0);
    CHECK(CALL(63, (uint64_t)file, data, 16, 0) == 10);
    for (unsigned i=0; i<10; ++i) if (((unsigned char *)(uintptr_t)data)[i] != (unsigned char)n->text[i]) return -__LINE__;
    CHECK(CALL(63, (uint64_t)file, data, 1, 0) == 0);
    CHECK(CALL(63, (uint64_t)file, 0, 1, 0) == 0);
    int64_t dir = CALL(56,UINT64_MAX-99,P(n->dir),0x4000,0);
    CHECK(dir >= 3);
    int64_t ro = CALL(56,(uint64_t)dir,P(n->leaf),0,0);
    CHECK(ro >= 3);
    CHECK(ERROR(CALL(64,(uint64_t)ro,P(n->text),1,0),9));
    CHECK(CALL(57,(uint64_t)ro,0,0,0)==0);
    CHECK(ERROR(CALL(57,(uint64_t)ro,0,0,0),9));
    CHECK(CALL(79,(uint64_t)dir,P(n->leaf),data,0)==0 && word(stat+48,8)==10);
    ro = CALL(56,99,P(n->dev),2,0);
    CHECK(ro>=3 && CALL(57,(uint64_t)ro,0,0,0)==0);
    CHECK(ERROR(CALL(63,999,0,0,0),9));
    ro=CALL(56,(uint64_t)dir,P(n->leaf),0x202,0);
    CHECK(ro>=3 && CALL(57,(uint64_t)ro,0,0,0)==0);
    CHECK(CALL(80,(uint64_t)file,data,0,0)==0 && word(stat+48,8)==0);
    CHECK(CALL(80,(uint64_t)dir,data,0,0)==0 && (word(stat+16,4)&0170000)==0040000);
    CHECK(CALL(226,boundary,page,0,0)==0);
    unsigned char *edge=(void *)(uintptr_t)(boundary-2); edge[0]='X'; edge[1]='Y';
    CHECK(CALL(62,(uint64_t)file,0,0,0)==0);
    CHECK(CALL(64,(uint64_t)file,boundary-2,4,0)==2);
    CHECK(CALL(62,(uint64_t)file,0,1,0)==2);
    CHECK(CALL(62,(uint64_t)file,0,0,0)==0);
    CHECK(CALL(63,(uint64_t)file,boundary-2,4,0)==2 && edge[0]=='X' && edge[1]=='Y');
    CHECK(CALL(226,boundary,page,3,0)==0);
    int64_t append=CALL(56,(uint64_t)dir,P(n->leaf),0x402,0);
    CHECK(append>=3);
    CHECK(CALL(62,(uint64_t)append,0,0,0)==0);
    CHECK(CALL(64,(uint64_t)append,P(n->xy),2,0)==2);
    CHECK(CALL(80,(uint64_t)file,data,0,0)==0 && word(stat+48,8)==4);
    CHECK(ERROR(CALL(62,(uint64_t)file,UINT64_MAX,0,0),22));
    CHECK(ERROR(CALL(56,(uint64_t)dir,P(n->leaf),0x4000,0),20));
    CHECK(CALL(57,(uint64_t)dir,0,0,0)==0);
    CHECK(CALL(57,(uint64_t)file,0,0,0)==0);
    CHECK(CALL(57,(uint64_t)append,0,0,0)==0);
    CHECK(CALL(215,base,page*3,0,0)==0);
    return cases;
}
