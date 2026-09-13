#include "artbox/guest.h"
#include "artbox/native_memory.h"
#include <stdio.h>
#include <string.h>
#if defined(__linux__)
#include <errno.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; \
} } while (0)
typedef struct sink { unsigned calls; char bytes[64]; size_t count; } sink;
static int64_t output(void *opaque, int fd, const void *bytes, size_t count) {
    sink *s = opaque;
    if (fd != 1 && fd != 2) return -9;
    if (count > sizeof(s->bytes) - s->count) return -5;
    memcpy(s->bytes + s->count, bytes, count); s->count += count; ++s->calls;
    return (int64_t)count;
}
static void exit_bridge(const void *entry, artbox_guest *guest,
                        artbox_dispatch_fn dispatch, void *stack) {
    (void)entry; (void)stack;
    (void)dispatch(guest, 93, 0x123, 0, 0, 0, 0, 0);
    /* A successful guest exit must never reach here. */
    (void)dispatch(guest, 64, 99, 0, 0, 0, 0, 0);
}
static void returning_bridge(const void *entry, artbox_guest *guest,
                             artbox_dispatch_fn dispatch, void *stack) {
    (void)entry; (void)guest; (void)dispatch; (void)stack;
}

int main(void) {
    static const char message[] = "hello";
    artbox_memory_ops memory = artbox_native_memory();
    sink captured = {0};
    artbox_guest *guest = artbox_guest_create(&memory, output, &captured, message, sizeof(message));
    int64_t address;
    size_t page = memory.page_size;
    CHECK(guest != NULL);
    CHECK(artbox_guest_call(guest, 9999, 0, 0, 0, 0, 0, 0) == -38);
    CHECK(artbox_guest_call(guest, 64, 99, 0, 0, 0, 0, 0) == -9);
    CHECK(artbox_guest_call(guest, 64, 1, 0, 0, 0, 0, 0) == 0);
    CHECK(captured.calls == 0);
    CHECK(artbox_guest_call(guest, 64, 1, 1, 5, 0, 0, 0) == -14);
    CHECK(artbox_guest_call(guest, 64, 1, (uintptr_t)message, sizeof(message), 0, 0, 0) == sizeof(message));
    CHECK(captured.count == sizeof(message) && memcmp(captured.bytes, message, sizeof(message)) == 0);
    CHECK(artbox_guest_call(guest, 222, 0, 0, 3, 0x22, UINT64_MAX, 0) == -22);
    CHECK(artbox_guest_call(guest, 222, 0, page, 7, 0x22, UINT64_MAX, 0) == -1);
    CHECK(artbox_guest_call(guest, 222, 0, page, 3, 0x32, UINT64_MAX, 0) == -95);
    address = artbox_guest_call(guest, 222, 0, page, 3, 0x22, UINT64_MAX, 0);
    CHECK(address > 0);
    memcpy((void *)(uintptr_t)address, message, sizeof(message));
    CHECK(artbox_guest_call(guest, 226, (uint64_t)address + 1, page, 1, 0, 0, 0) == -22);
    CHECK(artbox_guest_call(guest, 226, (uint64_t)address, page, 5, 0, 0, 0) == -1);
    CHECK(artbox_guest_call(guest, 226, (uint64_t)address, page, 1, 0, 0, 0) == 0);
    CHECK(memcmp((void *)(uintptr_t)address, message, sizeof(message)) == 0);
    CHECK(artbox_guest_call(guest, 226, (uint64_t)address, page, 0, 0, 0, 0) == 0);
    CHECK(artbox_guest_call(guest, 64, 1, (uint64_t)address, 1, 0, 0, 0) == -14);
    CHECK(artbox_guest_call(guest, 226, (uint64_t)address, page, 3, 0, 0, 0) == 0);
    CHECK(artbox_guest_call(guest, 215, (uint64_t)address + 1, page, 0, 0, 0, 0) == -22);
    CHECK(artbox_guest_call(guest, 215, (uint64_t)address, page, 0, 0, 0, 0) == 0);
    CHECK(artbox_guest_call(guest, 64, 1, (uint64_t)address, 1, 0, 0, 0) == -14);
    CHECK(artbox_guest_execute(guest, message, exit_bridge) == 0x23);
    CHECK(artbox_guest_syscall_count(guest, 93) == 1);
    CHECK(artbox_guest_execute(guest, message, returning_bridge) == ARTBOX_GUEST_RETURNED);
#if defined(__linux__)
    /* Real Linux behavior, separately from the deliberate no-exec/ownership
     * policy restrictions above. Do not call policy divergences Linux parity. */
    errno = 0;
    CHECK(write(-1, message, 0) == -1 && errno == 9);
    errno = 0;
    CHECK(mmap(NULL, 0, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED && errno == 22);
    {
        void *oracle = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(oracle != MAP_FAILED);
        memcpy(oracle, message, sizeof(message));
        CHECK(mprotect(oracle, page, PROT_READ) == 0);
        CHECK(memcmp(oracle, message, sizeof(message)) == 0);
        errno = 0;
        CHECK(mprotect((char *)oracle + 1, page, PROT_READ) == -1 && errno == 22);
        errno = 0;
        CHECK(munmap((char *)oracle + 1, page) == -1 && errno == 22);
        CHECK(munmap(oracle, page) == 0);
    }
    {
        int status;
        pid_t child = fork();
        CHECK(child >= 0);
        if (child == 0) _exit(0x123);
        CHECK(waitpid(child, &status, 0) == child);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0x23);
    }
#endif
    CHECK(artbox_guest_destroy(guest) == 0);
    printf("five-syscall portable contract: %u checks PASS\n", checks);
    return 0;
}
