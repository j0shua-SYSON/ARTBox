// Original portable thread lifecycle, MIT. Bionic owns guest pthread state.
#include "artbox/threads.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

struct Worker;
struct artbox_threads {
    artbox_vm *vm;
    artbox_futex *futex;
    artbox_atomic_u32_ops atomic;
    artbox_system_ops system;
    artbox_thread_ops native;
    int32_t pid;
    uint64_t next_tid, reaped = 0;
    size_t limit;
    artbox_thread_run run;
    void *context;
    std::mutex lock;
    std::condition_variable changed;
    std::vector<Worker*> active;
    std::thread reaper;
    bool stopping = false;
    int error = 0;
};
struct Worker {
    artbox_threads *owner;
    artbox_thread_start start;
    artbox_kernel_thread kernel;
    artbox_thread_finish finish{};
    void *handle = nullptr;
    bool done = false;
};
static void worker_entry(void *argument) {
    Worker *w = static_cast<Worker*>(argument);
    artbox_threads *t = w->owner;
    { // Creator holds this lock until the parent TID is published.
        std::lock_guard<std::mutex> guard(t->lock);
    }
    t->run(t->context, &w->kernel, &w->start, &w->finish);
    {
        std::lock_guard<std::mutex> guard(t->lock);
        w->done = true;
        t->changed.notify_all();
    }
    // The reaper still joins the native worker before touching guest memory.
}
static void reap(artbox_threads *t) {
    std::unique_lock<std::mutex> guard(t->lock);
    for (;;) {
        Worker *w = nullptr;
        for (Worker *candidate : t->active) if (candidate->done) { w = candidate; break; }
        if (!w) {
            if (t->stopping) return;
            t->changed.wait(guard);
            continue;
        }
        guard.unlock();
        int error = t->native.join(w->handle);
        if (error) {
            // Its lifetime is now unknown. Retain both record and mapping;
            // never clear a TID or release a potentially live native stack.
            guard.lock(); t->error = error < 0 ? error : -5; t->changed.notify_all();
            return;
        }
        {
            error = w->finish.error;
            if (!error && w->finish.unmap_size) {
                // Detached Bionic exit must have disabled CHILD_CLEARTID.
                error = w->kernel.clear_tid_address ? -22 :
                    artbox_vm_munmap(t->vm, w->finish.unmap_address, w->finish.unmap_size);
            } else if (!error) {
                error = artbox_futex_clear_tid(t->futex, w->kernel.clear_tid_address);
                if (error == -14) error = 0; // Linux exit tolerates an inaccessible clear address.
            }
        }
        guard.lock();
        if (error && !t->error) t->error = error < 0 ? error : -5;
        t->active.erase(std::find(t->active.begin(), t->active.end(), w));
        ++t->reaped;
        delete w;
        t->changed.notify_all();
    }
}
extern "C" artbox_threads *artbox_threads_create(artbox_vm *vm, artbox_futex *futex,
    const artbox_atomic_u32_ops *atomic, const artbox_system_ops *system,
    const artbox_thread_ops *native, int32_t pid, int32_t first_tid, size_t limit,
    artbox_thread_run run, void *context) {
    if (!vm || !futex || !atomic || !atomic->load_acquire || !atomic->store_release || !system ||
        !system->clock || !system->random || !native || !native->start || !native->join ||
        pid <= 0 || first_tid <= pid || !limit || limit > 65536 || !run) return nullptr;
    artbox_threads *t = new (std::nothrow) artbox_threads;
    if (!t) return nullptr;
    t->vm = vm; t->futex = futex; t->atomic = *atomic; t->system = *system; t->native = *native;
    t->pid = pid; t->next_tid = static_cast<uint64_t>(first_tid); t->limit = limit; t->run = run; t->context = context;
    try { t->active.reserve(limit); t->reaper = std::thread(reap, t); }
    catch (const std::exception&) { delete t; return nullptr; }
    return t;
}
static int start_native(void *context) {
    Worker *w = static_cast<Worker*>(context);
    return w->owner->native.start(reinterpret_cast<void*>(w->start.stack_base),
        static_cast<size_t>(w->start.stack_size), worker_entry, w, &w->handle);
}
extern "C" int64_t artbox_threads_start(artbox_threads *t, const artbox_thread_start *start) {
    if (!t || !start) return -22;
    const artbox_thread_start s = *start;
    if (s.flags != ARTBOX_PTHREAD_CLONE_FLAGS) return -38;
    const uint64_t page = artbox_vm_page_size(t->vm);
    if (!s.stack_base || !s.stack_size || s.stack_base % page || s.stack_size % page ||
        s.stack_size > SIZE_MAX || !s.tls || !s.entry || (s.parent_tid & 3)) return -22;
    if (!artbox_vm_access(t->vm, s.stack_base, s.stack_size, 3) ||
        !artbox_vm_access(t->vm, s.parent_tid, 4, 2)) return -14;
    try {
        std::lock_guard<std::mutex> guard(t->lock);
        if (t->stopping || t->error) return -5;
        if (t->active.size() == t->limit || t->next_tid > INT32_MAX) return -11;
        Worker *w = new (std::nothrow) Worker;
        if (!w) return -12;
        w->owner = t; w->start = s;
        artbox_kernel_thread_init(&w->kernel, t->vm, &t->system, t->pid, static_cast<int32_t>(t->next_tid++));
        w->kernel.clear_tid_address = s.child_tid;
        int error = artbox_vm_prepare_store_u32(t->vm, s.parent_tid, &t->atomic,
            static_cast<uint32_t>(w->kernel.tid), start_native, w);
        if (error) { delete w; return error; }
        t->active.push_back(w); // Reserved capacity; the child is blocked on this lock.
        return w->kernel.tid;
    } catch (const std::exception&) { return -12; }
}
extern "C" int artbox_threads_drain(artbox_threads *t, uint32_t timeout_ms) {
    if (!t) return -22;
    std::unique_lock<std::mutex> guard(t->lock);
    if (!t->changed.wait_for(guard, std::chrono::milliseconds(timeout_ms),
                            [&] { return t->active.empty() || t->error; })) return -110;
    return t->error;
}
extern "C" int artbox_threads_destroy(artbox_threads *t) {
    if (!t) return -22;
    {
        std::lock_guard<std::mutex> guard(t->lock);
        if (!t->active.empty()) return -16;
        t->stopping = true; t->changed.notify_all();
    }
    t->reaper.join();
    int error = t->error;
    delete t;
    return error;
}
extern "C" size_t artbox_threads_active(artbox_threads *t) {
    if (!t) return 0;
    std::lock_guard<std::mutex> guard(t->lock); return t->active.size();
}
extern "C" uint64_t artbox_threads_reaped(artbox_threads *t) {
    if (!t) return 0;
    std::lock_guard<std::mutex> guard(t->lock); return t->reaped;
}
