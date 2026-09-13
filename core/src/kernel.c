#include "artbox/kernel.h"

int artbox_kernel_thread_init(artbox_kernel_thread *thread, artbox_vm *vm,
                              const artbox_system_ops *system, int32_t pid, int32_t tid) {
    if (!thread || !vm || !system || !system->clock || !system->random || pid <= 0 || tid <= 0) return -22;
    *thread = (artbox_kernel_thread){vm, *system, pid, tid, 0};
    return 0;
}

static void put64(unsigned char *bytes, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) bytes[i] = (unsigned char)(value >> (i * 8));
}

int64_t artbox_kernel_call(void *context, uint64_t number, uint64_t a0, uint64_t a1,
                          uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    artbox_kernel_thread *thread = context;
    if (!thread) return -22;
    switch (number) {
        case 172: return thread->pid;
        case 178: return thread->tid;
        case 96:
            /* Linux stores this pointer without dereferencing or validating it.
             * The thread-exit path must later clear/wake it when accessible. */
            thread->clear_tid_address = a0;
            return thread->tid;
        case 113: {
            unsigned id = (uint32_t)a0;
            // A precise clock is a valid, potentially slower implementation of
            // Linux's coarse clock. Scudo uses MONOTONIC_COARSE during startup.
            if (id == 5 || id == 6) id -= 5;
            if (id > 1) return -22;
            artbox_timespec value;
            int result = thread->system.clock(id, &value);
            if (result) return result < 0 ? result : -5;
            if (value.nanoseconds < 0 || value.nanoseconds >= 1000000000 || (id == 1 && value.seconds < 0)) return -5;
            unsigned char bytes[16];
            put64(bytes, (uint64_t)value.seconds);
            put64(bytes + 8, (uint64_t)value.nanoseconds);
            return artbox_vm_write(thread->vm, a1, bytes, sizeof(bytes));
        }
        case 278: {
            unsigned flags = (uint32_t)a2;
            if ((flags & ~7u) || (flags & 6u) == 6u) return -22;
            /* All host providers use an initialized system CSPRNG. Insecure
             * requests receive secure bytes too; no user-space PRNG fallback. */
            uint64_t max_count = (uint64_t)INT32_MAX & ~((uint64_t)artbox_vm_page_size(thread->vm) - 1);
            uint64_t count = a1 > max_count ? max_count : a1;
            uint64_t done = 0;
            while (done < count) {
                unsigned char bytes[256];
                size_t chunk = count - done > sizeof(bytes) ? sizeof(bytes) : (size_t)(count - done);
                int error;
                if (done > UINT64_MAX - a0) return done ? (int64_t)done : -14;
                error = thread->system.random(bytes, chunk);
                if (!error) error = artbox_vm_write(thread->vm, a0 + done, bytes, chunk);
                if (error) return done ? (int64_t)done : error < 0 ? error : -5;
                done += chunk;
            }
            return (int64_t)done;
        }
        default: return artbox_vm_syscall(thread->vm, number, a0, a1, a2, a3, a4, a5);
    }
}
