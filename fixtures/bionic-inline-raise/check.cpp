// Exercise the actual pinned header on native Linux. The endpoint below is a
// Linux test backend; it is not ARTBox's Darwin signal implementation.
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <csignal>
#include <thread>
#include <vector>
#include <sys/syscall.h>
#include <unistd.h>

// glibc's macro includes the inline keyword; Bionic supplies that separately.
#undef __always_inline
#define __always_inline __attribute__((__always_inline__))
#include "bionic_inline_raise.h"

static std::atomic<unsigned> failures{0}, calls{0}, deliveries{0};
static constexpr unsigned threads = 8, iterations = 64;

extern "C" int64_t artbox_bionic_syscall(uint64_t number, uint64_t a0, uint64_t a1,
                                        uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    static_assert(sizeof(long) == 8, "This test requires native 64-bit Linux");
    const int saved = errno;
    ++calls;
    if (number != __NR_rt_tgsigqueueinfo || a0 != static_cast<uint64_t>(getpid()) ||
        a1 != static_cast<uint64_t>(syscall(__NR_gettid)) || a4 || a5 || !a3) {
        ++failures;
        errno = saved;
        return -EINVAL;
    }
    const auto* info = reinterpret_cast<const siginfo_t*>(a3);
    if (info->si_code != SI_QUEUE || info->si_pid != getpid() || info->si_uid != getuid()) ++failures;
    const long result = syscall(static_cast<long>(number), static_cast<long>(a0), static_cast<long>(a1),
                                static_cast<long>(a2), static_cast<long>(a3), static_cast<long>(a4), static_cast<long>(a5));
    const int error = errno;
    errno = saved;
    return result == -1 ? -error : result;
}

static void receive(int signal, void* payload) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, signal);
    siginfo_t info{};
    const timespec limit{2, 0};
    if (sigtimedwait(&set, &info, &limit) != signal || info.si_signo != signal ||
        info.si_code != SI_QUEUE || info.si_pid != getpid() || info.si_uid != getuid() ||
        info.si_value.sival_ptr != payload) {
        ++failures;
    } else {
        ++deliveries;
    }
}

int main() {
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGUSR1);
    sigaddset(&blocked, SIGUSR2);
    if (pthread_sigmask(SIG_BLOCK, &blocked, nullptr)) return 1;
    std::vector<std::thread> workers;
    for (unsigned t = 0; t < threads; ++t) workers.emplace_back([] {
        int payload = 42;
        for (unsigned i = 0; i < iterations; ++i) {
            errno = EDOM;
            inline_raise(SIGUSR1, &payload);
            if (errno != EDOM) ++failures;
            receive(SIGUSR1, &payload);
            inline_raise(SIGUSR2);
            receive(SIGUSR2, nullptr);
        }
        errno = ERANGE;
        inline_raise(-1, &payload);
        if (errno != ERANGE) ++failures;
    });
    for (auto& worker : workers) worker.join();
    const unsigned expected = threads * iterations * 2;
#ifdef ARTBOX_NATIVE_HOST
    if (calls != expected + threads) ++failures;
#else
    if (calls != 0) ++failures;
#endif
    if (deliveries != expected) ++failures;
    std::printf("{\"deliveries\":%u,\"threads\":%u,\"endpoint_calls\":%u,\"failures\":%u}\n",
                deliveries.load(), threads, calls.load(), failures.load());
    return failures ? 1 : 0;
}
