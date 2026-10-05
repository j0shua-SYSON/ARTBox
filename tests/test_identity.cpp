// SPDX-License-Identifier: MIT
#include "artbox/kernel.h"
#include "artbox/native_system.h"
#include "artbox/native_vm.h"
#include <cstdio>
#include <cstring>
#include <thread>
#if defined(__linux__)
#include <cerrno>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"identity line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static int64_t query(artbox_kernel_thread &thread, unsigned number) {
    // All four Linux calls ignore register arguments; they never dereference.
    return artbox_kernel_call(&thread,number,UINT64_MAX,1,UINT64_MAX,1,UINT64_MAX,1);
}
static bool matches(artbox_kernel_thread &thread, const artbox_credentials &expected) {
    return query(thread,174)==expected.uid && query(thread,175)==expected.euid &&
           query(thread,176)==expected.gid && query(thread,177)==expected.egid;
}
int main() {
    const auto memory=artbox_native_vm(); const auto system=artbox_native_system();
    auto *vm=artbox_vm_create(&memory,memory.page_size,1); CHECK(vm);
    artbox_kernel_thread app, manager;
    CHECK(artbox_kernel_thread_init(&app,vm,&system,100,100)==0);
    CHECK(matches(app,{10000,10000,10000,10000}));
    artbox_credentials role={1000,1001,1002,1003};
    CHECK(artbox_kernel_thread_init_with_credentials(&manager,vm,&system,200,201,&role)==0);
    CHECK(matches(manager,role) && matches(app,{10000,10000,10000,10000}));
    role={9,9,9,9}; // Initialization copies the caller's storage.
    CHECK(matches(manager,{1000,1001,1002,1003}));
    role={0,UINT32_MAX-1,UINT32_C(0x80000000),42};
    CHECK(artbox_kernel_thread_init_with_credentials(&manager,vm,&system,200,201,&role)==0);
    CHECK(matches(manager,role)); // Unsigned IDs remain positive in the 64-bit ABI.
    unsigned char before[sizeof(manager)]; std::memcpy(before,&manager,sizeof(manager));
    CHECK(artbox_kernel_thread_init_with_credentials(&manager,vm,&system,200,201,nullptr)==-22);
    const artbox_credentials invalid[]={{UINT32_MAX,1,2,3},{0,UINT32_MAX,2,3},
                                       {0,1,UINT32_MAX,3},{0,1,2,UINT32_MAX}};
    for (const auto &value:invalid) {
        CHECK(artbox_kernel_thread_init_with_credentials(&manager,vm,&system,200,201,&value)==-22);
        CHECK(std::memcmp(&manager,before,sizeof(manager))==0);
    }
    CHECK(query(manager,146)==-38 && query(manager,144)==-38); // No setuid/setgid support.
    CHECK(matches(manager,role));
#if defined(__linux__)
    const long numbers[]={SYS_getuid,SYS_geteuid,SYS_getgid,SYS_getegid};
    const artbox_credentials native={getuid(),geteuid(),getgid(),getegid()};
    CHECK(artbox_kernel_thread_init_with_credentials(&manager,vm,&system,200,201,&native)==0);
    for (unsigned i=0;i<4;++i) {
        errno=E2BIG;
        const long observed=syscall(numbers[i],-1L,1L,-1L,1L,-1L,1L);
        CHECK(observed>=0 && errno==E2BIG && query(manager,174+i)==observed);
    }
    bool inherited=false;
    std::thread worker([&] {
        inherited=getuid()==native.uid && geteuid()==native.euid &&
                  getgid()==native.gid && getegid()==native.egid;
    });
    worker.join(); CHECK(inherited);
#endif
    CHECK(artbox_vm_destroy(vm)==0);
    std::puts("Fixed guest credentials: distinct roles, unsigned IDs and Linux query semantics passed");
    return 0;
}
