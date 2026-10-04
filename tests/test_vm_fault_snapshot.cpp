// Original signal-context VM metadata contract. SPDX-License-Identifier: MIT
#include "artbox/native_vm.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"VM fault line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static artbox_vm_ops native;
static artbox_vm *observed;
static unsigned callbacks, callback_errors;
static bool fail_protect;
static void during_mutation() {
    artbox_vm_fault_info out{77,88,99};
    if(artbox_vm_fault_snapshot(observed,0,&out)!=-11 || out.mapped!=77 ||
       out.protection!=88 || out.file_backed!=99) ++callback_errors;
    ++callbacks;
}
static int reserve(size_t n,void **p) { during_mutation(); return native.reserve(n,p); }
static int protect(void *p,size_t n,unsigned flags) {
    during_mutation();
    int result=native.protect(p,n,flags);
    return fail_protect?-12:result;
}
static int reset(void *p,size_t n,unsigned flags) { during_mutation(); return native.reset(p,n,flags); }
static int release(void *p,size_t n) {
    if(observed) during_mutation();
    return native.release(p,n);
}
static int64_t while_mapper_locked(void *raw,void *buffer,size_t size) {
    artbox_vm_fault_info out{};
    int result=artbox_vm_fault_snapshot(static_cast<artbox_vm *>(raw),reinterpret_cast<uintptr_t>(buffer),&out);
    return !result && out.mapped && out.protection==3 && !out.file_backed ? static_cast<int64_t>(size) : -5;
}
static int file_acquire(void *file,void **ref) { during_mutation(); *ref=file; return 0; }
static void file_release(void *) { if(observed) during_mutation(); }
static int file_map(void *,void *p,size_t n,unsigned prot,unsigned,uint64_t) {
    during_mutation(); return native.protect(p,n,prot);
}
static int file_sync(void *,void *,size_t,unsigned) { return 0; }
static bool equals(const artbox_vm_fault_info &value,unsigned mapped,unsigned prot,unsigned file=0) {
    return value.mapped==mapped && value.protection==prot && value.file_backed==file;
}

