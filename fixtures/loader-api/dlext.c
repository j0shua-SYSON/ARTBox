// SPDX-License-Identifier: MIT
#include <android/dlext.h>
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>

// Internal AOSP libdl_android API; its declaration is absent from the public NDK.
extern struct android_namespace_t* android_get_exported_namespace(const char*);

_Static_assert(sizeof(android_dlextinfo)==48, "Android LP64 extension size");
_Static_assert(offsetof(android_dlextinfo,flags)==0, "flags offset");
_Static_assert(offsetof(android_dlextinfo,reserved_addr)==8, "address offset");
_Static_assert(offsetof(android_dlextinfo,reserved_size)==16, "size offset");
_Static_assert(offsetof(android_dlextinfo,relro_fd)==24, "RELRO fd offset");
_Static_assert(offsetof(android_dlextinfo,library_fd)==28, "library fd offset");
_Static_assert(offsetof(android_dlextinfo,library_fd_offset)==32, "file offset");
_Static_assert(offsetof(android_dlextinfo,library_namespace)==40, "namespace offset");
_Static_assert(ANDROID_DLEXT_USE_NAMESPACE==0x200, "namespace flag");

#define CHECK(id, condition) do { if (!(condition)) return -1000-(id); ++cases; } while (0)
#define PROVIDER "libartbox_loader_provider.so"

// Signed-group policy fixture, distinct from the Linux ordinary-loader oracle.
int artbox_loader_dlext_check(unsigned mutation) {
    int cases=0;
    while (dlerror()) {}
    CHECK(0,dlerror()==NULL);
    struct android_namespace_t *ns=android_get_exported_namespace(mutation==1 ? "sphal" : "default");
    CHECK(1,ns!=NULL);
    CHECK(2,android_get_exported_namespace("default")==ns);
    CHECK(3,android_get_exported_namespace("sphal")==NULL);
    CHECK(4,android_get_exported_namespace(NULL)==NULL);
    CHECK(5,dlerror()==NULL);
    void *handle=dlopen(PROVIDER,RTLD_NOW);
    CHECK(6,handle!=NULL);
    CHECK(7,android_dlopen_ext(PROVIDER,RTLD_NOW,NULL)==handle);
    CHECK(8,dlclose(handle)==0);
    android_dlextinfo info={0};
    info.flags=mutation==2 ? ANDROID_DLEXT_FORCE_LOAD : ANDROID_DLEXT_USE_NAMESPACE;
    info.library_namespace=ns;
    void *extended=android_dlopen_ext(PROVIDER,RTLD_NOW,&info);
    if (mutation==2) {
        if (extended) dlclose(extended);
        dlclose(handle);
    }
    CHECK(9,extended==handle);
    CHECK(10,dlclose(handle)==0);
    void *value=dlsym(handle,"artbox_loader_value");
    CHECK(11,value!=NULL);
    CHECK(12,((int(*)(void))value)()==202);
    info.flags=0;
    info.reserved_addr=(void*)UINTPTR_MAX;info.reserved_size=SIZE_MAX;
    info.relro_fd=-1;info.library_fd=-1;info.library_fd_offset=-1;
    info.library_namespace=(struct android_namespace_t*)UINTPTR_MAX;
    CHECK(13,android_dlopen_ext(PROVIDER,RTLD_NOW,&info)==handle);
    CHECK(14,dlclose(handle)==0);
    info.flags=ANDROID_DLEXT_USE_NAMESPACE;info.library_namespace=NULL;
    CHECK(15,android_dlopen_ext(PROVIDER,RTLD_NOW,&info)==NULL);
    CHECK(16,dlerror()!=NULL && dlerror()==NULL);
    info.library_namespace=(struct android_namespace_t*)handle;
    CHECK(17,android_dlopen_ext(PROVIDER,RTLD_NOW,&info)==NULL);
    CHECK(18,dlerror()!=NULL && dlerror()==NULL);
    info.library_namespace=ns;
    CHECK(19,android_dlopen_ext("absent.so",RTLD_NOW,&info)==NULL);
    CHECK(20,android_get_exported_namespace("default")==ns && android_get_exported_namespace("sphal")==NULL);
    CHECK(21,dlerror()!=NULL && dlerror()==NULL);
    CHECK(22,android_dlopen_ext(PROVIDER,(int)0x80000000u,&info)==NULL);
    CHECK(23,dlerror()!=NULL && dlerror()==NULL);
    void *root=android_dlopen_ext(NULL,RTLD_NOW,&info);
    CHECK(24,root!=NULL);
    CHECK(25,dlclose(root)==0);
    CHECK(26,dlclose(handle)==0);
    CHECK(27,dlclose(handle)==-1);
    CHECK(28,dlerror()!=NULL && dlerror()==NULL);
    const uint64_t unsupported[]={ANDROID_DLEXT_RESERVED_ADDRESS,ANDROID_DLEXT_RESERVED_ADDRESS_HINT,
        ANDROID_DLEXT_WRITE_RELRO,ANDROID_DLEXT_USE_RELRO,ANDROID_DLEXT_USE_LIBRARY_FD,
        ANDROID_DLEXT_USE_LIBRARY_FD_OFFSET,ANDROID_DLEXT_FORCE_LOAD,0x80,0x100,
        ANDROID_DLEXT_RESERVED_ADDRESS_RECURSIVE,UINT64_C(1)<<63};
    for (unsigned i=0;i<sizeof(unsupported)/sizeof(unsupported[0]);++i) {
        info.flags=unsupported[i]|ANDROID_DLEXT_USE_NAMESPACE;
        CHECK(100+(int)i*2,android_dlopen_ext(PROVIDER,RTLD_NOW,&info)==NULL);
        CHECK(101+(int)i*2,dlerror()!=NULL && dlerror()==NULL);
    }
    return cases; // 29 ordinary/namespace checks plus 22 rejection/error checks.
}
