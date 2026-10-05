/* Original native Linux oracle; no kernel code is distributed. SPDX-License-Identifier: MIT */
#include "../fixtures/eventfd/check.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "native eventfd line %d: %s (errno %d)\n", __LINE__, #x, errno); exit(1); } } while (0)
_Static_assert(EFD_NONBLOCK == 0x800 && EFD_CLOEXEC == 0x80000 && EFD_SEMAPHORE == 1, "eventfd flags");
#if defined(__aarch64__)
_Static_assert(SYS_eventfd2 == 19, "ARM64 eventfd syscall");
#endif
static int n_create(void *unused, uint64_t initial, uint64_t flags) {
    (void)unused;
    long result = syscall(SYS_eventfd2, initial, flags);
    return result < 0 ? -errno : (int)result;
}
static int n_close(void *unused, int fd) { (void)unused; return close(fd) ? -errno : 0; }
static int64_t n_read(void *unused, int fd, void *buffer, size_t count) {
    (void)unused;
    long result = syscall(SYS_read, fd, buffer, count);
    return result < 0 ? -errno : result;
}
static int64_t n_write(void *unused, int fd, const void *buffer, size_t count) {
    (void)unused;
    long result = syscall(SYS_write, fd, buffer, count);
    return result < 0 ? -errno : result;
}
static int64_t n_seek(void *unused, int fd, int64_t offset, unsigned origin) {
    (void)unused;
    long result = syscall(SYS_lseek, fd, offset, origin);
    return result < 0 ? -errno : result;
}
static int n_snapshot(void *unused, int fd, unsigned events) {
    (void)unused;
    int ep = epoll_create1(EPOLL_CLOEXEC);
    if (ep < 0) return -errno;
    struct epoll_event event = {.events = events, .data.u64 = UINT64_C(0xa17b12345678cafe)};
    int result = epoll_ctl(ep, EPOLL_CTL_ADD, fd, &event);
    if (result) result = -errno;
    else {
        result = epoll_wait(ep, &event, 1, 0);
        if (result < 0) result = -errno;
        else if (result) result = event.data.u64 == UINT64_C(0xa17b12345678cafe) ? (int)event.events : -1;
    }
    if (close(ep)) return -errno;
    return result;
}
static void pause_worker(void) { const struct timespec delay = {0, 1000000}; nanosleep(&delay, NULL); }
static void interrupted(int number) { (void)number; }
struct worker {
    int fd, kind;
    atomic_int tid, done;
    int64_t result;
    uint64_t value;
    struct epoll_event event;
};
static void *entry(void *opaque) {
    struct worker *w = opaque;
    atomic_store(&w->tid, (int)syscall(SYS_gettid));
    if (w->kind == 0) w->result = n_read(NULL, w->fd, &w->value, 8);
    else if (w->kind == 1) w->result = n_write(NULL, w->fd, &w->value, 8);
    else {
        long result = syscall(SYS_epoll_pwait, w->fd, &w->event, 1, -1, NULL, 8);
        w->result = result < 0 ? -errno : result;
    }
    atomic_store(&w->done, 1);
    return NULL;
}
/* Observe the worker in the exact kernel syscall; elapsed time or a published
 * thread ID alone is not proof that it blocked. No production /proc dependency. */
