#ifndef ARTBOX_VFS_H
#define ARTBOX_VFS_H
#include "artbox/kernel.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Host-side metadata; the core encodes Linux ARM64 stat bytes explicitly. */
typedef struct artbox_file_info {
    uint64_t device, inode, rdevice, size, blocks;
    uint32_t mode, links, uid, gid, block_size;
    int64_t access_seconds, access_nanoseconds, modify_seconds, modify_nanoseconds,
            change_seconds, change_nanoseconds;
} artbox_file_info;
typedef struct artbox_file_ops {
    void *context;
    /* A NULL directory selects the preopened root. name is one component,
     * never an absolute path or '..'. Reject symlinks at every component.
     * Flags use Linux ARM64 values; handles never become guest descriptors. */
    int (*open)(void *context, void *directory, const char *name, uint32_t flags, uint32_t mode, void **file);
    int (*close)(void *file);
    int64_t (*read)(void *file, void *buffer, size_t size);
    int64_t (*write)(void *file, const void *buffer, size_t size);
    int64_t (*seek)(void *file, int64_t offset, unsigned origin);
    int (*stat)(void *file, artbox_file_info *info);
    int (*stat_at)(void *context, void *directory, const char *name, artbox_file_info *info);
    artbox_vm_file_ops mapping; // Optional; a zero table rejects file mappings.
} artbox_file_ops;
typedef struct artbox_vfs artbox_vfs;
/* The root/provider outlives this table. A NULL provider gives only virtual
 * devices. One table is shared by the guest process. Initial cwd is '/'. */
artbox_vfs *artbox_vfs_create(const artbox_file_ops *files, size_t descriptor_limit);
/* Set the initial argv snapshot once, before guest execution. Bytes include
 * their final NUL and are copied (maximum 64 KiB); no host proc data is read. */
int artbox_vfs_set_commandline(artbox_vfs *fs, const void *bytes, size_t length);
/* Stop guest access before destroy. All descriptors close, even on an error. */
int artbox_vfs_destroy(artbox_vfs *fs);
int64_t artbox_vfs_call(artbox_vfs *fs, artbox_kernel_thread *thread, uint64_t number,
                        uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3);
/* mmap dispatches anonymous storage to VM, otherwise pins the guest descriptor
 * while the mapping acquires its independent native backing reference. */
int64_t artbox_vfs_mmap(artbox_vfs *fs, artbox_vm *vm, uint64_t address,
    uint64_t length, uint64_t protection, uint64_t flags, int64_t fd, uint64_t offset);
#ifdef __cplusplus
}
#endif
#endif
