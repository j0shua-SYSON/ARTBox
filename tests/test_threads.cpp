#include "artbox/threads.h"
#include "artbox/native_atomic.h"
#include "artbox/native_system.h"
#include "artbox/native_thread.h"
#include "artbox/native_vm.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)
static std::atomic<bool> after_entry{false}, allow_native_return{false};
static std::atomic<unsigned> starts{0};
static bool reject_start;
static int fake_start(void *, size_t, void (*entry)(void *), void *arg, void **handle) {
    ++starts;
    if (reject_start) return -11;
    *handle = new std::thread([=] {
        entry(arg);
        // Simulate native TLS destruction after the core callback returns.
        after_entry = true;
        while (!allow_native_return) std::this_thread::yield();
    });
    return 0;
}
static int fake_join(void *handle) {
    auto *worker = static_cast<std::thread*>(handle);
    worker->join(); delete worker;
    return 0;
}
static void await(const std::atomic<bool> &value) {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!value) { CHECK(std::chrono::steady_clock::now() < end); std::this_thread::yield(); }
}
struct Context {
    artbox_atomic_u32_ops atomic;
    std::atomic<unsigned> ran{0};
    bool detached = false, native_stack = false;
};
static void run(void *context, artbox_kernel_thread *kernel, const artbox_thread_start *start, artbox_thread_finish *finish) {
    auto *c = static_cast<Context*>(context);
    uint32_t tid = 0;
    CHECK(kernel->pid == 100 && kernel->tid > 100);
    CHECK(artbox_vm_load_u32(kernel->vm, start->parent_tid, &c->atomic, &tid) == 0 && tid == static_cast<uint32_t>(kernel->tid));
    CHECK(kernel->clear_tid_address == start->child_tid && start->tls == 123 && start->entry == 456 && start->argument == 789);
    if (c->native_stack) {
        uintptr_t local = reinterpret_cast<uintptr_t>(&tid);
        CHECK(local >= start->stack_base && local - start->stack_base < start->stack_size);
    }
    if (c->detached) {
        CHECK(artbox_kernel_call(kernel, 96, 0, 0, 0, 0, 0, 0) == kernel->tid);
        finish->unmap_address = start->stack_base;
        finish->unmap_size = start->stack_size;
    }
    ++c->ran;
}
int main() {
    artbox_vm_ops memory = artbox_native_vm();
    artbox_system_ops system = artbox_native_system();
    Context context; context.atomic = artbox_native_atomic_u32();
    artbox_vm *vm = artbox_vm_create(&memory, 16 * 1024 * 1024, 16);
    CHECK(vm);
    artbox_futex *futex = artbox_futex_create(vm, &context.atomic, &system, 8);
    CHECK(futex);
    uint64_t word = static_cast<uint64_t>(artbox_vm_mmap(vm, 0, memory.page_size, 3, 0x22, -1, 0));
    uint64_t stack = static_cast<uint64_t>(artbox_vm_mmap(vm, 0, memory.page_size, 3, 0x22, -1, 0));
    CHECK(word < INT64_MAX && stack < INT64_MAX);
    artbox_thread_ops fake{fake_start, fake_join};
    artbox_threads *threads = artbox_threads_create(vm, futex, &context.atomic, &system, &fake, 100, 101, 1, run, &context);
    CHECK(threads);
    artbox_thread_start s{ARTBOX_PTHREAD_CLONE_FLAGS, stack, memory.page_size, 123, word, word, 456, 789};
    auto invalid = s; invalid.flags = 0;
    CHECK(artbox_threads_start(threads, &invalid) == -38);
    invalid = s; invalid.parent_tid = 0;
    CHECK(artbox_threads_start(threads, &invalid) == -14 && starts == 0);
    invalid = s; invalid.stack_base += 1;
    CHECK(artbox_threads_start(threads, &invalid) == -22 && starts == 0);
    reject_start = true;
    CHECK(artbox_vm_store_u32(vm, word, &context.atomic, 99) == 0);
    CHECK(artbox_threads_start(threads, &s) == -11 && artbox_threads_active(threads) == 0);
    uint32_t tid;
    CHECK(artbox_vm_load_u32(vm, word, &context.atomic, &tid) == 0 && tid == 99);
    reject_start = false;
    int64_t first = artbox_threads_start(threads, &s);
    CHECK(first > 100); await(after_entry);
    CHECK(artbox_threads_destroy(threads) == -16 && artbox_threads_drain(threads, 1) == -110);
    CHECK(artbox_threads_start(threads, &s) == -11); // Reaping workers count against the limit.
    CHECK(artbox_vm_load_u32(vm, word, &context.atomic, &tid) == 0 && tid == first);
    std::atomic<int> waited{99};
    std::thread joiner([&] { waited = static_cast<int>(artbox_futex_call(futex, word, 0, static_cast<uint64_t>(first), 0, 0, 0)); });
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!artbox_futex_waiters(futex, word, 0)) { CHECK(std::chrono::steady_clock::now() < end); std::this_thread::yield(); }
    CHECK(waited == 99 && artbox_threads_reaped(threads) == 0);
    allow_native_return = true;
    CHECK(artbox_threads_drain(threads, 2000) == 0); joiner.join(); CHECK(waited == 0);
    CHECK(artbox_vm_load_u32(vm, word, &context.atomic, &tid) == 0 && tid == 0);
    int64_t previous = first;
    for (unsigned i = 0; i < 128; ++i) {
        int64_t current = artbox_threads_start(threads, &s);
        CHECK(current > previous && artbox_threads_drain(threads, 2000) == 0);
        previous = current;
    }
    context.detached = true; after_entry = false; allow_native_return = false;
    CHECK(artbox_threads_start(threads, &s) > previous); await(after_entry);
    CHECK(artbox_vm_access(vm, stack, memory.page_size, 3));
    CHECK(artbox_threads_drain(threads, 1) == -110);
    allow_native_return = true;
    CHECK(artbox_threads_drain(threads, 2000) == 0 && !artbox_vm_access(vm, stack, 1, 1));
    CHECK(artbox_threads_reaped(threads) == 130 && context.ran == 130);
    CHECK(artbox_threads_destroy(threads) == 0);

    artbox_thread_ops native = artbox_native_threads();
