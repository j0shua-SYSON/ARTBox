#include "artbox/vfs.h"
#include "artbox/native_vm.h"
#include "artbox/native_system.h"
#include <cstdio>
#include <cstring>
#include <vector>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #c); return 1; } } while (0)
int main() {
    artbox_vm_ops ops = artbox_native_vm(); artbox_system_ops system = artbox_native_system();
    artbox_vm *vm = artbox_vm_create(&ops, 16*ops.page_size, 16);
    artbox_vfs *fs = artbox_vfs_create(nullptr, 8); artbox_kernel_thread thread;
    CHECK(vm && fs && !artbox_kernel_thread_init(&thread, vm, &system, 10000, 10000));
    std::vector<unsigned char> argv(ops.page_size+32, 'a'); argv.back()=0;
    CHECK(artbox_vfs_set_commandline(fs,nullptr,1)==-22);
    CHECK(artbox_vfs_set_commandline(fs,argv.data(),65537)==-7);
    CHECK(artbox_vfs_set_commandline(fs,argv.data(),argv.size()-1)==-22);
    CHECK(artbox_vfs_set_commandline(fs,argv.data(),argv.size())==0);
    argv[0]='x'; // Snapshot ownership must not borrow the caller's mutable bytes.
    CHECK(artbox_vfs_set_commandline(fs,argv.data(),argv.size())==-114);
    int64_t allocation=artbox_vm_mmap(vm,0,ops.page_size*3,3,0x22,-1,0); CHECK(allocation>0);
    uint64_t path=static_cast<uint64_t>(allocation), data=path+ops.page_size, hole=data+ops.page_size;
    const char filename[]="/proc/self/cmdline";
    CHECK(!artbox_vm_write(vm,path,filename,sizeof(filename)));
    auto call=[&](uint64_t n,uint64_t a=0,uint64_t b=0,uint64_t c=0,uint64_t d=0) { return artbox_vfs_call(fs,&thread,n,a,b,c,d); };
    int64_t fd=call(56,static_cast<uint64_t>(-100),path,0), other=call(56,static_cast<uint64_t>(-100),path,0);
    CHECK(fd>=3 && other>=3 && fd!=other);
    CHECK(call(63,static_cast<uint64_t>(fd),data,1)==1 && *reinterpret_cast<unsigned char*>(static_cast<uintptr_t>(data))=='a');
    CHECK(call(62,static_cast<uint64_t>(other),0,1)==0);
    CHECK(!artbox_vm_mprotect(vm,hole,ops.page_size,0));
    CHECK(call(63,static_cast<uint64_t>(fd),hole-2,4)==2 && call(62,static_cast<uint64_t>(fd),0,1)==3);
    CHECK(call(63,static_cast<uint64_t>(fd),hole,1)==-14 && call(62,static_cast<uint64_t>(fd),0,1)==3);
    CHECK(call(62,static_cast<uint64_t>(fd),argv.size(),0)==static_cast<int64_t>(argv.size()));
    CHECK(call(63,static_cast<uint64_t>(fd),0,1)==0);
    CHECK(call(62,static_cast<uint64_t>(fd),0,2)==0 && call(63,static_cast<uint64_t>(fd),data,1)==1);
    CHECK(call(64,static_cast<uint64_t>(fd),data,1)==-9);
    CHECK(artbox_vfs_mmap(fs,vm,0,ops.page_size,1,2,fd,0)==-19);
    CHECK(call(57,static_cast<uint64_t>(fd))==0 && call(57,static_cast<uint64_t>(other))==0);
    CHECK(!artbox_vfs_destroy(fs) && !artbox_vm_destroy(vm));
    std::puts("Proc snapshot ownership, independent offsets, partial faults, EOF, seek and mapping rejection passed");
    return 0;
}
