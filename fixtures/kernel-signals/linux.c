// Original ARTBox native Linux signal-boundary probe. SPDX-License-Identifier: MIT
// Native ARM64 Linux reference only; this does not implement guest signals.
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <ucontext.h>

struct action64 { uint64_t handler, flags, restorer, mask; };
struct stack64 { uint64_t address; int32_t flags; uint32_t padding; uint64_t size; };
_Static_assert(sizeof(struct action64) == 32, "ARM64 kernel sigaction");
_Static_assert(sizeof(struct stack64) == 24, "ARM64 kernel stack_t");
_Static_assert(sizeof(siginfo_t) == 128, "Linux siginfo payload");
_Static_assert(ATOMIC_LLONG_LOCK_FREE == 2, "Signal observations require lock-free words");
static unsigned checks;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "signal probe line %d: %s errno=%d\n", __LINE__, #x, errno); return 1; } ++checks; } while (0)
static int64_t call6(long n, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    errno = 0;
    long r = syscall(n, a, b, c, d, e, f);
    return r == -1 ? -errno : r;
}
static int64_t action(int signal, const struct action64 *in, struct action64 *out) {
    return call6(SYS_rt_sigaction, (uint64_t)signal, (uintptr_t)in, (uintptr_t)out, 8, 0, 0);
}
static int64_t alternate(const struct stack64 *in, struct stack64 *out) {
    return call6(SYS_sigaltstack, (uintptr_t)in, (uintptr_t)out, 0, 0, 0, 0);
}
static int64_t wait_for(const uint64_t *mask, siginfo_t *info, const struct timespec *timeout) {
    return call6(SYS_rt_sigtimedwait, (uintptr_t)mask, (uintptr_t)info, (uintptr_t)timeout, 8, 0, 0);
}
static atomic_int worker_tid;
static int64_t worker_result;
static siginfo_t worker_info;
static volatile sig_atomic_t delivered, handler_signo, handler_code, handler_pid;
static volatile sig_atomic_t handler_mask_result, handler_stack_result;
static _Atomic uint64_t handler_mask, handler_stack_pointer, interrupted_pc, interrupted_sp;
static _Atomic uint64_t handler_stack_address, handler_stack_size;
static volatile sig_atomic_t handler_stack_flags;
static void capture_signal(int signal, siginfo_t *info, void *context) {
    int saved_errno = errno;
    char marker;
    uint64_t blocked = 0;
    struct stack64 stack = {0};
    handler_signo = signal;
    handler_code = info->si_code;
    handler_pid = info->si_pid;
    handler_stack_pointer = (uintptr_t)&marker;
    handler_mask_result = (int)call6(SYS_rt_sigprocmask, 2, 0, (uintptr_t)&blocked, 8, 0, 0);
    handler_mask = blocked;
    handler_stack_result = (int)alternate(NULL, &stack);
    handler_stack_address = stack.address;
    handler_stack_size = stack.size;
    handler_stack_flags = stack.flags;
    ucontext_t *interrupted = context;
    interrupted_pc = interrupted->uc_mcontext.pc;
    interrupted_sp = interrupted->uc_mcontext.sp;
    delivered = 1;
    errno = saved_errno;
}
static void *waiter(void *unused) {
    (void)unused;
    const uint64_t mask = UINT64_C(1) << (SIGUSR1-1);
    const struct timespec deadline = {5, 0};
    atomic_store_explicit(&worker_tid, (int)syscall(SYS_gettid), memory_order_release);
    worker_result = wait_for(&mask, &worker_info, &deadline);
    return NULL;
}
int main(int argc, char **argv) {
    int drop_worker = argc == 2 && strcmp(argv[1], "--drop-worker-signal") == 0;
    if (argc > 2 || (argc == 2 && !drop_worker)) return 64;
    struct action64 saved, ignored = {1, 0, 0, UINT64_MAX}, observed;
    const uint64_t mask = UINT64_C(1) << (SIGUSR1-1);
    uint64_t previous;
    CHECK(call6(SYS_rt_sigprocmask, 0, (uintptr_t)&mask, (uintptr_t)&previous, 8, 0, 0) == 0);
    CHECK(action(SIGUSR1, NULL, &saved) == 0);
    CHECK(action(SIGUSR1, &ignored, NULL) == 0);
    CHECK(action(SIGUSR1, NULL, &observed) == 0);
    CHECK(observed.handler == 1 && observed.mask == (UINT64_MAX & ~UINT64_C(0x40100)));
    CHECK(action(0, &ignored, NULL) == -EINVAL);
    CHECK(action(65, &ignored, NULL) == -EINVAL);
    CHECK(action(SIGKILL, &ignored, NULL) == -EINVAL);
    CHECK(action(SIGSTOP, &ignored, NULL) == -EINVAL);
    CHECK(action(SIGUSR1, (void *)(uintptr_t)1, NULL) == -EFAULT);
    CHECK(call6(SYS_rt_sigaction, SIGUSR1, 0, 0, 0, 0, 0) == -EINVAL);
    // Copyout failure must be observed after any Linux state mutation.
    ignored.mask = 0;
    CHECK(action(SIGUSR1, &ignored, (void *)(uintptr_t)1) == -EFAULT);
    CHECK(action(SIGUSR1, NULL, &observed) == 0 && observed.mask == 0);
    ignored.flags = UINT64_C(0x1ffffffff);
    CHECK(action(SIGUSR1, &ignored, NULL) == 0);
    CHECK(action(SIGUSR1, NULL, &observed) == 0);
    const uint64_t accepted_action_flags = observed.flags;
    CHECK(action(SIGUSR1, &saved, NULL) == 0);

    struct stack64 original_stack, active, current;
    CHECK(alternate(NULL, &original_stack) == 0);
    void *memory = malloc(128 * 1024);
    CHECK(memory != NULL);
    active = (struct stack64){(uintptr_t)memory, 0, 0, 128 * 1024};
    CHECK(alternate(&active, NULL) == 0);
    CHECK(alternate(NULL, &current) == 0 && current.address == active.address && current.size == active.size && current.flags == 0);
    active.flags = 0x40000000;
    CHECK(alternate(&active, NULL) == -EINVAL);
    CHECK(alternate((void *)(uintptr_t)1, NULL) == -EFAULT);
    active.flags = 0;
    active.size = 0;
    CHECK(alternate(&active, NULL) == -ENOMEM);
    // Measure the kernel threshold instead of using libc's dynamic MINSIGSTKSZ.
    uint64_t lower = 0, upper = 128 * 1024;
    while (upper - lower > 1) {
        active.size = lower + (upper-lower)/2;
        int64_t r = alternate(&active, NULL);
        CHECK(r == 0 || r == -ENOMEM);
        if (r == 0) upper = active.size; else lower = active.size;
    }
    active.size = 128 * 1024;
    CHECK(alternate(&active, (void *)(uintptr_t)1) == -EFAULT);
    CHECK(alternate(NULL, &current) == 0 && current.size == active.size);

    // Exercise the handler on real Linux: signal-safe queries, context and
    // alternate-stack state are observed before implementing a host adapter.
    struct action64 handler = {(uintptr_t)capture_signal, SA_SIGINFO | SA_ONSTACK | SA_RESTART, 0,
                               UINT64_C(1) << (SIGUSR2 - 1)};
    CHECK(action(SIGUSR1, &handler, NULL) == 0);
    CHECK(call6(SYS_rt_sigprocmask, 1, (uintptr_t)&mask, 0, 8, 0, 0) == 0);
    CHECK(call6(SYS_tgkill, getpid(), (uint64_t)syscall(SYS_gettid), SIGUSR1, 0, 0, 0) == 0);
    CHECK(delivered && handler_signo == SIGUSR1 && handler_code == SI_TKILL && handler_pid == getpid());
    CHECK(handler_mask_result == 0 && (handler_mask & mask) &&
          (handler_mask & (UINT64_C(1) << (SIGUSR2 - 1))));
    CHECK(handler_stack_pointer >= (uintptr_t)memory && handler_stack_pointer < (uintptr_t)memory + active.size);
    CHECK(handler_stack_result == 0 && handler_stack_flags == SS_ONSTACK &&
          handler_stack_address == (uintptr_t)memory && handler_stack_size == active.size);
    CHECK(interrupted_pc != 0 && interrupted_sp != 0 &&
          (interrupted_sp < (uintptr_t)memory || interrupted_sp >= (uintptr_t)memory + active.size));
    CHECK(call6(SYS_rt_sigprocmask, 0, (uintptr_t)&mask, 0, 8, 0, 0) == 0);
    CHECK(alternate(&original_stack, NULL) == 0);
    free(memory);

    const struct timespec zero = {0, 0}, invalid = {0, 1000000000};
    const struct action64 wait_action = {0};
    CHECK(action(SIGUSR1, &wait_action, NULL) == 0); // Never inherit SIG_IGN during sigwait.
    siginfo_t info;
    CHECK(wait_for(&mask, &info, &zero) == -EAGAIN);
    CHECK(wait_for(&mask, &info, &invalid) == -EINVAL);
    CHECK(wait_for((void *)(uintptr_t)1, &info, &zero) == -EFAULT);
    CHECK(call6(SYS_rt_sigtimedwait, (uintptr_t)&mask, (uintptr_t)&info, (uintptr_t)&zero, 0, 0, 0) == -EINVAL);
    pid_t pid = getpid(), tid = (pid_t)syscall(SYS_gettid);
    CHECK(call6(SYS_tgkill, pid, tid, 0, 0, 0, 0) == 0);
    CHECK(call6(SYS_tgkill, pid, tid, SIGUSR1, 0, 0, 0) == 0);
    CHECK(call6(SYS_tgkill, pid, tid, SIGUSR1, 0, 0, 0) == 0);
    CHECK(wait_for(&mask, &info, &zero) == SIGUSR1);
    CHECK(info.si_signo == SIGUSR1 && info.si_pid == pid && info.si_uid == getuid() && info.si_code == SI_TKILL);
    CHECK(wait_for(&mask, &info, &zero) == -EAGAIN); // Standard signals coalesce.
    pthread_t worker;
    CHECK(pthread_create(&worker, NULL, waiter, NULL) == 0);
    int target;
    const struct timespec tick = {0, 1000000};
    for (unsigned n = 0; n < 5000 && !(target = atomic_load_explicit(&worker_tid, memory_order_acquire)); ++n)
        nanosleep(&tick, NULL);
    target = atomic_load_explicit(&worker_tid, memory_order_acquire);
    CHECK(target > 0);
    CHECK(drop_worker || call6(SYS_tgkill, pid, target, SIGUSR1, 0, 0, 0) == 0);
    CHECK(pthread_join(worker, NULL) == 0);
    CHECK(worker_result == SIGUSR1 && worker_info.si_pid == pid && worker_info.si_code == SI_TKILL);
    CHECK(action(SIGUSR1, &saved, NULL) == 0);
    CHECK(call6(SYS_rt_sigprocmask, 2, (uintptr_t)&previous, 0, 8, 0, 0) == 0);
    printf("{\"checks\":%u,\"kernel_min_altstack\":%" PRIu64
           ",\"accepted_action_flags\":%" PRIu64
           ",\"handler_delivery\":true,\"alternate_stack\":true,\"standard_coalescing\":true,\"thread_wakeup\":true}\n",
           checks, upper, accepted_action_flags);
    return 0;
}
