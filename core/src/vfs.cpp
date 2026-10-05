// Original rooted guest descriptor table, MIT.
#include "artbox/vfs.h"
#include "artbox/binder_device.h"
#include "artbox/signals.h"
#include <algorithm>
#include <cstring>
#include <exception>
#include <mutex>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>
struct BinderOpen {
    artbox_binder_device *device;
    artbox_vm *vm;
    uint64_t token = 0;
    BinderOpen(artbox_binder_device *d, artbox_vm *v) : device(d), vm(v) {}
    ~BinderOpen() { if (token) (void)artbox_binder_device_close(device, token); }
};
struct PollWaiter { void *owner = nullptr; int error = 0; };
struct PollHub {
    std::mutex lock;
    artbox_wake_ops wake{};
    size_t interests = 0, limit = 0;
    uint64_t subscription = 0;
    std::vector<PollWaiter *> waiters;
};
static void poll_notify(void *context) {
    auto *hub = static_cast<PollHub *>(context);
    std::lock_guard<std::mutex> guard(hub->lock);
    for (auto *waiter : hub->waiters) if (waiter->owner) {
        int error = hub->wake.signal(waiter->owner);
        if (error && !waiter->error) waiter->error = error < 0 ? error : -5;
    }
}
struct EpollInterest { int32_t fd; uint32_t events; uint64_t token, data; };
struct EpollOpen {
    artbox_vm *vm;
    size_t cursor = 0;
    std::vector<EpollInterest> interests;
    explicit EpollOpen(artbox_vm *v) : vm(v) {}
};
struct descriptor {
    unsigned kind = 0, flags = 0;
    bool directory = false;
    void *handle = nullptr;
    std::string path;
    uint64_t position = 0;
    std::shared_ptr<BinderOpen> binder;
    std::shared_ptr<EpollOpen> epoll;
};
struct artbox_vfs {
    std::mutex lock;
    artbox_file_ops files{};
    std::vector<descriptor> descriptors;
    std::vector<unsigned char> commandline;
    artbox_binder_device *binder = nullptr;
    uint32_t binder_uid = 0;
    std::unique_ptr<PollHub> poll;
};
extern "C" artbox_vfs *artbox_vfs_create(const artbox_file_ops *files, size_t limit) {
    if (!limit || limit > 4096 || (files && (!files->open || !files->close || !files->read || !files->write ||
        !files->seek || !files->stat || !files->stat_at))) return nullptr;
    artbox_vfs *fs = new (std::nothrow) artbox_vfs;
    if (!fs) return nullptr;
    if (files) fs->files = *files;
    try { fs->descriptors.resize(limit); }
    catch (const std::exception&) { delete fs; return nullptr; }
    return fs;
}
extern "C" int artbox_vfs_set_binder(artbox_vfs *fs, artbox_binder_device *device, uint32_t uid) {
    if (!fs || !device) return -22;
    std::lock_guard<std::mutex> guard(fs->lock);
    if (fs->binder) return -114;
    if (fs->poll) {
        int error = artbox_binder_device_observe(device, poll_notify, fs->poll.get(), &fs->poll->subscription);
        if (error) return error;
    }
    fs->binder = device; fs->binder_uid = uid;
    return 0;
}
extern "C" int artbox_vfs_set_commandline(artbox_vfs *fs, const void *bytes, size_t length) {
    if (!fs || !bytes || !length) return -22;
    if (length > 65536) return -7;
    const auto *data = static_cast<const unsigned char*>(bytes);
    if (data[length - 1]) return -22;
    try {
        std::lock_guard<std::mutex> guard(fs->lock);
        if (!fs->commandline.empty()) return -114;
        fs->commandline.assign(data, data + length);
        return 0;
    } catch (const std::exception&) { return -12; }
}
extern "C" int artbox_vfs_set_epoll(artbox_vfs *fs, const artbox_wake_ops *wake,
    size_t interest_limit, size_t waiter_limit) {
    if (!fs || !wake || !wake->create || !wake->signal || !wake->wait || !wake->close ||
        !interest_limit || interest_limit > 4096 || !waiter_limit || waiter_limit > 1024) return -22;
    try {
        std::lock_guard<std::mutex> guard(fs->lock);
        if (fs->poll) return -114;
        std::unique_ptr<PollHub> hub(new PollHub);
        hub->wake = *wake; hub->interests = interest_limit; hub->limit = waiter_limit;
        hub->waiters.reserve(waiter_limit);
        if (fs->binder) {
            int error = artbox_binder_device_observe(fs->binder, poll_notify, hub.get(), &hub->subscription);
            if (error) return error;
        }
        fs->poll = std::move(hub);
        return 0;
    } catch (const std::exception &) { return -12; }
}
static int64_t epoll_dispatch(artbox_vfs *, artbox_kernel_thread *, uint64_t,
    uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern "C" int64_t artbox_vfs_syscall(artbox_vfs *fs, artbox_kernel_thread *thread, uint64_t number,
    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    if (number >= 20 && number <= 22) return epoll_dispatch(fs, thread, number, a0, a1, a2, a3, a4, a5);
    return artbox_vfs_call(fs, thread, number, a0, a1, a2, a3);
}
extern "C" int artbox_vfs_destroy(artbox_vfs *fs) {
    if (!fs) return -22;
    if (fs->poll && fs->poll->subscription) {
        int error = artbox_binder_device_unobserve(fs->binder, fs->poll->subscription);
        if (error) return error; // Preserve the callback owner if removal failed.
    }
    int result = 0;
    for (descriptor &d : fs->descriptors) if (d.kind == 5) {
        int error = fs->files.close(d.handle);
        if (!result) result = error;
    }
    delete fs;
    return result;
}
static descriptor *get(artbox_vfs *fs, int32_t fd) {
    if (fd < 3 || static_cast<size_t>(fd - 3) >= fs->descriptors.size()) return nullptr;
    descriptor *d = &fs->descriptors[static_cast<size_t>(fd - 3)];
    return d->kind ? d : nullptr;
}
extern "C" int artbox_vfs_events(artbox_vfs *fs, artbox_kernel_thread *thread, int fd) {
    if (!fs || !thread || !thread->vm) return -22;
    std::shared_ptr<BinderOpen> binder;
    {
        std::lock_guard<std::mutex> guard(fs->lock);
        descriptor *d = get(fs, fd);
        if (!d) return -9;
        if (!d->binder) return -95;
        binder = d->binder;
    }
    if (binder->vm != thread->vm) return -95;
    return artbox_binder_device_events(binder->device, binder->token, thread->tid);
}
struct PollRegistration {
    PollHub *hub;
    PollWaiter waiter;
    bool registered = false;
    explicit PollRegistration(PollHub *h) : hub(h) {}
    int attach() {
        {
            std::lock_guard<std::mutex> guard(hub->lock);
            if (hub->waiters.size() == hub->limit) return -12;
            hub->waiters.push_back(&waiter); registered = true;
        }
        // The slot is already bounded, but callbacks skip its empty owner.
        // A fresh readiness scan after publication covers every earlier change.
        void *owner = nullptr;
        int error = hub->wake.create(hub->wake.context, &owner);
        if (error) return error < 0 ? error : -5;
        if (!owner) return -5;
        std::lock_guard<std::mutex> guard(hub->lock);
        waiter.owner = owner;
        return 0;
    }
    int error() {
        std::lock_guard<std::mutex> guard(hub->lock);
        return waiter.error;
    }
    ~PollRegistration() {
        if (registered) {
            std::lock_guard<std::mutex> guard(hub->lock);
            auto found = std::find(hub->waiters.begin(), hub->waiters.end(), &waiter);
            hub->waiters.erase(found);
        }
        if (waiter.owner) (void)hub->wake.close(waiter.owner);
    }
};
static void put(unsigned char *, uint64_t, unsigned);
static uint64_t epoll_word(const unsigned char *bytes, unsigned count) {
    uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) value |= uint64_t(bytes[i]) << (8 * i);
    return value;
}
static int monotonic_ns(artbox_kernel_thread *thread, int64_t &value) {
    if (!thread->system.clock) return -95;
    artbox_timespec time{};
    int error = thread->system.clock(1, &time);
    if (error) return error;
    if (time.seconds < 0 || time.nanoseconds < 0 || time.nanoseconds >= 1000000000 ||
        time.seconds > (INT64_MAX - time.nanoseconds) / 1000000000) return -75;
    value = time.seconds * 1000000000 + time.nanoseconds;
    return 0;
}
// Descriptor mutex held. No interest owns the endpoint: mapped lifetime is
// tracked by Binder's existing token/watch, not by the fd's current occupant.
static int epoll_scan(artbox_vfs *fs, EpollOpen &ep, artbox_kernel_thread *thread,
    uint64_t output, int maximum) {
    size_t remaining = ep.interests.size(), index = ep.cursor;
    int copied = 0;
    while (remaining && !ep.interests.empty() && copied < maximum) {
        --remaining;
        index %= ep.interests.size();
        const auto interest = ep.interests[index];
        int ready = artbox_binder_device_events(fs->binder, interest.token, thread->tid);
        if (ready == -9) {
            ep.interests.erase(ep.interests.begin() + static_cast<ptrdiff_t>(index));
            continue;
        }
        if (ready < 0) return copied ? copied : ready;
        const uint32_t events = static_cast<uint32_t>(ready) & (interest.events | 0x18u);
        if (events) {
            unsigned char bytes[16] = {};
            put(bytes, events, 4); put(bytes + 8, interest.data, 8);
            int error = artbox_vm_write(thread->vm, output + static_cast<uint64_t>(copied) * 16, bytes, sizeof(bytes));
            if (error) return copied ? copied : error;
            ++copied;
        }
        ++index;
    }
    ep.cursor = ep.interests.empty() ? 0 : index % ep.interests.size();
    return copied;
}
static int64_t epoll_dispatch(artbox_vfs *fs, artbox_kernel_thread *thread, uint64_t number,
    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    if (!fs || !thread || !thread->vm || thread->tid <= 0) return -22;
    try {
        std::unique_lock<std::mutex> guard(fs->lock);
        if (!fs->poll) return -38;
        if (number == 20) {
            uint32_t flags = static_cast<uint32_t>(a0);
            if (flags & ~UINT32_C(0x80000)) return -22;
            size_t slot = 0;
            while (slot < fs->descriptors.size() && fs->descriptors[slot].kind) ++slot;
            if (slot == fs->descriptors.size()) return -24;
            descriptor d; d.kind = 10; d.flags = flags | 2;
            d.epoll = std::make_shared<EpollOpen>(thread->vm);
            d.epoll->interests.reserve(fs->poll->interests);
            fs->descriptors[slot] = std::move(d);
            return static_cast<int64_t>(slot + 3);
        }
        descriptor *d = get(fs, static_cast<int32_t>(a0));
        if (!d) return -9;
        if (!d->epoll) return -22;
        const auto ep = d->epoll; // Survives close/reuse while this wait is active.
        if (ep->vm != thread->vm) return -95;
        if (number == 21) {
            const int operation = static_cast<int32_t>(a1), fd = static_cast<int32_t>(a2);
            if (operation < 1 || operation > 3 || fd == static_cast<int32_t>(a0)) return -22;
            descriptor *target = get(fs, fd);
            if (!target) return -9;
            if (target->epoll) return -95; // Nested epoll needs its own cycle/lifetime contract.
            if (!target->binder) return -1;
            if (target->binder->vm != thread->vm) return -95;
            unsigned char bytes[16]{};
            if (operation != 2 && artbox_vm_read(thread->vm, a3, bytes, sizeof(bytes))) return -14;
            const uint32_t events = static_cast<uint32_t>(epoll_word(bytes, 4));
            if (events & ~UINT32_C(0x201f)) return -95; // Only tested level-triggered interests.
            const uint64_t token = target->binder->token;
            // Reap dead interests before enforcing this epoll's configured cap.
            for (size_t i = 0; i < ep->interests.size();) {
                if (artbox_binder_device_events(fs->binder, ep->interests[i].token, thread->tid) == -9)
                    ep->interests.erase(ep->interests.begin() + static_cast<ptrdiff_t>(i));
                else ++i;
            }
            auto found = std::find_if(ep->interests.begin(), ep->interests.end(),
                [fd, token](const EpollInterest &interest) { return interest.fd == fd && interest.token == token; });
            if (operation == 1) {
                if (found != ep->interests.end()) return -17;
                if (ep->interests.size() == fs->poll->interests) return -28;
                int ready = artbox_binder_device_events(fs->binder, token, thread->tid);
                if (ready < 0) return ready;
                ep->interests.push_back(EpollInterest{fd, events, token, epoll_word(bytes + 8, 8)});
            } else {
                if (found == ep->interests.end()) return -2;
                if (operation == 2) ep->interests.erase(found);
                else { found->events = events; found->data = epoll_word(bytes + 8, 8); }
            }
            poll_notify(fs->poll.get());
            return 0;
        }
        const int maximum = static_cast<int32_t>(a2), timeout = static_cast<int32_t>(a3);
        if (maximum <= 0 || maximum > INT32_MAX / 16) return -22;
        const uint64_t length = static_cast<uint64_t>(maximum) * 16;
        if (a1 > static_cast<uint64_t>(INT64_MAX) - length) return -14;
        if (a4) return a5 == 8 ? -95 : -22; // Never drop an unimplemented mask substitution.
        int64_t deadline = 0;
        if (timeout > 0) {
            int error = monotonic_ns(thread, deadline);
            if (error) return error;
            if (deadline > INT64_MAX - int64_t(timeout) * 1000000) return -75;
            deadline += int64_t(timeout) * 1000000;
        }
        const bool interruptible = artbox_signals_interrupt_number(thread) != 0;
        const uint64_t epoch = artbox_signals_interrupt_epoch(thread);
        PollRegistration registration(fs->poll.get());
        for (;;) {
            int result = epoll_scan(fs, *ep, thread, a1, maximum);
            if (result || !timeout) return result;
            guard.unlock();
            if (interruptible && artbox_signals_interrupt_epoch(thread) != epoch) return -4;
            int milliseconds = 5; // Passive VM watches and signal epochs cannot call poll_notify.
            if (timeout > 0) {
                int64_t now = 0;
                int error = monotonic_ns(thread, now);
                if (error) return error;
                if (now >= deadline) return 0;
                const int64_t remaining = deadline - now;
                if (remaining < 5000000) milliseconds = static_cast<int>((remaining + 999999) / 1000000);
            }
            if (!registration.registered) {
                int error = registration.attach();
                if (error) return error;
                guard.lock();
                continue; // Register first, then perform the final readiness check before sleep.
            }
            int error = registration.error();
            if (error) return error;
            result = fs->poll->wake.wait(registration.waiter.owner, milliseconds);
            if (result < 0) return result;
            if (result > 1) return -5;
            guard.lock();
        }
    } catch (const std::exception &) { return -12; }
}
extern "C" int64_t artbox_vfs_mmap(artbox_vfs *fs, artbox_vm *vm, uint64_t address,
    uint64_t length, uint64_t prot, uint64_t flags, int64_t fd, uint64_t offset) {
    if (!fs || !vm) return -22;
    if (flags & 0x20) return artbox_vm_mmap(vm, address, length, prot, flags, fd, offset);
    if (prot & 4) return -1;
    std::unique_lock<std::mutex> guard(fs->lock);
    descriptor *d = get(fs, static_cast<int32_t>(fd));
    if (!d) return -9;
    if (d->kind == 9) {
        if ((d->flags & 3) == 1) return -13; // mmap requires a readable descriptor.
        auto binder = d->binder;
        guard.unlock();
        if (binder->vm != vm) return -95;
        return artbox_binder_device_mmap(binder->device, binder->token, address, length, prot, flags, offset);
    }
    if (d->kind != 5 || d->directory) return -19;
    unsigned access = d->flags & 3;
    if (access == 1) return -13; // Every file mapping requires readable backing.
    if (!fs->files.mapping.acquire) return -95;
    unsigned maximum = (flags & 0xf) == 1 && access == 0 ? 1u : 3u;
    return artbox_vm_map_file(vm, address, length, prot, flags, offset, d->handle, &fs->files.mapping, maximum);
}
struct path_info {
    std::string relative, canonical;
    void *directory = nullptr;
    bool trailing = false, terminal_dot = false;
};
static int path(artbox_vfs *fs, artbox_vm *vm, uint64_t address, int32_t dirfd, path_info &out) {
    char bytes[4096]; size_t length = 0;
    for (; length < sizeof(bytes); ++length) {
        if (address > UINT64_MAX - length || artbox_vm_read(vm, address + length, bytes + length, 1)) return -14;
        if (!bytes[length]) break;
    }
    if (length == sizeof(bytes)) return -36;
    if (!length) return -2;
    out.trailing = bytes[length - 1] == '/';
    size_t tail = length;
    while (tail && bytes[tail - 1] == '/') --tail;
    out.terminal_dot = tail && bytes[tail - 1] == '.' && (tail == 1 || bytes[tail - 2] == '/');
    std::string prefix;
    if (bytes[0] != '/' && dirfd != -100) {
        descriptor *base = get(fs, dirfd);
        if (!base) return -9;
        if (!base->directory) return -20;
        out.directory = base->handle; prefix = base->path;
    }
    size_t cursor = 0;
    while (cursor < length) {
        while (cursor < length && bytes[cursor] == '/') ++cursor;
        size_t start = cursor;
        while (cursor < length && bytes[cursor] != '/') ++cursor;
        size_t size = cursor - start;
        if (!size || (size == 1 && bytes[start] == '.')) continue;
        if (size == 2 && bytes[start] == '.' && bytes[start + 1] == '.') return -1;
        if (!out.relative.empty()) out.relative += '/';
        out.relative.append(bytes + start, size);
    }
    out.canonical = prefix;
    if (!prefix.empty() && !out.relative.empty()) out.canonical += '/';
    out.canonical += out.relative;
    return out.canonical.size() >= sizeof(bytes) ? -36 : 0;
}
static unsigned device(const std::string &name) {
    return name == "dev/null" ? 1u : name == "dev/zero" ? 2u : name == "dev/urandom" ? 3u : name == "dev" ? 4u :
           name == "proc/self/cmdline" ? 6u : name == "proc" ? 7u : name == "proc/self" ? 8u :
           name == "dev/binder" ? 9u : 0u;
}
static bool virtual_directory(unsigned kind) { return kind == 4 || kind == 7 || kind == 8; }
static bool below(const std::string &name, const char *prefix) {
    size_t n = std::strlen(prefix);
    return name.compare(0, n, prefix) == 0 && (name.size() == n || name[n] == '/');
}
struct Walk {
    artbox_vfs *fs;
    void *current, *owned = nullptr;
    std::string leaf;
    Walk(artbox_vfs *f, void *base) : fs(f), current(base) {}
    ~Walk() { if (owned) (void)fs->files.close(owned); }
    int resolve(const std::string &relative) {
        size_t begin = 0, slash;
        while ((slash = relative.find('/', begin)) != std::string::npos) {
            std::string component = relative.substr(begin, slash - begin);
            void *next = nullptr;
            int error = fs->files.open(fs->files.context, current, component.c_str(), 0x84000, 0, &next);
            if (error) return error;
            if (owned) {
                error = fs->files.close(owned);
                owned = next; current = next;
                if (error) return error;
            } else { owned = next; current = next; }
            begin = slash + 1;
        }
        leaf = relative.substr(begin);
        if (leaf.empty()) leaf = ".";
        return 0;
    }
};
static void put(unsigned char *out, uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) out[i] = static_cast<unsigned char>(value >> (8 * i));
}
static int stat_bytes(artbox_vm *vm, uint64_t address, const artbox_file_info &f) {
    unsigned char bytes[128] = {};
    put(bytes, f.device, 8); put(bytes + 8, f.inode, 8); put(bytes + 16, f.mode, 4);
    put(bytes + 20, f.links, 4); put(bytes + 24, f.uid, 4); put(bytes + 28, f.gid, 4);
    put(bytes + 32, f.rdevice, 8); put(bytes + 48, f.size, 8); put(bytes + 56, f.block_size, 4);
    put(bytes + 64, f.blocks, 8);
    put(bytes + 72, static_cast<uint64_t>(f.access_seconds), 8); put(bytes + 80, static_cast<uint64_t>(f.access_nanoseconds), 8);
    put(bytes + 88, static_cast<uint64_t>(f.modify_seconds), 8); put(bytes + 96, static_cast<uint64_t>(f.modify_nanoseconds), 8);
    put(bytes + 104, static_cast<uint64_t>(f.change_seconds), 8); put(bytes + 112, static_cast<uint64_t>(f.change_nanoseconds), 8);
    return artbox_vm_write(vm, address, bytes, sizeof(bytes));
}
static artbox_file_info device_info(unsigned kind) {
    artbox_file_info f{};
    f.device = 1; f.inode = kind; f.links = 1; f.block_size = 4096;
    if (kind == 9) { f.mode = 0020666; f.rdevice = 0x10000; return f; }
    if (kind >= 6) { f.uid = f.gid = 10000; f.block_size = 1024; }
    if (virtual_directory(kind)) { f.mode = 0040555; return f; }
    if (kind == 6) { f.mode = 0100444; return f; } // Linux proc inode size is zero.
    f.mode = 0020000u | (kind == 3 ? 0444u : 0666u);
    f.rdevice = 0x100u + (kind == 1 ? 3u : kind == 2 ? 5u : 9u);
    return f;
}
struct Transfer { artbox_vfs *fs; descriptor *file; bool writing; };
static int64_t transfer(void *context, void *buffer, size_t length) {
    Transfer *t = static_cast<Transfer*>(context);
    return t->writing ? t->fs->files.write(t->file->handle, buffer, length) :
                        t->fs->files.read(t->file->handle, buffer, length);
}
extern "C" int64_t artbox_vfs_call(artbox_vfs *fs, artbox_kernel_thread *thread, uint64_t number,
                                   uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    if (!fs || !thread || !thread->vm || !thread->system.random) return -22;
    if (number == 29) {
        // Pin the open description, then release the table lock before driver
        // dispatch. A blocking read must not serialize all descriptors.
        std::shared_ptr<BinderOpen> binder;
        {
            std::lock_guard<std::mutex> guard(fs->lock);
            descriptor *d = get(fs, static_cast<int32_t>(a0));
            if (!d) return -9;
            if (!d->binder) return -25;
            binder = d->binder;
        }
        if (binder->vm != thread->vm) return -95; // Cross-address-space FDs are not supported.
        return artbox_binder_device_ioctl_interruptible(binder->device, binder->token, thread,
            static_cast<uint32_t>(a1), a2);
    }
    if (number == 17) {
        // Relative path lookup already uses the virtual root as cwd. Never
        // expose the host directory backing that root. Size is unsigned long;
        // Linux checks capacity before copying only the actual pathname bytes.
        if (a1 < 2) return -34;
        const char cwd[2] = {'/', '\0'};
        int error = artbox_vm_write(thread->vm, a0, cwd, sizeof(cwd));
        return error ? error : 2;
    }
    if (number != 35 && number != 48 && number != 56 && number != 57 && number != 62 && number != 63 && number != 64 && number != 79 && number != 80)
        return -38;
    try {
        std::lock_guard<std::mutex> guard(fs->lock);
        if (number == 35) {
            unsigned flags = static_cast<uint32_t>(a2);
            if (flags & ~0x200u) return -22;
            if (flags) return -95; // AT_REMOVEDIR is a separate directory-mutation contract.
            path_info p;
            int error = path(fs, thread->vm, a1, static_cast<int32_t>(a0), p);
            if (error) return error;
            if (below(p.canonical, "system") || below(p.canonical, "dev") || below(p.canonical, "proc")) return -30;
            if (!fs->files.unlink) return -38;
            Walk walk(fs, p.directory);
            // Normalizing a terminal '/.' would delete its parent pathname.
            std::string relative = p.relative;
            if (p.terminal_dot && !relative.empty()) relative += "/.";
            if ((error = walk.resolve(relative))) return error;
            return fs->files.unlink(fs->files.context, walk.current, walk.leaf.c_str(), p.trailing);
        }
        if (number == 48 || number == 56 || number == 79) {
            unsigned flags = static_cast<uint32_t>(a2);
            if (number == 48 && (flags & ~7u)) return -22;
            if (number == 79 && a3 != 0 && a3 != 0x100) return -95;
            path_info p;
            int error = path(fs, thread->vm, a1, static_cast<int32_t>(a0), p);
            if (error) return error;
            unsigned kind = device(p.canonical);
            if (kind == 9 && !fs->binder) return -2;
            if (!kind && (below(p.canonical, "dev") || below(p.canonical, "proc"))) return -2;
            if (kind == 6 && fs->commandline.empty()) return -2;
            if (!kind && !fs->files.open) return -38;
            if (kind && !virtual_directory(kind) && p.trailing) return -20;
            // /proc/self is a process alias. Following it is supported; exposing
            // its symlink metadata requires a later readlink/lstat contract.
            if (kind == 8 && number == 79 && a3 == 0x100) return -95;
            if (kind == 8 && number == 56 && (flags & 0x8000)) return (flags & 0x4000) ? -20 : -40;
            if (number == 48 || number == 79) {
                artbox_file_info info{};
                if (kind) info = device_info(kind);
                else {
                    Walk walk(fs, p.directory);
                    if ((error = walk.resolve(p.relative)) ||
                        (error = fs->files.stat_at(fs->files.context, walk.current, walk.leaf.c_str(), &info))) return error;
                    if (p.trailing && (info.mode & 0170000) != 0040000) return -20;
                }
                if (number == 79) return stat_bytes(thread->vm, a2, info);
                unsigned permissions = (info.mode >> 6) & 7;
                if (below(p.canonical, "system") && (flags & 2)) return -30;
                return (permissions & flags) == flags ? 0 : -13;
            }
            if ((flags & 3u) == 3u) return -22;
            if (flags & ~(3u | 0x40u | 0x80u | 0x100u | 0x200u | 0x400u | 0x800u | 0x8000u | 0x4000u | 0x20000u | 0x80000u)) return -95;
            if (kind && (flags & 0xc0) == 0xc0) return -17;
            if (kind && !virtual_directory(kind) && (flags & 0x4000)) return -20;
            if (virtual_directory(kind) && ((flags & 3) || (flags & 0x200))) return -21;
            if ((kind == 3 || kind == 6) && (flags & 3)) return -13;
            if (kind == 6 && (flags & 0x200)) return -13;
            if (below(p.canonical, "system") && ((flags & 3) || (flags & (0x40 | 0x200)))) return -30;
            size_t slot = 0;
            while (slot < fs->descriptors.size() && fs->descriptors[slot].kind) ++slot;
            if (slot == fs->descriptors.size()) return -24;
            descriptor d; d.kind = kind ? kind : 5; d.flags = flags; d.directory = virtual_directory(kind); d.path = p.canonical;
            if (kind == 9) {
                d.binder = std::make_shared<BinderOpen>(fs->binder, thread->vm);
                if ((error = artbox_binder_device_open(fs->binder, thread->vm, thread->pid,
                        fs->binder_uid, &d.binder->token))) return error;
                if ((error = artbox_binder_device_set_nonblocking(fs->binder, d.binder->token,
                        !!(flags & 0x800)))) return error;
            }
            if (!kind) {
                Walk walk(fs, p.directory);
                if ((error = walk.resolve(p.relative))) return error;
                unsigned native_flags = flags | (p.trailing ? 0x4000u : 0);
                if ((error = fs->files.open(fs->files.context, walk.current, walk.leaf.c_str(), native_flags,
                                             static_cast<uint32_t>(a3) & 0777u, &d.handle))) return error;
                artbox_file_info info{};
                if ((error = fs->files.stat(d.handle, &info))) { (void)fs->files.close(d.handle); return error; }
                d.directory = (info.mode & 0170000) == 0040000;
            }
            fs->descriptors[slot] = std::move(d);
            return static_cast<int64_t>(slot + 3);
        }
        descriptor *d = get(fs, static_cast<int32_t>(a0));
        if (!d) return -9;
        if (number == 57) {
            int error = d->kind == 5 ? fs->files.close(d->handle) : 0;
            *d = descriptor{}; return error;
        }
        if (d->epoll) return number == 63 || number == 64 ? -22 : -95;
        if (number == 62) {
            if (d->kind == 9) return static_cast<uint32_t>(a2) <= 4 ? -29 : -22;
            if (d->kind == 6) {
                unsigned origin = static_cast<uint32_t>(a2);
                if (origin > 4) return -22;
                if (origin > 2) return -95; // SEEK_DATA/HOLE are outside this snapshot contract.
                int64_t offset = static_cast<int64_t>(a1), base = origin == 1 ? static_cast<int64_t>(d->position) : 0;
                if (offset < -base || offset > INT64_MAX - base) return -22;
                d->position = static_cast<uint64_t>(base + offset);
                return base + offset;
            }
            if (d->kind != 5) return static_cast<uint32_t>(a2) <= 4 ? 0 : -22;
            return fs->files.seek(d->handle, static_cast<int64_t>(a1), static_cast<uint32_t>(a2));
        }
        if (number == 80) {
            artbox_file_info info{};
            if (d->kind == 5) { int error = fs->files.stat(d->handle, &info); if (error) return error; }
            else info = device_info(d->kind);
            return stat_bytes(thread->vm, a1, info);
        }
        bool writing = number == 64;
        unsigned access = d->flags & 3;
        if ((writing && !access) || (!writing && access == 1)) return -9;
        if (d->kind == 9) return -22; // Binder has ioctl/mmap, no read/write methods.
        if (d->directory) return -21;
        size_t page = artbox_vm_page_size(thread->vm);
        uint64_t maximum = static_cast<uint64_t>(INT32_MAX) & ~(static_cast<uint64_t>(page) - 1);
        uint64_t count = a2 > maximum ? maximum : a2;
        if (d->kind != 5 && writing) return static_cast<int64_t>(count);
        if (d->kind == 1) return 0;
        uint64_t done = 0;
        while (done < count) {
            if (d->kind == 6 && d->position >= fs->commandline.size()) break;
            if (a1 > UINT64_MAX - done) return done ? static_cast<int64_t>(done) : -14;
            uint64_t address = a1 + done;
            size_t chunk = page - static_cast<size_t>(address % page);
            if (chunk > count - done) chunk = static_cast<size_t>(count - done);
            int64_t result;
            if (d->kind == 6) {
                size_t remaining = fs->commandline.size() - static_cast<size_t>(d->position);
                if (chunk > remaining) chunk = remaining;
                int error = artbox_vm_write(thread->vm, address, fs->commandline.data() + d->position, chunk);
                if (!error) d->position += chunk;
                result = error ? error : static_cast<int64_t>(chunk);
            } else if (d->kind == 5) {
                Transfer t{fs, d, writing};
                result = artbox_vm_transfer(thread->vm, address, chunk, writing ? 1 : 2, transfer, &t);
                if (result == -14 && !writing && address <= static_cast<uint64_t>(INT64_MAX) - chunk) {
                    // A regular-file read at EOF copies no bytes, even when its
                    // destination is unmapped. Query without advancing the FD.
                    artbox_file_info info{};
                    int64_t position = fs->files.seek(d->handle, 0, 1);
                    if (position >= 0 && !fs->files.stat(d->handle, &info) && static_cast<uint64_t>(position) >= info.size)
                        result = 0;
                }
            } else {
                unsigned char bytes[256] = {};
                if (chunk > sizeof(bytes)) chunk = sizeof(bytes);
                int error = d->kind == 3 ? thread->system.random(bytes, chunk) : 0;
                if (!error) error = artbox_vm_write(thread->vm, address, bytes, chunk);
                result = error ? error : static_cast<int64_t>(chunk);
            }
            if (result < 0) return done ? static_cast<int64_t>(done) : result;
            done += static_cast<uint64_t>(result);
            if (result < static_cast<int64_t>(chunk)) break;
        }
        return static_cast<int64_t>(done);
    } catch (const std::exception&) { return -12; }
}
