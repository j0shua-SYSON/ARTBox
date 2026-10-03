#ifndef ARTBOX_GUEST_DLFCN_H
#define ARTBOX_GUEST_DLFCN_H
#include "artbox/dlfcn.h"
#include "artbox/vm.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct artbox_guest_dlfcn artbox_guest_dlfcn;
typedef struct artbox_guest_dl_thread artbox_guest_dl_thread;
typedef struct artbox_guest_dl_ops {
    void *context;
    /* Invoke precompiled guest code; no mapper lock is held. */
    artbox_elf_result (*invoke)(void *context,uint64_t function,
        const uint64_t arguments[3],uint64_t *result);
    /* Return this thread's existing TLS block, or zero when unallocated.
     * Required when any image has PT_TLS. Must not allocate a TLS block. */
    artbox_elf_result (*tls_data)(void *context,uint64_t module_id,uint64_t *address);
} artbox_guest_dl_ops;

/* The loader, immutable signed group, mapper and callback context outlive this
 * service and its threads. Names are copied into read-only guest data pages.
 * Program headers and symbol names point into the verified signed images. */
artbox_elf_result artbox_guest_dlfcn_create(artbox_dlfcn *loader,
    const artbox_load_group *group,artbox_vm *vm,const artbox_guest_dl_ops *ops,
    artbox_guest_dlfcn **out);
void artbox_guest_dlfcn_destroy(artbox_guest_dlfcn *service);
artbox_elf_result artbox_guest_dl_thread_create(artbox_guest_dlfcn *service,
    artbox_guest_dl_thread **out);
void artbox_guest_dl_thread_destroy(artbox_guest_dl_thread *thread);

/* All integer pointers below belong to the guest address space. Each calling
 * guest thread owns one thread object; callbacks may reenter on that thread.
 * The returned error string remains guest-readable until thread teardown. */
uint64_t artbox_guest_dlopen(artbox_guest_dl_thread *thread,uint64_t name,unsigned flags);
uint64_t artbox_guest_dlsym(artbox_guest_dl_thread *thread,uint64_t handle,
    uint64_t name,uint64_t version,uint64_t caller);
int artbox_guest_dlclose(artbox_guest_dl_thread *thread,uint64_t handle);
uint64_t artbox_guest_dlerror(artbox_guest_dl_thread *thread);
int artbox_guest_dladdr(artbox_guest_dl_thread *thread,uint64_t address,uint64_t output);
int artbox_guest_dl_iterate_phdr(artbox_guest_dl_thread *thread,uint64_t callback,uint64_t data);

#ifdef __cplusplus
}
#endif
#endif