static void observe(const struct worker *w) {
    for (unsigned attempt = 0; attempt < 5000 && !atomic_load(&w->done); ++attempt) {
        int tid = atomic_load(&w->tid);
        if (tid) {
            char path[96]; snprintf(path, sizeof(path), "/proc/self/task/%d/syscall", tid);
            FILE *stream = fopen(path, "r"); CHECK(stream != NULL);
            long number = -1; unsigned long long fd = 0, pointer = 0;
            int count = fscanf(stream, "%ld %llx %llx", &number, &fd, &pointer);
            CHECK(fclose(stream) == 0);
            long expected = w->kind == 0 ? SYS_read : w->kind == 1 ? SYS_write : SYS_epoll_pwait;
            uintptr_t buffer = w->kind == 2 ? (uintptr_t)&w->event : (uintptr_t)&w->value;
            if (count == 3 && number == expected && fd == (unsigned)w->fd && pointer == buffer) return;
        }
        pause_worker();
    }
    CHECK(0 && "worker was not observed in its blocking syscall");
}
static void join_worker(pthread_t thread, struct worker *w) {
    for (unsigned attempt = 0; attempt < 5000 && !atomic_load(&w->done); ++attempt) pause_worker();
    CHECK(atomic_load(&w->done) && pthread_join(thread, NULL) == 0);
}
static void wait_case(int kind, int interrupt, int reuse) {
    int fd = eventfd(0, EFD_CLOEXEC); CHECK(fd >= 0);
    uint64_t value = UINT64_MAX - 1;
    if (kind == 1) CHECK(n_write(NULL, fd, &value, 8) == 8);
    int ep = -1, alias = -1;
    if (kind == 2) {
        ep = epoll_create1(EPOLL_CLOEXEC); CHECK(ep >= 0);
        struct epoll_event event = {.events = EPOLLIN, .data.u64 = UINT64_C(0xf123abcd87654321)};
        CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, fd, &event) == 0);
    }
    if (reuse && kind != 2) { alias = dup(fd); CHECK(alias >= 0); }
    struct worker w; memset(&w, 0, sizeof(w));
    w.fd = kind == 2 ? ep : fd; w.kind = kind; w.value = 7;
    atomic_init(&w.tid, 0); atomic_init(&w.done, 0);
    pthread_t thread; CHECK(pthread_create(&thread, NULL, entry, &w) == 0);
    observe(&w);
    int replacement = -1;
    if (reuse) {
        CHECK(close(w.fd) == 0);
        replacement = eventfd(1, EFD_CLOEXEC | EFD_NONBLOCK);
        CHECK(replacement == w.fd);
    }
    if (interrupt) CHECK(pthread_kill(thread, SIGUSR1) == 0);
    else if (kind == 1) {
        CHECK(n_read(NULL, alias >= 0 ? alias : fd, &value, 8) == 8 && value == UINT64_MAX - 1);
    } else {
        value = 5; CHECK(n_write(NULL, alias >= 0 ? alias : fd, &value, 8) == 8);
    }
    join_worker(thread, &w);
    if (interrupt) CHECK(w.result == -EINTR);
    else if (kind == 0) CHECK(w.result == 8 && w.value == 5);
    else if (kind == 1) {
        CHECK(w.result == 8);
        CHECK(n_read(NULL, alias >= 0 ? alias : fd, &value, 8) == 8 && value == 7);
    } else CHECK(w.result == 1 && w.event.events == EPOLLIN && w.event.data.u64 == UINT64_C(0xf123abcd87654321));
    if (replacement >= 0) {
        CHECK(n_read(NULL, replacement, &value, 8) == 8 && value == 1);
        CHECK(n_read(NULL, replacement, &value, 8) == -EAGAIN);
        CHECK(close(replacement) == 0);
    }
    if (alias >= 0) CHECK(close(alias) == 0);
    if (kind == 2 && !reuse) CHECK(close(ep) == 0);
    if (kind == 2 || !reuse) CHECK(close(fd) == 0);
}
static void copy_faults(void) {
    const long page = sysconf(_SC_PAGESIZE); CHECK(page > 0);
    unsigned char *data = mmap(NULL, (size_t)page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(data != MAP_FAILED && mprotect(data + page, (size_t)page, PROT_NONE) == 0);
    int fd = eventfd(11, EFD_NONBLOCK | EFD_CLOEXEC); CHECK(fd >= 0);
    uint64_t value = 0;
    CHECK(n_read(NULL, fd, data + page - 4, 8) == -EFAULT);
    CHECK(n_read(NULL, fd, &value, 8) == -EAGAIN);
    CHECK(n_write(NULL, fd, data + page - 4, 8) == -EFAULT);
    CHECK(n_read(NULL, fd, &value, 8) == -EAGAIN);
    CHECK(close(fd) == 0 && munmap(data, (size_t)page * 2) == 0);
}
static void persistent_interest(void) {
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC), ep = epoll_create1(EPOLL_CLOEXEC);
    CHECK(fd >= 0 && ep >= 0);
    const uint64_t cookie = UINT64_C(0xbadce110fedc1234), new_cookie = ~cookie;
    struct epoll_event event = {.events = EPOLLIN, .data.u64 = cookie}, output[2];
    CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, fd, &event) == 0);
    int alias = dup(fd); CHECK(alias >= 0);
    CHECK(close(fd) == 0);
    int replacement = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC); CHECK(replacement == fd);
    CHECK(epoll_ctl(ep, EPOLL_CTL_MOD, replacement, &event) == -1 && errno == ENOENT);
    event.data.u64 = new_cookie;
    CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, replacement, &event) == 0);
    uint64_t value = 1; CHECK(n_write(NULL, alias, &value, 8) == 8);
    CHECK(n_write(NULL, replacement, &value, 8) == 8);
    CHECK(epoll_wait(ep, output, 2, 0) == 2);
    CHECK(output[0].events == EPOLLIN && output[1].events == EPOLLIN &&
        ((output[0].data.u64 == cookie && output[1].data.u64 == new_cookie) ||
         (output[1].data.u64 == cookie && output[0].data.u64 == new_cookie)));
    CHECK(close(alias) == 0);
    CHECK(epoll_wait(ep, output, 2, 0) == 1 && output[0].data.u64 == new_cookie);
    CHECK(close(replacement) == 0);
    CHECK(epoll_wait(ep, output, 2, 0) == 0 && close(ep) == 0);
}
int main(void) {
    const artbox_eventfd_ops ops = {n_create, n_close, n_read, n_write, n_snapshot, n_seek};
    int cases = artbox_eventfd_check(NULL, &ops); CHECK(cases > 0);
    copy_faults(); persistent_interest();
    struct sigaction action, previous; memset(&action, 0, sizeof(action));
    action.sa_handler = interrupted; sigemptyset(&action.sa_mask); /* No SA_RESTART. */
    CHECK(sigaction(SIGUSR1, &action, &previous) == 0);
    for (int kind = 0; kind < 3; ++kind) {
        wait_case(kind, 0, 0); wait_case(kind, 1, 0); wait_case(kind, 0, 1);
    }
    CHECK(sigaction(SIGUSR1, &previous, NULL) == 0);
    printf("{\"shared_cases\":%d,\"copy_faults\":true,\"persistent_interest\":true,"
        "\"observed_blocking_cases\":9,\"native_interrupt_cases\":3,"
        "\"close_reuse_cases\":3,\"guest_compared\":false,\"passed\":true}\n", cases);
    return 0;
}
