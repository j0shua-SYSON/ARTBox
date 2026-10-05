/* Original native Linux timer oracle. SPDX-License-Identifier: MIT */
#include "../fixtures/timerfd/check.h"
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
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "native timerfd line %d: %s (errno %d)\n", __LINE__, #x, errno); exit(1); } } while (0)
_Static_assert(sizeof(struct itimerspec) == 32 && offsetof(struct itimerspec, it_value) == 16, "64-bit timer layout");
_Static_assert(sizeof(artbox_timer_spec) == 32 && offsetof(struct timespec, tv_nsec) == 8, "fixture timer layout");
_Static_assert(TFD_NONBLOCK == 0x800 && TFD_CLOEXEC == 0x80000 && TFD_TIMER_ABSTIME == 1 && TFD_TIMER_CANCEL_ON_SET == 2, "timer flags");
#if defined(__aarch64__)
_Static_assert(SYS_timerfd_create == 85 && SYS_timerfd_settime == 86 && SYS_timerfd_gettime == 87, "ARM64 timer syscalls");
#endif
static int n_create(void *unused, uint64_t clock, uint64_t flags) {
    (void)unused; long result = syscall(SYS_timerfd_create, clock, flags); return result < 0 ? -errno : (int)result;
}
static int n_close(void *unused, int fd) { (void)unused; return close(fd) ? -errno : 0; }
static int n_set(void *unused, int fd, unsigned flags, const artbox_timer_spec *value, artbox_timer_spec *old) {
    (void)unused; return syscall(SYS_timerfd_settime, fd, flags, value, old) ? -errno : 0;
}
static int n_get(void *unused, int fd, artbox_timer_spec *value) {
    (void)unused; return syscall(SYS_timerfd_gettime, fd, value) ? -errno : 0;
}
static int64_t n_read(void *unused, int fd, void *output, size_t size) {
    (void)unused; long result = syscall(SYS_read, fd, output, size); return result < 0 ? -errno : result;
}
static int64_t n_write(void *unused, int fd, const void *input, size_t size) {
    (void)unused; long result = syscall(SYS_write, fd, input, size); return result < 0 ? -errno : result;
}
static int64_t n_seek(void *unused, int fd, int64_t offset, unsigned origin) {
    (void)unused; long result = syscall(SYS_lseek, fd, offset, origin); return result < 0 ? -errno : result;
}
static int n_snapshot(void *unused, int fd, unsigned events) {
    (void)unused;
    int ep = epoll_create1(EPOLL_CLOEXEC); if (ep < 0) return -errno;
    struct epoll_event event = {.events = events, .data.u64 = UINT64_C(0x1a2b3c4d5e6f7788)};
    int result = epoll_ctl(ep, EPOLL_CTL_ADD, fd, &event);
    if (result) result = -errno;
    else {
        result = epoll_wait(ep, &event, 1, 0);
        if (result < 0) result = -errno;
        else if (result) result = event.data.u64 == UINT64_C(0x1a2b3c4d5e6f7788) ? (int)event.events : -1;
    }
    if (close(ep)) return -errno;
    return result;
}
static int64_t n_clock(void *unused) {
    (void)unused; struct timespec now; if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
    return (int64_t)now.tv_sec * 1000000000 + now.tv_nsec;
}
static void n_pause(void *unused) { (void)unused; const struct timespec delay = {0, 1000000}; nanosleep(&delay, NULL); }
static void interrupt_worker(int number) { (void)number; }
struct worker { int fd, epoll; atomic_int tid, done; int64_t result; uint64_t ticks; struct epoll_event event; };
static void *entry(void *opaque) {
    struct worker *w = opaque;
    atomic_store(&w->tid, (int)syscall(SYS_gettid));
    if (!w->epoll) w->result = n_read(NULL, w->fd, &w->ticks, 8);
    else {
        long result = syscall(SYS_epoll_pwait, w->fd, &w->event, 1, -1, NULL, 8);
        w->result = result < 0 ? -errno : result;
    }
    atomic_store(&w->done, 1); return NULL;
}
static void observe(const struct worker *w) {
    for (unsigned attempt = 0; attempt < 5000 && !atomic_load(&w->done); ++attempt) {
        int tid = atomic_load(&w->tid);
        if (tid) {
            char path[96]; snprintf(path, sizeof(path), "/proc/self/task/%d/syscall", tid);
            FILE *stream = fopen(path, "r"); CHECK(stream);
            long number = -1; unsigned long long fd = 0, pointer = 0;
            int fields = fscanf(stream, "%ld %llx %llx", &number, &fd, &pointer); CHECK(fclose(stream) == 0);
            if (fields == 3 && number == (w->epoll ? SYS_epoll_pwait : SYS_read) && fd == (unsigned)w->fd &&
                pointer == (w->epoll ? (uintptr_t)&w->event : (uintptr_t)&w->ticks)) return;
        }
        n_pause(NULL);
    }
    CHECK(0 && "native timer wait was not observed in its syscall");
}
static void wait_case(int epoll, int interrupt, int reuse) {
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC); CHECK(fd >= 0);
    int ep = -1, alias = -1;
    if (epoll) {
        ep = epoll_create1(EPOLL_CLOEXEC); CHECK(ep >= 0);
        struct epoll_event event = {.events = EPOLLIN, .data.u64 = UINT64_C(0xfedcabcd98765432)};
        CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, fd, &event) == 0);
    } else if (reuse) { alias = dup(fd); CHECK(alias >= 0); }
    struct worker w; memset(&w, 0, sizeof(w));
    w.fd = epoll ? ep : fd; w.epoll = epoll; atomic_init(&w.tid, 0); atomic_init(&w.done, 0);
    pthread_t thread; CHECK(pthread_create(&thread, NULL, entry, &w) == 0);
    observe(&w);
    int replacement = -1;
    if (reuse) {
        CHECK(close(w.fd) == 0);
        replacement = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK); CHECK(replacement == w.fd);
    }
    if (interrupt) CHECK(pthread_kill(thread, SIGUSR1) == 0);
    else { artbox_timer_spec spec = {0, 0, 0, 10000000}; CHECK(n_set(NULL, alias >= 0 ? alias : fd, 0, &spec, NULL) == 0); }
    for (unsigned attempt = 0; attempt < 5000 && !atomic_load(&w.done); ++attempt) n_pause(NULL);
    CHECK(atomic_load(&w.done) && pthread_join(thread, NULL) == 0);
    if (interrupt) CHECK(w.result == -EINTR);
    else if (epoll) CHECK(w.result == 1 && w.event.events == EPOLLIN && w.event.data.u64 == UINT64_C(0xfedcabcd98765432));
    else CHECK(w.result == 8 && w.ticks == 1);
    if (replacement >= 0) {
        uint64_t ticks = 0; CHECK(n_read(NULL, replacement, &ticks, 8) == -EAGAIN); CHECK(close(replacement) == 0);
    }
    if (alias >= 0) CHECK(close(alias) == 0);
    if (epoll && !reuse) CHECK(close(ep) == 0);
    if (epoll || !reuse) CHECK(close(fd) == 0);
}
static int copy_and_type_controls(void) {
    const long page = sysconf(_SC_PAGESIZE); CHECK(page > 0);
    unsigned char *data = mmap(NULL, (size_t)page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(data != MAP_FAILED && mprotect(data + page, (size_t)page, PROT_NONE) == 0);
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK); CHECK(fd >= 0);
    artbox_timer_spec spec = {0, 0, 0, 1}; CHECK(n_set(NULL, fd, 1, &spec, NULL) == 0);
    int64_t start = n_clock(NULL);
    while (n_snapshot(NULL, fd, 1) == 0 && n_clock(NULL) - start < 2000000000) n_pause(NULL);
    CHECK(n_snapshot(NULL, fd, 1) == 1);
    int64_t partial = n_read(NULL, fd, data + page - 4, 8);
    // copy_to_iter may copy a prefix before fault; the count is architecture-
    // dependent. Either way the timer's pending expiration has been consumed.
    CHECK(partial == -EFAULT || (partial > 0 && partial <= 4));
    uint64_t expected = 1, value = 0;
    if (partial > 0) CHECK(memcmp(data + page - 4, &expected, (size_t)partial) == 0);
    CHECK(n_read(NULL, fd, &value, 8) == -EAGAIN);
    CHECK(close(fd) == 0 && munmap(data, (size_t)page * 2) == 0);
    int wrong = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC); CHECK(wrong >= 0);
    CHECK(n_set(NULL, wrong, 0, &spec, NULL) == -EINVAL && n_get(NULL, wrong, &spec) == -EINVAL);
    CHECK(close(wrong) == 0);
    return partial > 0 ? (int)partial : 0;
}
int main(void) {
    const artbox_timerfd_ops ops = {n_create, n_close, n_set, n_get, n_read, n_write, n_snapshot, n_seek, n_clock, n_pause};
    int cases = artbox_timerfd_check(NULL, &ops); CHECK(cases > 0);
    int partial = copy_and_type_controls();
    struct sigaction action, previous; memset(&action, 0, sizeof(action));
    action.sa_handler = interrupt_worker; sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGUSR1, &action, &previous) == 0);
    for (int epoll = 0; epoll < 2; ++epoll) {
        wait_case(epoll, 0, 0); wait_case(epoll, 1, 0); wait_case(epoll, 0, 1);
    }
    CHECK(sigaction(SIGUSR1, &previous, NULL) == 0);
    printf("{\"shared_cases\":%d,\"partial_copy_bytes\":%d,\"observed_blocking_cases\":6,"
           "\"native_interrupt_cases\":2,\"close_reuse_cases\":2,\"guest_compared\":false,\"passed\":true}\n", cases, partial);
    return 0;
}
