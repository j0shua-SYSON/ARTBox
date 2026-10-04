/* Real Linux Binder reference, private binderfs device. SPDX-License-Identifier: MIT */
#include "../fixtures/binder-device/check.h"
#include "../fixtures/binder-file/check.h"
#include "../fixtures/binder-mapping/check.h"
#include "../fixtures/binder-transaction/check.h"
#include "../fixtures/binder-poll/check.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/android/binderfs.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <sys/wait.h>

int artbox_native_binder_wait_check(const char *path);

struct transaction_context { const char *path; int32_t server_pid, client_pid; int server_fd, nonblocking; };
static int32_t transaction_pid(void *opaque, int32_t role) {
    struct transaction_context *context = opaque;
    return role == 1 ? context->server_pid : context->client_pid;
}
static int transaction_open(void *opaque, int32_t role) {
    struct transaction_context *context = opaque;
    int fd = open(context->path, O_RDWR | O_CLOEXEC | (context->nonblocking ? O_NONBLOCK : 0));
    if (role == 1 && fd >= 0) context->server_fd = fd;
    return fd < 0 ? -errno : fd;
}

static int open_device(void *context) {
    int fd = open(context, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    return fd < 0 ? -errno : fd;
}
static int close_device(void *context, int fd) {
    (void)context;
    return close(fd) ? -errno : 0;
}
static int poll_device(void *context, int fd, unsigned events) {
    (void)context;
    struct pollfd descriptor = {fd, (short)events, 0};
    int result = poll(&descriptor, 1, 0);
    return result < 0 ? -errno : descriptor.revents;
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
static int64_t transaction_ioctl(void *context, int fd, int32_t tid, uint32_t request, uint64_t argument) {
    (void)context; (void)tid; // The native kernel observes the actual calling pthread.
    int result = ioctl(fd, (unsigned long)request, (unsigned long)argument);
    return result < 0 ? -errno : result;
}
static int64_t transaction_map(void *context, int fd, size_t length) {
    (void)context;
    void *address = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
    return address == MAP_FAILED ? -errno : (int64_t)(uintptr_t)address;
}
static int transaction_unmap(void *context, uint64_t address, size_t length) {
    (void)context;
    return munmap((void *)(uintptr_t)address, length) ? -errno : 0;
}
static int transaction_read(void *context, uint64_t address, void *out, size_t length) {
    (void)context; memcpy(out, (const void *)(uintptr_t)address, length); return 0;
}
struct parallel_call { int (*function)(void *); void *argument; int result; };
static void *parallel_entry(void *opaque) {
    struct parallel_call *call = opaque;
    call->result = call->function(call->argument);
    return NULL;
}
static int transaction_parallel(void *context, int (*left)(void *), void *left_arg,
    int (*right)(void *), void *right_arg, int results[2]) {
    struct transaction_context *owner = context;
    // Native reference only. The child opens its own endpoint after fork;
    // inheriting a parent-created Binder open would keep the parent's PID.
    pid_t client = fork();
    if (client < 0) return -errno;
    if (!client) {
        owner->client_pid = (int32_t)getpid();
        // Binder receive VMAs are not inherited across fork. Drop the inherited
        // descriptor too: the child must not keep its parent's owner alive.
        if (close(owner->server_fd)) _exit(2);
        _exit(right(right_arg) ? 1 : 0);
    }
    owner->client_pid = (int32_t)client;
    struct parallel_call call = {left, left_arg, -1};
    pthread_t thread;
    int result = pthread_create(&thread, NULL, parallel_entry, &call);
    int status = 0;
    pid_t waited;
    do { waited = waitpid(client, &status, 0); } while (waited < 0 && errno == EINTR);
    if (!result && pthread_join(thread, NULL)) _exit(2); // Never return with a live callback.
    if (result) return -result;
    if (waited != client) return -1;
    results[1] = WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
    results[0] = call.result;
    return 0;
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
static int64_t map_file(void *context, int fd, uint64_t size, unsigned prot, unsigned flags, uint64_t offset) {
    (void)context;
    void *address = mmap(NULL, (size_t)size, (int)prot, (int)flags, fd, (off_t)offset);
    return address == MAP_FAILED ? -errno : (int64_t)(uintptr_t)address;
}
static int protect_file(void *context, uint64_t address, uint64_t size, unsigned prot) {
    (void)context;
    return mprotect((void *)(uintptr_t)address, (size_t)size, (int)prot) ? -errno : 0;
}
static int unmap_file(void *context, uint64_t address, uint64_t size) {
    (void)context;
    return munmap((void *)(uintptr_t)address, (size_t)size) ? -errno : 0;
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
    const artbox_binder_mapping_ops memory = {(uint64_t)sysconf(_SC_PAGESIZE), map_file, protect_file, unmap_file, open_file};
    int mapping_cases = artbox_binder_mapping_check(argv[2], &ops, &memory);
    if (mapping_cases < 0) return 1;
    struct transaction_context context = {argv[2], (int32_t)getpid(), (int32_t)getpid(), -1, 1};
    const artbox_binder_transaction_ops transactions = {transaction_pid, (uint32_t)geteuid(),
        (size_t)sysconf(_SC_PAGESIZE), transaction_open, close_device, transaction_map, transaction_unmap,
        transaction_ioctl, transaction_read, pause_device, transaction_parallel};
    artbox_binder_transaction_scratch server = {0}, client = {0};
    if (artbox_binder_transaction_same_pid_check(&context, &transactions, &server, &client)) return 1;
    if (artbox_binder_transaction_check(&context, &transactions, &server, &client)) return 1;
    int death_cases = artbox_binder_death_check(&context, &transactions, &server, &client);
    if (death_cases != 3) return 1;
    if (artbox_binder_object_check(&context, &transactions, &server, &client)) return 1;
    if (artbox_binder_oneway_check(&context, &transactions, &server, &client)) return 1;
    int wait_cases = artbox_native_binder_wait_check(argv[2]);
    if (wait_cases != 4) return 1;
    context.nonblocking = 0;
    if (artbox_binder_transaction_check(&context, &transactions, &server, &client)) return 1;
    artbox_binder_poll_scratch poll_scratch;
    int poll_cases = artbox_binder_poll_check(argv[2], &ops, poll_device, &poll_scratch);
    if (poll_cases < 0) return 1;
    printf("{\"protocol\":8,\"cases\":%d,\"file_cases\":%d,\"mapping_cases\":%d,"
           "\"same_pid_rejected\":true,\"threaded_ping_pong\":true,\"death_cases\":%d,"
           "\"object_handle_lifecycle\":true,\"oneway_lifecycle\":true,\"wait_cases\":%d,\"blocking_threaded_ping_pong\":true,"
           "\"poll_cases\":%d,\"fresh_binderfs_context\":true,\"passed\":true}\n", cases, file_cases, mapping_cases, death_cases, wait_cases, poll_cases);
    return 0;
}
