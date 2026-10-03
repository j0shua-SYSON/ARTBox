/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include <stdint.h>

/* The real AOSP assembly reads this field at offset 20. No host TLS is used. */
struct thread_state { uint32_t guard[5]; uint32_t cached; uint64_t tail; };
_Static_assert(offsetof(struct thread_state, cached) == 20, "Bionic cached PID offset");
static struct thread_state thread;
__attribute__((visibility("hidden"))) struct thread_state artbox_vfork_poison_thread;
__attribute__((visibility("hidden"))) unsigned char artbox_vfork___libc_memtag_stack;
static void *tls[8];
static int guest_errno, tls_calls, syscall_calls;
static int64_t reply;
static uint64_t captured[7];
static uint32_t cached_during_call;
extern int artbox_vfork_vfork(void);

__attribute__((visibility("hidden"))) void **artbox_vfork_artbox_bionic_get_tls(void) {
    ++tls_calls;
    return tls;
}
__attribute__((visibility("hidden"))) int *artbox_vfork___errno(void) { return &guest_errno; }

__attribute__((visibility("hidden"))) int64_t artbox_vfork_capture(
        uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
        uint64_t a3, uint64_t a4, uint64_t a5) {
    ++syscall_calls;
    captured[0] = n; captured[1] = a0; captured[2] = a1; captured[3] = a2;
    captured[4] = a3; captured[5] = a4; captured[6] = a5;
    cached_during_call = thread.cached;
    return reply;
}

int artbox_vfork_check(void) {
    static const int64_t replies[] = {-1, -38, -4095, -4096, 0, 123, INT32_MAX};
    static const uint32_t states[] = {UINT32_C(0x12345678), UINT32_C(0x92345678)};
    int cases = 0;
    for (unsigned mode = 0; mode != 2; ++mode) {
        for (unsigned state = 0; state != 2; ++state) {
            for (unsigned r = 0; r != sizeof(replies) / sizeof(replies[0]); ++r) {
                for (unsigned i = 0; i != 5; ++i) {
                    thread.guard[i] = UINT32_C(0xcab00000) + i;
                    artbox_vfork_poison_thread.guard[i] = UINT32_C(0xcab00000) + i;
                }
                thread.tail = artbox_vfork_poison_thread.tail = UINT64_C(0xface012389abcdef);
                thread.cached = states[state];
                artbox_vfork_poison_thread.cached = UINT32_C(0x87654321);
                tls[1] = &thread;
                artbox_vfork___libc_memtag_stack = (unsigned char)mode;
                guest_errno = 77; tls_calls = syscall_calls = 0;
                reply = replies[r];
                int result = artbox_vfork_vfork();
                int error = reply < 0 && reply >= -4095;
                if (result != (error ? -1 : (int)reply) || guest_errno != (error ? -reply : 77))
                    return -100 - cases;
                if (tls_calls != 1 || syscall_calls != 1 || captured[0] != 220 ||
                    captured[1] != (mode ? UINT64_C(17) : UINT64_C(0x4111)) ||
                    cached_during_call != UINT32_C(0x80000000)) return -200 - cases;
                for (unsigned i = 2; i != 7; ++i) if (captured[i]) return -300 - cases;
                /* The mutation control must fail here on the first rejected call. */
                if (thread.cached != (reply == 0 ? UINT32_C(0x80000000) : states[state]))
                    return -1000 - cases;
                if (artbox_vfork_poison_thread.cached != UINT32_C(0x87654321)) return -400 - cases;
                for (unsigned i = 0; i != 5; ++i)
                    if (thread.guard[i] != UINT32_C(0xcab00000) + i ||
                        artbox_vfork_poison_thread.guard[i] != UINT32_C(0xcab00000) + i)
                        return -500 - cases;
                if (thread.tail != UINT64_C(0xface012389abcdef) ||
                    artbox_vfork_poison_thread.tail != UINT64_C(0xface012389abcdef)) return -600 - cases;
                ++cases;
            }
        }
    }
    return cases;
}
