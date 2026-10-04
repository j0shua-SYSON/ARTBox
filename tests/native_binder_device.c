/* Real Linux Binder reference, private binderfs device. SPDX-License-Identifier: MIT */
#include "../fixtures/binder-device/check.h"
#include "../fixtures/binder-file/check.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/android/binderfs.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int open_device(void *context) {
    int fd = open(context, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    return fd < 0 ? -errno : fd;
}
static int close_device(void *context, int fd) {
    (void)context;
    return close(fd) ? -errno : 0;
}
static int64_t call_device(void *context, int fd, uint32_t request, uint64_t argument) {
    (void)context;
    int result = ioctl(fd, (unsigned long)request, (unsigned long)argument);
    int64_t answer = result < 0 ? -errno : result;
    if (answer < 0) fprintf(stderr, "native ioctl 0x%08x -> %lld\n", request, (long long)answer);
    return answer;
}
static void pause_device(void *context) {
    (void)context;
    const struct timespec delay = {0, 1000000};
    nanosleep(&delay, NULL);
}
static int open_file(void *context, uint32_t flags) {
    int native = (int)(flags & 3);
    if (flags & 0x80000) native |= O_CLOEXEC;
    if (flags & 0x800) native |= O_NONBLOCK;
    if (flags & 0x4000) native |= O_DIRECTORY;
    if (flags & 0x40) native |= O_CREAT;
    if (flags & 0x80) native |= O_EXCL;
    int fd = open(context, native, 0600);
    return fd < 0 ? -errno : fd;
}
static int64_t read_file(void *context, int fd, uint64_t buffer, uint64_t size) {
    (void)context;
    ssize_t result = read(fd, (void *)(uintptr_t)buffer, (size_t)size);
    return result < 0 ? -errno : result;
}
static int64_t write_file(void *context, int fd, uint64_t buffer, uint64_t size) {
    (void)context;
    ssize_t result = write(fd, (void *)(uintptr_t)buffer, (size_t)size);
    return result < 0 ? -errno : result;
}
static int64_t seek_file(void *context, int fd, int64_t offset, unsigned origin) {
    (void)context;
    off_t result = lseek(fd, (off_t)offset, (int)origin);
    return result < 0 ? -errno : result;
}
static int type_file(void *context, int fd) {
    (void)context;
    struct stat value;
    return fstat(fd, &value) ? -errno : (int)(value.st_mode & S_IFMT);
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--probe-module-unload")) {
        /* No force flag. The pinned Binder module has no cleanup_module hook;
         * record the real EBUSY instead of pretending it can be unloaded. */
        errno = 0;
        long result = syscall(SYS_delete_module, "binder_linux", O_NONBLOCK);
        int error = errno;
        printf("{\"result\":%ld,\"errno\":%d}\n", result, error);
        return 0;
    }
    if (argc != 3) return 2; /* control path, expected newly-created device path */
    int control = open(argv[1], O_RDONLY | O_CLOEXEC);
    if (control < 0) { perror("binder-control"); return 2; }
    struct binderfs_device device = {0};
    strcpy(device.name, "artbox");
    if (ioctl(control, BINDER_CTL_ADD, &device)) { perror("BINDER_CTL_ADD"); return 2; }
    if (close(control)) return 2;
    const artbox_binder_device_ops ops = {open_device, close_device, call_device, pause_device};
    artbox_binder_device_scratch scratch;
    int cases = artbox_binder_device_check(argv[2], &ops, &scratch);
    if (cases < 0) return 1;
    const artbox_binder_file_ops files = {open_file, close_device, read_file, write_file, seek_file, type_file};
    int file_cases = artbox_binder_file_check(argv[2], &files, &scratch);
    if (file_cases < 0) return 1;
    printf("{\"protocol\":8,\"cases\":%d,\"file_cases\":%d,\"fresh_binderfs_context\":true,\"passed\":true}\n", cases, file_cases);
    return 0;
}
