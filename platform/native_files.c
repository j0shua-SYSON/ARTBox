#include "artbox/native_files.h"
#if defined(_WIN32)
int artbox_native_files_open(const char *root, artbox_native_files **files) {
    (void)root; if (files) *files = NULL; return -95;
}
artbox_file_ops artbox_native_files_ops(artbox_native_files *files) {
    (void)files; artbox_file_ops ops = {0}; return ops;
}
int artbox_native_files_close(artbox_native_files *files) { (void)files; return -95; }
#else
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
struct artbox_native_files { int root; };
typedef struct file_handle { int fd; } file_handle;
static int error(void) {
    switch (errno) {
        case EPERM: return -1; case ENOENT: return -2; case EINTR: return -4;
        case EBADF: return -9; case EAGAIN: return -11; case ENOMEM: return -12;
        case EACCES: return -13; case EFAULT: return -14; case EBUSY: return -16;
        case EEXIST: return -17; case EXDEV: return -18; case ENODEV: return -19;
        case ENOTDIR: return -20; case EISDIR: return -21; case EINVAL: return -22;
        case ENFILE: return -23; case EMFILE: return -24; case EFBIG: return -27;
        case ENOSPC: return -28; case ESPIPE: return -29; case EROFS: return -30;
        case ENAMETOOLONG: return -36; case ENOSYS: return -38; case ENOTEMPTY: return -39;
        case ELOOP: return -40; case EOVERFLOW: return -75; case ENOTSUP: return -95;
        default: return -5;
    }
}
static int directory_fd(void *context, void *directory) {
    return directory ? ((file_handle *)directory)->fd : ((artbox_native_files *)context)->root;
}
static int component(const char *name) {
    return name && *name && !strchr(name, '/') && strcmp(name, "..");
}
static int open_file(void *context, void *directory, const char *name, uint32_t flags, uint32_t mode, void **out) {
    if (!component(name) || !out) return -22;
    file_handle *file = malloc(sizeof(*file));
    if (!file) return -12;
    int native = (flags & 3) == 0 ? O_RDONLY : (flags & 3) == 1 ? O_WRONLY : O_RDWR;
    // Regular files are unaffected by NONBLOCK; avoid blocking on an unsupported
    // FIFO before its type is checked by the descriptor-table owner.
    native |= O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
    if (flags & 0x40) native |= O_CREAT;
    if (flags & 0x80) native |= O_EXCL;
    if (flags & 0x200) native |= O_TRUNC;
    if (flags & 0x400) native |= O_APPEND;
    if (flags & 0x800) native |= O_NONBLOCK;
    if (flags & 0x10000) native |= O_DIRECTORY;
    int fd = openat(directory_fd(context, directory), name, native, (mode_t)(mode & 0777));
    if (fd < 0) { int result = error(); free(file); return result; }
    file->fd = fd; *out = file;
    return 0;
}
static int close_file(void *handle) {
    file_handle *file = handle;
    int result = close(file->fd) ? error() : 0;
    free(file); return result; // Never retry close and accidentally close a reused FD.
}
static int64_t read_file(void *handle, void *buffer, size_t size) {
    ssize_t result = read(((file_handle *)handle)->fd, buffer, size);
    return result < 0 ? error() : result;
}
static int64_t write_file(void *handle, const void *buffer, size_t size) {
    ssize_t result = write(((file_handle *)handle)->fd, buffer, size);
    return result < 0 ? error() : result;
}
static int64_t seek_file(void *handle, int64_t offset, unsigned origin) {
    if (origin > 2) return -22;
    _Static_assert(sizeof(off_t) == 8, "64-bit file offsets");
    off_t result = lseek(((file_handle *)handle)->fd, (off_t)offset, origin == 0 ? SEEK_SET : origin == 1 ? SEEK_CUR : SEEK_END);
    return result < 0 ? error() : (int64_t)result;
}
static int encode(const struct stat *s, artbox_file_info *out) {
    if (S_ISLNK(s->st_mode)) return -40;
    if (!S_ISREG(s->st_mode) && !S_ISDIR(s->st_mode)) return -95;
    memset(out, 0, sizeof(*out));
    out->device = s->st_dev; out->inode = s->st_ino;
    out->mode = (S_ISDIR(s->st_mode) ? 0040000 : 0100000) | (s->st_mode & 0777);
    out->links = (uint32_t)s->st_nlink; out->uid = out->gid = 10000;
    out->size = (uint64_t)s->st_size; out->blocks = (uint64_t)s->st_blocks;
    out->block_size = (uint32_t)s->st_blksize;
#if defined(__APPLE__)
    out->access_seconds = s->st_atimespec.tv_sec; out->access_nanoseconds = s->st_atimespec.tv_nsec;
    out->modify_seconds = s->st_mtimespec.tv_sec; out->modify_nanoseconds = s->st_mtimespec.tv_nsec;
    out->change_seconds = s->st_ctimespec.tv_sec; out->change_nanoseconds = s->st_ctimespec.tv_nsec;
#else
    out->access_seconds = s->st_atim.tv_sec; out->access_nanoseconds = s->st_atim.tv_nsec;
    out->modify_seconds = s->st_mtim.tv_sec; out->modify_nanoseconds = s->st_mtim.tv_nsec;
    out->change_seconds = s->st_ctim.tv_sec; out->change_nanoseconds = s->st_ctim.tv_nsec;
#endif
    return 0;
}
static int stat_file(void *handle, artbox_file_info *info) {
    struct stat s;
    return fstat(((file_handle *)handle)->fd, &s) ? error() : encode(&s, info);
}
static int stat_at(void *context, void *directory, const char *name, artbox_file_info *info) {
    if (!component(name)) return -22;
    struct stat s;
    return fstatat(directory_fd(context, directory), name, &s, AT_SYMLINK_NOFOLLOW) ? error() : encode(&s, info);
}
int artbox_native_files_open(const char *root, artbox_native_files **out) {
    if (!root || !out) return -22;
    *out = NULL;
    artbox_native_files *files = malloc(sizeof(*files));
    if (!files) return -12;
    files->root = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (files->root < 0) { int result = error(); free(files); return result; }
    *out = files; return 0;
}
artbox_file_ops artbox_native_files_ops(artbox_native_files *files) {
    const artbox_file_ops ops = {files, open_file, close_file, read_file, write_file, seek_file, stat_file, stat_at};
    return ops;
}
int artbox_native_files_close(artbox_native_files *files) {
    if (!files) return -22;
    int result = close(files->root) ? error() : 0;
    free(files); return result;
}
#endif
