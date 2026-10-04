/* Original Linux Binder wait oracle. SPDX-License-Identifier: MIT */
#include "artbox/binder_wire.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

struct waiting_read {
    int fd;
    _Atomic int tid, done;
    uint64_t transfer[6];
    uint32_t command, read[64];
    int result;
};

static void pause_reader(void) {
    const struct timespec delay = {0, 1000000};
    nanosleep(&delay, NULL);
}
static void interrupt_reader(int signal_number) { (void)signal_number; }
static void *read_entry(void *opaque) {
    struct waiting_read *r = opaque;
    sigset_t set;
    sigemptyset(&set); sigaddset(&set, SIGUSR1);
    if (pthread_sigmask(SIG_UNBLOCK, &set, NULL)) {
        r->result = -1;
    } else {
        atomic_store(&r->tid, (int)syscall(SYS_gettid));
        int result = ioctl(r->fd, (unsigned long)ARTBOX_BINDER_WRITE_READ, r->transfer);
        r->result = result < 0 ? -errno : result;
    }
    atomic_store(&r->done, 1);
    return NULL;
}

/* Observe the actual syscall, rather than treating a scheduled worker or an
 * elapsed delay as proof that Binder blocked. Only this native Linux oracle
 * reads host /proc; no guest ABI or production dependency is introduced. */
static int in_binder_read(const struct waiting_read *r) {
    int tid = atomic_load(&r->tid);
    if (!tid) return 0;
    char path[96];
    snprintf(path, sizeof(path), "/proc/self/task/%d/syscall", tid);
    FILE *stream = fopen(path, "r");
    if (!stream) return -1;
    long number = -1;
    unsigned long long fd = 0, request = 0, argument = 0;
    int fields = fscanf(stream, "%ld %llx %llx %llx", &number, &fd, &request, &argument);
    if (fclose(stream)) return -1;
    return fields == 4 && number == SYS_ioctl && fd == (unsigned)r->fd &&
        request == ARTBOX_BINDER_WRITE_READ && argument == (uint64_t)(uintptr_t)r->transfer;
}

static int interrupted_read(const char *path, int enter_looper) {
    struct waiting_read r;
    memset(&r, 0, sizeof(r));
    atomic_init(&r.tid, 0); atomic_init(&r.done, 0);
    r.fd = open(path, O_RDWR | O_CLOEXEC);
    if (r.fd < 0) return -1;
    const size_t length = (size_t)sysconf(_SC_PAGESIZE) * 16;
    void *mapping = mmap(NULL, length, PROT_READ, MAP_PRIVATE, r.fd, 0);
    if (mapping == MAP_FAILED) { close(r.fd); return -1; }
    r.command = ARTBOX_BC_ENTER_LOOPER;
    r.transfer[0] = enter_looper ? sizeof(r.command) : 0;
    r.transfer[2] = (uint64_t)(uintptr_t)&r.command;
    r.transfer[3] = sizeof(r.read);
    r.transfer[5] = (uint64_t)(uintptr_t)r.read;
    pthread_t reader;
    if (pthread_create(&reader, NULL, read_entry, &r)) {
        munmap(mapping, length); close(r.fd); return -1;
    }
    int observed = 0;
    for (unsigned attempt = 0; attempt < 5000 && !atomic_load(&r.done); ++attempt) {
        observed = in_binder_read(&r);
        if (observed) break;
        pause_reader();
    }
    /* Always release a possibly blocked worker before inspecting its output.
     * A broken driver must fail the disposable reference process, never leave
     * an unjoined callback accessing this stack or a named backing object. */
    if (!atomic_load(&r.done) && pthread_kill(reader, SIGUSR1)) _exit(2);
    for (unsigned attempt = 0; attempt < 5000 && !atomic_load(&r.done); ++attempt) pause_reader();
    if (!atomic_load(&r.done) || pthread_join(reader, NULL)) _exit(2);
    int result = observed == 1 && r.result == -EINTR &&
        r.transfer[1] == r.transfer[0] && r.transfer[4] == 0 && r.read[0] == ARTBOX_BR_NOOP ? 0 : -1;
    if (result) fprintf(stderr, "Binder interrupted read: enter=%d observed=%d result=%d write=%llu read=%llu\n",
        enter_looper, observed, r.result, (unsigned long long)r.transfer[1], (unsigned long long)r.transfer[4]);
    if (munmap(mapping, length)) result = -1;
    if (close(r.fd)) result = -1;
    return result;
}

int artbox_native_binder_wait_check(const char *path) {
    struct sigaction action, previous;
    memset(&action, 0, sizeof(action));
    action.sa_handler = interrupt_reader; /* Deliberately no SA_RESTART. */
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGUSR1, &action, &previous)) return -1;
    int result = interrupted_read(path, 0);
    if (!result) result = interrupted_read(path, 1);
    if (sigaction(SIGUSR1, &previous, NULL)) result = -1;
    if (result) return -1;

    /* A bad read pointer faults before the empty-queue check. O_NONBLOCK reports EAGAIN
     * and still writes the leading NOOP, with read_consumed left at zero. */
    int fd = open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return -1;
    uint32_t read[64] = {0};
    uint64_t transfer[6] = {0, 0, 0, sizeof(read), 0, 1};
    int call = ioctl(fd, (unsigned long)ARTBOX_BINDER_WRITE_READ, transfer);
    if (call != -1 || errno != EFAULT || transfer[1] || transfer[4]) result = -1;
    transfer[5] = (uint64_t)(uintptr_t)read;
    call = ioctl(fd, (unsigned long)ARTBOX_BINDER_WRITE_READ, transfer);
    if (call != -1 || errno != EAGAIN || transfer[1] || transfer[4] || read[0] != ARTBOX_BR_NOOP) result = -1;
    if (close(fd)) result = -1;
    return result ? -1 : 4;
}
