/* Original Linux Binder epoll oracle. SPDX-License-Identifier: MIT */
#include "../fixtures/binder-poll/check.h"
#include "artbox/binder_wire.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

struct interest { int fd, epoll; };
struct context { const char *path; struct interest interests[4]; };
static uint64_t event_cookie(int fd) { return ((uint64_t)(unsigned)fd << 32) | UINT64_C(0xa17b0042); }
static struct interest *interest_for(struct context *c, int fd) {
    for (unsigned i = 0; i < 4; ++i) if (c->interests[i].fd == fd) return &c->interests[i];
    return NULL;
}
static void pause_wait(void *unused) {
    (void)unused;
    const struct timespec delay = {0, 1000000};
    nanosleep(&delay, NULL);
}
static int epoll_open(void *opaque) {
    struct context *c = opaque;
    struct interest *slot = interest_for(c, -1);
    if (!slot) return -EMFILE;
    int fd = open(c->path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return -errno;
    int ep = epoll_create1(EPOLL_CLOEXEC);
    if (ep < 0) { int error = errno; close(fd); return -error; }
    struct epoll_event event = {.events = EPOLLIN | EPOLLOUT, .data.u64 = event_cookie(fd)};
    if (epoll_ctl(ep, EPOLL_CTL_ADD, fd, &event)) { close(fd); close(ep); return -1; }
    /* A persistent registration has identity; ADD cannot silently replace it. */
    if (epoll_ctl(ep, EPOLL_CTL_ADD, fd, &event) != -1 || errno != EEXIST) {
        close(fd); close(ep); return -1;
    }
    slot->fd = fd; slot->epoll = ep;
    return fd;
}
static int epoll_close(void *opaque, int fd) {
    struct interest *slot = interest_for(opaque, fd);
    if (!slot) return -EBADF;
    int result = close(fd) ? -errno : 0;
    struct epoll_event event;
    /* Closing the last target descriptor removes its interest, including an
     * unread ready event. The epoll descriptor must not keep Binder alive. */
    if (epoll_wait(slot->epoll, &event, 1, 0) != 0) result = -1;
    if (close(slot->epoll)) result = -1;
    slot->fd = slot->epoll = -1;
    return result;
}
static int64_t binder_ioctl(void *unused, int fd, uint32_t request, uint64_t argument) {
    (void)unused;
    int result = ioctl(fd, (unsigned long)request, (unsigned long)argument);
    return result < 0 ? -errno : result;
}
static int epoll_snapshot(void *opaque, int fd, unsigned events) {
    struct interest *slot = interest_for(opaque, fd);
    if (!slot) return -EBADF;
    struct epoll_event event = {.events = events, .data.u64 = event_cookie(fd)};
    /* MOD is also how AOSP Looper repolls after Binder thread teardown. */
    if (epoll_ctl(slot->epoll, EPOLL_CTL_MOD, fd, &event)) return -errno;
    int count = epoll_wait(slot->epoll, &event, 1, 0);
    if (count < 0) return -errno;
    if (!count) return 0;
    return count == 1 && event.data.u64 == event_cookie(fd) ? (int)event.events : -1;
}
static int registration_controls(struct context *c) {
    if (epoll_create1(1) != -1 || errno != EINVAL) return -1;
    int fd = epoll_open(c);
    if (fd < 0) return -1;
    struct interest *slot = interest_for(c, fd);
    struct epoll_event event = {.events = EPOLLIN, .data.u64 = UINT64_C(0xfeedface12345678)};
    int result = 0;
    if (epoll_ctl(slot->epoll, EPOLL_CTL_DEL, fd, NULL)) result = -1;
    if (epoll_wait(slot->epoll, &event, 1, 0) != 0) result = -1;
    if (epoll_ctl(slot->epoll, EPOLL_CTL_DEL, fd, NULL) != -1 || errno != ENOENT) result = -1;
    if (epoll_ctl(slot->epoll, EPOLL_CTL_MOD, fd, &event) != -1 || errno != ENOENT) result = -1;
    if (epoll_ctl(slot->epoll, EPOLL_CTL_ADD, fd, &event)) result = -1;
    if (epoll_wait(slot->epoll, &event, 1, 0) != 1 || event.events != EPOLLIN ||
        event.data.u64 != UINT64_C(0xfeedface12345678)) result = -1;
    if (epoll_close(c, fd)) result = -1;
    return result;
}

struct waiter {
    int fd, epoll, result;
    _Atomic int tid, done;
    struct epoll_event event;
    uint64_t cookie;
};
static void *wait_entry(void *opaque) {
    struct waiter *w = opaque;
    uint32_t version = 0, enter = ARTBOX_BC_ENTER_LOOPER, read[32] = {0};
    uint64_t header[6] = {4, 0, (uint64_t)(uintptr_t)&enter, 0, 0, 0};
    w->result = -1;
    if (binder_ioctl(NULL, w->fd, ARTBOX_BINDER_VERSION, (uint64_t)(uintptr_t)&version) || version != 8 ||
        binder_ioctl(NULL, w->fd, ARTBOX_BINDER_WRITE_READ, (uint64_t)(uintptr_t)header) || header[1] != 4) {
        atomic_store(&w->done, 1); return NULL;
    }
    atomic_store(&w->tid, (int)syscall(SYS_gettid));
    int count = epoll_wait(w->epoll, &w->event, 1, -1);
    if (count == 1 && w->event.events == EPOLLIN && w->event.data.u64 == event_cookie(w->fd)) {
        memset(header, 0, sizeof(header));
        header[3] = sizeof(read); header[5] = (uint64_t)(uintptr_t)read;
        int64_t result = binder_ioctl(NULL, w->fd, ARTBOX_BINDER_WRITE_READ, (uint64_t)(uintptr_t)header);
        uint64_t cookie = 0; memcpy(&cookie, read + 2, 8);
        if (!result && header[4] == 16 && read[0] == ARTBOX_BR_NOOP &&
            read[1] == ARTBOX_BR_DEAD_BINDER && cookie == w->cookie &&
            epoll_wait(w->epoll, &w->event, 1, 0) == 0) w->result = 0;
    }
    atomic_store(&w->done, 1);
    return NULL;
}
static int infinite_wait_syscall(long number, unsigned long long timeout) {
#ifdef SYS_epoll_wait
    if (number == SYS_epoll_wait) return (uint32_t)timeout == UINT32_MAX;
#endif
#ifdef SYS_epoll_pwait
    if (number == SYS_epoll_pwait) return (uint32_t)timeout == UINT32_MAX;
#endif
#ifdef SYS_epoll_pwait2
    if (number == SYS_epoll_pwait2) return timeout == 0; // Null timespec is infinite.
#endif
    return 0;
}
static int in_epoll_wait(const struct waiter *w) {
    int tid = atomic_load(&w->tid);
    if (!tid) return 0;
    char path[96]; snprintf(path, sizeof(path), "/proc/self/task/%d/syscall", tid);
    FILE *stream = fopen(path, "r");
    if (!stream) return -1;
    long number = -1;
    unsigned long long fd = 0, events = 0, count = 0, timeout = 0;
    int fields = fscanf(stream, "%ld %llx %llx %llx %llx", &number, &fd, &events, &count, &timeout);
    if (fclose(stream)) return -1;
    return fields == 5 && infinite_wait_syscall(number, timeout) && fd == (unsigned)w->epoll &&
        events == (uint64_t)(uintptr_t)&w->event && count == 1;
}
static int death_wakeup(struct context *c) {
    int owner = epoll_open(c), observer = epoll_open(c);
    if (owner < 0 || observer < 0) return -1;
    int64_t manager = -EBUSY;
    for (unsigned attempt = 0; attempt < 2000 && manager == -EBUSY; ++attempt) {
        manager = binder_ioctl(NULL, owner, ARTBOX_BINDER_SET_CONTEXT_MGR, 0);
        if (manager == -EBUSY) pause_wait(NULL);
    }
    if (manager) return -1;
    struct waiter w = {.fd = observer, .epoll = interest_for(c, observer)->epoll,
                      .cookie = UINT64_C(0xa17b123456789abc)};
    atomic_init(&w.tid, 0); atomic_init(&w.done, 0);
    uint32_t commands[6] = {ARTBOX_BC_ACQUIRE, 0, ARTBOX_BC_REQUEST_DEATH_NOTIFICATION, 0, 0, 0};
    memcpy(commands + 4, &w.cookie, 8);
    uint64_t header[6] = {sizeof(commands), 0, (uint64_t)(uintptr_t)commands, 0, 0, 0};
    if (binder_ioctl(NULL, observer, ARTBOX_BINDER_WRITE_READ, (uint64_t)(uintptr_t)header) ||
        header[1] != sizeof(commands) || epoll_snapshot(c, observer, EPOLLIN) != 0) return -1;
    pthread_t thread;
    if (pthread_create(&thread, NULL, wait_entry, &w)) return -1;
    int observed = 0;
    for (unsigned attempt = 0; attempt < 5000 && !atomic_load(&w.done); ++attempt) {
        observed = in_epoll_wait(&w);
        if (observed) break;
        pause_wait(NULL);
    }
    /* Release a possibly waiting worker even if observation failed. A stuck
     * worker ends the disposable oracle rather than retaining this stack. */
    int closed = epoll_close(c, owner);
    for (unsigned attempt = 0; attempt < 5000 && !atomic_load(&w.done); ++attempt) pause_wait(NULL);
    if (!atomic_load(&w.done) || pthread_join(thread, NULL)) _exit(2);
    int result = observed == 1 && !closed && !w.result ? 0 : -1;
    if (result) fprintf(stderr, "Binder epoll wake: observed=%d close=%d result=%d\n", observed, closed, w.result);
    if (epoll_close(c, observer)) result = -1;
    return result;
}
int artbox_native_binder_epoll_check(const char *path) {
    struct context context = {.path = path};
    for (unsigned i = 0; i < 4; ++i) context.interests[i].fd = context.interests[i].epoll = -1;
    const artbox_binder_device_ops ops = {epoll_open, epoll_close, binder_ioctl, pause_wait};
    artbox_binder_poll_scratch scratch;
    int cases = artbox_binder_poll_check(&context, &ops, epoll_snapshot, &scratch);
    if (cases != 31 || registration_controls(&context) || death_wakeup(&context)) return -1;
    for (unsigned i = 0; i < 4; ++i) if (context.interests[i].fd != -1 || context.interests[i].epoll != -1) return -1;
    return cases;
}
