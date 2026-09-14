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

/* A mapping owns an acquired file reference until its last reservation is
 * released. map replaces only mapper-owned storage with a native non-executable
 * file view; sharing is Linux MAP_SHARED=1 or MAP_PRIVATE=2. Callbacks run under
 * the mapping lock and must not reenter the mapper. Errors are Linux errno. */
typedef struct artbox_vm_file_ops {
    int (*acquire)(void *file, void **reference);
    void (*release)(void *reference);
    int (*map)(void *reference, void *address, size_t length, unsigned protection,
               unsigned sharing, uint64_t offset);
    int (*sync)(void *reference, void *address, size_t length, unsigned flags);
} artbox_vm_file_ops;

artbox_vm *artbox_vm_create(const artbox_vm_ops *ops, uint64_t reservation_limit, size_t region_limit);
/* The caller must stop guest access before destroying the address space. */
int artbox_vm_destroy(artbox_vm *space);
int64_t artbox_vm_mmap(artbox_vm *space, uint64_t address, uint64_t length,
                      uint64_t protection, uint64_t flags, int64_t fd, uint64_t offset);
/* File mappings currently select their own address; MAP_FIXED is unsupported.
 * maximum_protection is 1 for read-only shared mappings and 3 otherwise. */
int64_t artbox_vm_map_file(artbox_vm *space, uint64_t address, uint64_t length,
    uint64_t protection, uint64_t flags, uint64_t offset, void *file,
    const artbox_vm_file_ops *ops, unsigned maximum_protection);
int artbox_vm_mprotect(artbox_vm *space, uint64_t address, uint64_t length, uint64_t protection);
int artbox_vm_munmap(artbox_vm *space, uint64_t address, uint64_t length);
int artbox_vm_madvise(artbox_vm *space, uint64_t address, uint64_t length, int advice);
int artbox_vm_msync(artbox_vm *space, uint64_t address, uint64_t length, uint64_t flags);
/* AArch64 Linux memory syscall dispatcher, suitable for a native binding.
 * Other numbers return -ENOSYS; no syscall is forwarded to the host by number. */
int64_t artbox_vm_syscall(void *space, uint64_t number, uint64_t a0, uint64_t a1,
                         uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
/* Register verified writable image data or host thread stack storage. These
 * ranges may be protected but cannot be replaced, discarded or freed here.
 * The owner retains their lifetime until the address space is destroyed. */
int artbox_vm_register_data(artbox_vm *space, void *address, size_t length, unsigned protection);
/* Borrow immutable signed code/constants for syscall reads. No operation may
 * change this range's protection or contents. It need not be page-aligned. */
int artbox_vm_register_readonly(artbox_vm *space, const void *address, size_t length);
/* Copy while holding the mapping lock, preventing a concurrent VM unmap or
 * protection change between validation and access. Host buffers must be valid
 * and separate from the guest range. These do not synchronize guest data races. */
int artbox_vm_read(artbox_vm *space, uint64_t address, void *destination, size_t length);
int artbox_vm_write(artbox_vm *space, uint64_t address, const void *source, size_t length);
/* Atomic callbacks are precompiled native ABI operations on aligned 32-bit
 * guest words. They must not reenter the mapper. Validation and access happen
 * under the mapping lock, including loads from read-only futex words. */
typedef struct artbox_atomic_u32_ops {
    uint32_t (*load_acquire)(const void *address);
    void (*store_release)(void *address, uint32_t value);
} artbox_atomic_u32_ops;
int artbox_vm_load_u32(artbox_vm *space, uint64_t address,
                       const artbox_atomic_u32_ops *ops, uint32_t *value);
int artbox_vm_store_u32(artbox_vm *space, uint64_t address,
                        const artbox_atomic_u32_ops *ops, uint32_t value);
/* Validate and hold a writable word across preparation, then publish value.
 * A failed preparation leaves the word unchanged. prepare must not reenter VM;
 * a successfully started child must wait for its owner to release it. This
 * prevents clone failure from returning with a worker on a freed guest stack. */
int artbox_vm_prepare_store_u32(artbox_vm *space, uint64_t address,
    const artbox_atomic_u32_ops *ops, uint32_t value, int (*prepare)(void *), void *context);
/* Validated I/O while the mapping lock is held. access is 1 for a source or
 * 2 for a destination. The callback sees only valid host memory, returns its
 * byte count/negative Linux errno, and must not reenter VM. This prevents a bad
 * guest read buffer from advancing a backing file's offset before EFAULT. */
int64_t artbox_vm_transfer(artbox_vm *space, uint64_t address, size_t length, unsigned access,
    int64_t (*transfer)(void *context, void *buffer, size_t length), void *context);
/* A snapshot of metadata, not a pin against another thread changing a map. */
int artbox_vm_access(artbox_vm *space, uint64_t address, uint64_t length, unsigned required);
uint64_t artbox_vm_reserved_bytes(artbox_vm *space);
size_t artbox_vm_page_size(const artbox_vm *space);
#ifdef __cplusplus
}
#endif
#endif