int main() {
    CHECK(artbox_vm_fault_snapshot_support());
    native=artbox_native_vm();
    artbox_vm_ops ops=native;
    ops.reserve=reserve; ops.protect=protect; ops.reset=reset; ops.release=release;
    observed=artbox_vm_create(&ops,16*native.page_size,8);
    CHECK(observed);
    artbox_vm *vm=observed;
    const size_t page=native.page_size;
    artbox_vm_fault_info out{77,88,99};
    CHECK(artbox_vm_fault_snapshot(nullptr,0,&out)==-22 && equals(out,77,88,99));
    CHECK(artbox_vm_fault_snapshot(vm,0,nullptr)==-22);
    CHECK(!artbox_vm_fault_snapshot(vm,0,&out) && equals(out,0,0));
    CHECK(!artbox_vm_fault_snapshot(vm,UINT64_MAX,&out) && equals(out,0,0));
    int64_t allocated=artbox_vm_mmap(vm,0,3*page,3,0x22,-1,0);
    CHECK(allocated>0);
    uint64_t base=static_cast<uint64_t>(allocated);
    CHECK(!artbox_vm_fault_snapshot(vm,base+7,&out) && equals(out,1,3));
    CHECK(!artbox_vm_mprotect(vm,base+page,page,0));
    CHECK(!artbox_vm_fault_snapshot(vm,base+page,&out) && equals(out,1,0));
    CHECK(!artbox_vm_munmap(vm,base+2*page,page));
    CHECK(!artbox_vm_fault_snapshot(vm,base+2*page,&out) && equals(out,0,0));
    CHECK(!artbox_vm_fault_snapshot(vm,base+3*page,&out) && equals(out,0,0));
    CHECK(artbox_vm_transfer(vm,base,1,1,while_mapper_locked,vm)==1);
    CHECK(!artbox_vm_madvise(vm,base,page,4));
    CHECK(!artbox_vm_fault_snapshot(vm,base,&out) && equals(out,1,3));
    CHECK(artbox_vm_mmap(vm,base+2*page,page,1,0x32,-1,0)==static_cast<int64_t>(base+2*page));
    CHECK(!artbox_vm_fault_snapshot(vm,base+2*page,&out) && equals(out,1,1));
    CHECK(!artbox_vm_munmap(vm,base,3*page));
    CHECK(!artbox_vm_fault_snapshot(vm,base,&out) && equals(out,0,0));

    const artbox_vm_file_ops file{file_acquire,file_release,file_map,file_sync};
    allocated=artbox_vm_map_file(vm,0,page,1,2,0,vm,&file,3);
    CHECK(allocated>0);
    base=static_cast<uint64_t>(allocated);
    CHECK(!artbox_vm_fault_snapshot(vm,base,&out) && equals(out,1,1,1));
    CHECK(artbox_vm_mmap(vm,base,page,3,0x32,-1,0)==allocated);
    CHECK(!artbox_vm_fault_snapshot(vm,base,&out) && equals(out,1,3));
    CHECK(!artbox_vm_munmap(vm,base,page));

    const unsigned char constant[7]={1,2,3,4,5,6,7};
    CHECK(!artbox_vm_register_readonly(vm,constant,sizeof(constant)));
    base=reinterpret_cast<uintptr_t>(constant);
    CHECK(!artbox_vm_fault_snapshot(vm,base+6,&out) && equals(out,1,1));
    CHECK(!artbox_vm_fault_snapshot(vm,base+7,&out) && equals(out,0,0));
    artbox_reference_window window{};
    CHECK(!artbox_vm_reserve_window(vm,page*8,page,&window));
    CHECK(!artbox_vm_fault_snapshot(vm,window.base,&out) && equals(out,0,0));
    CHECK(!artbox_vm_fault_snapshot(vm,window.base+page,&out) && equals(out,0,0));
    allocated=artbox_vm_mmap_window(vm,window.base,0,page,0,0x22,-1,0);
    CHECK(allocated>0);
    base=static_cast<uint64_t>(allocated);
    CHECK(!artbox_vm_fault_snapshot(vm,base,&out) && equals(out,1,0));
    CHECK(!artbox_vm_munmap(vm,base,page));
    CHECK(!artbox_vm_fault_snapshot(vm,base,&out) && equals(out,0,0));
    CHECK(callbacks>=15 && !callback_errors);

    // Race structural insertion/removal and page metadata against handler-style
    // readers. They may return a coherent result or EAGAIN, never stale pointers.
    std::atomic<bool> done{false};
    std::atomic<unsigned> reads{0},errors{0};
    std::thread reader([&] {
        while(!done.load()) {
            artbox_vm_fault_info value{77,88,99};
            int result=artbox_vm_fault_snapshot(vm,base,&value);
            if(result==-11) { if(!equals(value,77,88,99)) ++errors; }
            else if(result || !(equals(value,0,0) || equals(value,1,0) || equals(value,1,3))) ++errors;
            ++reads;
        }
    });
    for(unsigned i=0;i<1024;++i) {
        int64_t other=artbox_vm_mmap(vm,0,page,3,0x22,-1,0);
        if(other<=0 || artbox_vm_mmap_window(vm,window.base,base,page,0,0x32,-1,0)!=static_cast<int64_t>(base) ||
           artbox_vm_mprotect(vm,base,page,3) || artbox_vm_munmap(vm,base,page) ||
           artbox_vm_munmap(vm,static_cast<uint64_t>(other),page)) ++errors;
        while(reads.load()<i+1) std::this_thread::yield();
    }
    done=true; reader.join();
    CHECK(!errors && reads>=1024 && !callback_errors);
    allocated=artbox_vm_mmap(vm,0,page,3,0x22,-1,0);
    CHECK(allocated>0);
    fail_protect=true;
    CHECK(artbox_vm_mprotect(vm,static_cast<uint64_t>(allocated),page,0)==-12);
    out={77,88,99};
    CHECK(artbox_vm_fault_snapshot(vm,base,&out)==-5 && equals(out,77,88,99));
    observed=nullptr; // Destroy requires all signal readers to have stopped.
    CHECK(!artbox_vm_destroy(vm));
    std::printf("VM fault metadata: %u checks, 1024 writer races, bounded mutation rejection\n",checks);
    return 0;
}
