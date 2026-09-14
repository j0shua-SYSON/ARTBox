// One original NDK caller for signed Bionic and native Linux file mappings.
#include <stdint.h>
#include <stddef.h>
extern int64_t artbox_file_syscall(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int *artbox_file_errno(void);
#define P(x) ((uint64_t)(uintptr_t)(x))
#define CALL(n,a,b,c,d) artbox_file_syscall(n,a,b,c,d,0,0)
#define MAP(len,prot,flags,fd,offset) artbox_file_syscall(222,0,len,prot,flags,(uint64_t)(fd),offset)
#define CHECK(c) do { if (!(c)) return -__LINE__; ++cases; } while (0)
#define ERROR(c,e) ((c)==-1 && *artbox_file_errno()==(e))
int64_t artbox_file_mapping_check(uint64_t page) {
    unsigned cases = 0;
    int64_t memory = MAP(page,3,0x22,-1,0);
    CHECK(memory > 0);
    unsigned char *buffer = (void*)(uintptr_t)memory;
    const char path[] = "data/mapping";
    for (unsigned i=0; i<sizeof(path); ++i) buffer[i]=(unsigned char)path[i];
    int64_t fd = CALL(56,UINT64_MAX-99,P(buffer),0xc2,0600);
    CHECK(fd >= 3);
    int64_t ro = CALL(56,UINT64_MAX-99,P(buffer),0,0);
    CHECK(ro >= 3);
    int64_t wo = CALL(56,UINT64_MAX-99,P(buffer),1,0);
    CHECK(wo >= 3);
    CHECK(ERROR(MAP(page,1,2,wo,0),13));
    CHECK(CALL(57,(uint64_t)wo,0,0,0)==0);
    for (uint64_t i=0; i<page; ++i) buffer[i]=0x41;
    CHECK(CALL(64,(uint64_t)fd,P(buffer),page,0)==(int64_t)page);
    for (uint64_t i=0; i<page; ++i) buffer[i]=0x42;
    CHECK(CALL(64,(uint64_t)fd,P(buffer),page,0)==(int64_t)page);
    for (uint64_t i=0; i<page; ++i) buffer[i]=0x43;
    CHECK(CALL(64,(uint64_t)fd,P(buffer),page,0)==(int64_t)page);
    CHECK(ERROR(MAP(page,1,2,fd,1),22));
    CHECK(ERROR(MAP(page,1,0,fd,0),22));
    CHECK(ERROR(MAP(page,3,1,ro,0),13));
    int64_t private_map = MAP(page*2,3,2,ro,page);
    CHECK(private_map > 0);
    unsigned char *copy=(void*)(uintptr_t)private_map;
    CHECK(copy[0]==0x42 && copy[page]==0x43);
    int64_t shared_map = MAP(page*3,3,1,fd,0);
    CHECK(shared_map > 0);
    unsigned char *shared=(void*)(uintptr_t)shared_map;
    CHECK(shared[0]==0x41 && shared[page]==0x42 && shared[page*2]==0x43);
    int64_t read_map=MAP(page,1,1,ro,0);
    CHECK(read_map > 0);
    CHECK(ERROR(CALL(226,(uint64_t)read_map,page,3,0),13));
    CHECK(CALL(226,(uint64_t)read_map,page,0,0)==0);
    CHECK(ERROR(CALL(226,(uint64_t)read_map,page,3,0),13));
    CHECK(CALL(226,(uint64_t)read_map,page,1,0)==0);
    copy[0]=0x91;
    CHECK(shared[page]==0x42);
    CHECK(CALL(227,(uint64_t)private_map,page,4,0)==0 && shared[page]==0x42);
    shared[page]=0x77;
    CHECK(CALL(227,(uint64_t)shared_map,page*3,4,0)==0 && copy[0]==0x91);
    CHECK(CALL(62,(uint64_t)ro,page,0,0)==(int64_t)page);
    CHECK(CALL(63,(uint64_t)ro,P(buffer),1,0)==1 && buffer[0]==0x77);
    CHECK(CALL(57,(uint64_t)fd,0,0,0)==0);
    CHECK(CALL(57,(uint64_t)ro,0,0,0)==0);
    CHECK(CALL(233,(uint64_t)private_map,page,4,0)==0 && copy[0]==0x77);
    shared[0]=0x66;
    CHECK(CALL(227,(uint64_t)shared_map,page,4,0)==0 && *(unsigned char*)(uintptr_t)read_map==0x66);
    CHECK(CALL(233,(uint64_t)shared_map,page,4,0)==0 && shared[0]==0x66);
    CHECK(CALL(215,(uint64_t)private_map+page,page,0,0)==0);
    CHECK(ERROR(CALL(226,(uint64_t)private_map+page,page,1,0),12));
    CHECK(artbox_file_syscall(222,(uint64_t)private_map+page,page,3,0x32,UINT64_MAX,0)==private_map+(int64_t)page);
    CHECK(copy[page]==0 && copy[0]==0x77);
    copy[page]=0x99;
    CHECK(CALL(233,(uint64_t)private_map,page*2,4,0)==0 && copy[page]==0 && copy[0]==0x77);
    CHECK(ERROR(CALL(227,(uint64_t)shared_map+1,page,4,0),22));
    CHECK(ERROR(CALL(227,(uint64_t)shared_map,page,5,0),22));
    CHECK(CALL(227,(uint64_t)shared_map,page,0,0)==0);
    CHECK(CALL(215,(uint64_t)private_map,page*2,0,0)==0);
    CHECK(CALL(215,(uint64_t)shared_map,page*3,0,0)==0);
    CHECK(CALL(215,(uint64_t)read_map,page,0,0)==0);
    CHECK(CALL(215,(uint64_t)memory,page,0,0)==0);
    return cases;
}