#if defined(_WIN32)
    void *handle = nullptr;
    CHECK(native.start(nullptr, 0, nullptr, nullptr, &handle) == -95);
#else
    // Exercise the actual public pthread supplied-stack API, including reclaim.
    context.native_stack = true;
    threads = artbox_threads_create(vm, futex, &context.atomic, &system, &native, 100, 1000, 8, run, &context);
    CHECK(threads);
    for (unsigned i = 0; i < 8; ++i) {
        int64_t allocation = artbox_vm_mmap(vm, 0, 1024 * 1024 + 2 * memory.page_size, 0, 0x22, -1, 0);
        CHECK(allocation > 0);
        s.stack_base = static_cast<uint64_t>(allocation) + memory.page_size; s.stack_size = 1024 * 1024;
        CHECK(artbox_vm_mprotect(vm, s.stack_base, s.stack_size, 3) == 0);
        CHECK(artbox_threads_start(threads, &s) >= 1000 && artbox_threads_drain(threads, 2000) == 0);
        CHECK(!artbox_vm_access(vm, s.stack_base, 1, 1));
        CHECK(artbox_vm_munmap(vm, static_cast<uint64_t>(allocation), 1024 * 1024 + 2 * memory.page_size) == 0);
    }
    CHECK(artbox_threads_reaped(threads) == 8 && artbox_threads_destroy(threads) == 0);
#endif
    CHECK(artbox_futex_destroy(futex) == 0 && artbox_vm_destroy(vm) == 0);
    std::puts("Thread lifecycle: creation failures, 130 reaps, clear-TID wake and deferred stack release passed");
    return 0;
}
