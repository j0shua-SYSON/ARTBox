#ifndef ARTBOX_VM_H
#define ARTBOX_VM_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct artbox_vm artbox_vm;
/* Native callbacks operate only on non-executable storage. reserve creates
 * inaccessible, zero-on-first-use storage. reset replaces a subrange with
 * fresh anonymous storage at the same address. All errors are Linux errno. */
typedef struct artbox_vm_ops {
    size_t page_size;
    int (*reserve)(size_t length, void **address);
    int (*protect)(void *address, size_t length, unsigned protection);
    int (*reset)(void *address, size_t length, unsigned protection);
    int (*release)(void *address, size_t length);
} artbox_vm_ops;

artbox_vm *artbox_vm_create(const artbox_vm_ops *ops, uint64_t reservation_limit, size_t region_limit);
/* The caller must stop guest access before destroying the address space. */
int artbox_vm_destroy(artbox_vm *space);
int64_t artbox_vm_mmap(artbox_vm *space, uint64_t address, uint64_t length,
                      uint64_t protection, uint64_t flags, int64_t fd, uint64_t offset);
int artbox_vm_mprotect(artbox_vm *space, uint64_t address, uint64_t length, uint64_t protection);
int artbox_vm_munmap(artbox_vm *space, uint64_t address, uint64_t length);
int artbox_vm_madvise(artbox_vm *space, uint64_t address, uint64_t length, int advice);
/* AArch64 Linux memory syscall dispatcher, suitable for a native binding.
 * Other numbers return -ENOSYS; no syscall is forwarded to the host by number. */
int64_t artbox_vm_syscall(void *space, uint64_t number, uint64_t a0, uint64_t a1,
                         uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
/* Register verified writable image data or host thread stack storage. These
 * ranges may be protected but cannot be replaced, discarded or freed here.
 * The owner retains their lifetime until the address space is destroyed. */
int artbox_vm_register_data(artbox_vm *space, void *address, size_t length, unsigned protection);
/* A snapshot of metadata, not a pin against another thread changing a map. */
int artbox_vm_access(artbox_vm *space, uint64_t address, uint64_t length, unsigned required);
uint64_t artbox_vm_reserved_bytes(artbox_vm *space);
#ifdef __cplusplus
}
#endif
#endif
