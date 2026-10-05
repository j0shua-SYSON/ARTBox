// Original user-mode Binder endpoint boundary. SPDX-License-Identifier: MIT
#include "artbox/binder_device.h"
#include "artbox/binder_arena.h"
#include "artbox/signals.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <mutex>
#include <new>
#include <vector>

namespace {
enum { entered = 1, registered = 2, exited = 4, invalid = 8 };
constexpr size_t packet_limit = 1024;
constexpr size_t object_limit = 64, claim_limit = 4096;
struct Thread {
    int32_t tid; unsigned looper;
    uint64_t incoming, outgoing;
    uint32_t error; unsigned completions;
    bool dead_reply, initial_return, active;
};
struct Endpoint {
    uint64_t token = 0;
    bool opened = false, mapped_once = false, nonblocking = false;
    artbox_vm *vm = nullptr;
    artbox_vm *receive_owner = nullptr;
    artbox_vm_mapping_watch *receive_watch = nullptr;
    uint64_t receive_base = 0;
    size_t receive_size = 0;
    void *receive_writable = nullptr;
    artbox_binder_arena *arena = nullptr;
    int32_t pid = 0;
    uint32_t uid = 0, max_threads = 0;
    size_t active_calls = 0;
    std::vector<Thread> threads;
};
struct Transaction {
    uint64_t id, source, target;
    int32_t source_tid, target_tid;
};
struct Work {
    uint64_t endpoint;
    int32_t tid;
    uint32_t command;
    uint64_t transaction;
    uint64_t async_node;
    artbox_binder_transaction value;
};
struct Reference {
    uint64_t endpoint, owner, node;
    uint32_t handle, strong, weak, temporary_strong;
    size_t death; // One-based slot; clearing detaches it immediately.
};
struct Node {
    uint64_t id, owner, pointer, cookie;
    uint32_t flags;
    int32_t notification_tid;
    bool manager, dead, has_strong, has_weak, pending_strong, pending_weak;
    uint64_t async_buffer;
};
struct Claim {
    uint64_t endpoint, buffer, node;
    uint32_t handle;
    bool remote;
};
enum DeathState { armed, death_queued, death_delivered, death_acknowledged, clear_queued };
struct Death {
    uint64_t endpoint, owner, cookie;
    int32_t tid;
    DeathState state;
    bool clearing;
};
struct Observer {
    uint64_t subscription = 0;
    void (*notify)(void *) = nullptr;
    void *context = nullptr;
};
uint32_t read32(const unsigned char *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint64_t read64(const unsigned char *p) {
    return uint64_t(read32(p)) | (uint64_t(read32(p + 4)) << 32);
}
void write64(unsigned char *p, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) p[i] = static_cast<unsigned char>(value >> (i * 8));
}
void write32(unsigned char *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(value >> (i * 8));
}
int transfer_packet(artbox_binder_device *, Endpoint &, Thread &, uint64_t, uint32_t);
int read_work(artbox_binder_device *, Endpoint &, Thread &, unsigned char *);
int read_wait(artbox_binder_device *, Endpoint &, int32_t, unsigned char *,
    std::unique_lock<std::mutex> &, artbox_kernel_thread *, uint64_t);
int free_packet(artbox_binder_device *, Endpoint &, uint64_t);
int reference_command(artbox_binder_device *, Endpoint &, uint64_t, uint32_t);
int death_command(artbox_binder_device *, Endpoint &, Thread &, uint64_t, uint32_t);
int node_done_command(artbox_binder_device *, Endpoint &, uint64_t, uint32_t);
int command(artbox_binder_device *device, Endpoint &endpoint, Thread &thread,
    uint64_t address, size_t &consumed) {
    unsigned char bytes[76] = {};
    if (artbox_vm_read(endpoint.vm, address, bytes, 4)) return -14;
    consumed = 4;
    switch (read32(bytes)) {
    case ARTBOX_BC_ENTER_LOOPER:
        if (thread.looper & registered) thread.looper |= invalid;
        thread.looper |= entered;
        return 0;
    case ARTBOX_BC_REGISTER_LOOPER:
        // There are no spawn requests until receive/threadpool delivery exists.
        thread.looper |= registered | invalid;
        return 0;
    case ARTBOX_BC_EXIT_LOOPER:
        thread.looper |= exited;
        return 0;
    case ARTBOX_BC_TRANSACTION: case ARTBOX_BC_REPLY:
        consumed = 68;
        return transfer_packet(device, endpoint, thread, address, read32(bytes));
    case ARTBOX_BC_FREE_BUFFER:
        consumed = 12;
        return free_packet(device, endpoint, address);
    case ARTBOX_BC_INCREFS: case ARTBOX_BC_ACQUIRE:
    case ARTBOX_BC_RELEASE: case ARTBOX_BC_DECREFS:
        consumed = 8;
        return reference_command(device, endpoint, address, read32(bytes));
    case ARTBOX_BC_REQUEST_DEATH_NOTIFICATION: case ARTBOX_BC_CLEAR_DEATH_NOTIFICATION:
    case ARTBOX_BC_DEAD_BINDER_DONE:
        consumed = read32(bytes) == ARTBOX_BC_DEAD_BINDER_DONE ? 12 : 16;
        return death_command(device, endpoint, thread, address, read32(bytes));
    case ARTBOX_BC_INCREFS_DONE: case ARTBOX_BC_ACQUIRE_DONE:
        consumed = 20;
        return node_done_command(device, endpoint, address, read32(bytes));
    default:
        artbox_binder_frame frame;
        size_t cursor = 0;
        return artbox_binder_next(ARTBOX_BINDER_WRITE, bytes, sizeof(bytes), &cursor, &frame)
            == ARTBOX_BINDER_OK ? -95 : -22;
    }
}
int write_read(artbox_binder_device *device, Endpoint &endpoint, Thread &thread, uint64_t argument,
    std::unique_lock<std::mutex> &guard, artbox_kernel_thread *owner, uint64_t epoch) {
    unsigned char bytes[48];
    if (artbox_vm_read(endpoint.vm, argument, bytes, sizeof(bytes))) return -14;
    const uint64_t size = read64(bytes), base = read64(bytes + 16);
    uint64_t consumed = read64(bytes + 8);
    int result = 0;
    if (size > 65536) result = -7; // Explicit host work bound, not a Linux quota.
    while (!result && !thread.error && consumed < size) {
        if (consumed > UINT64_MAX - base || base + consumed > UINT64_MAX - 4) {
            result = -14;
            break;
        }
        size_t command_bytes = 0;
        result = command(device, endpoint, thread, base + consumed, command_bytes);
        if (!result) consumed += command_bytes;
    }
    write64(bytes + 8, consumed);
    if (result) write64(bytes + 32, 0); // A failed write never attempts the read.
    else if (read64(bytes + 24)) result = read_wait(device, endpoint, thread.tid, bytes, guard, owner, epoch);
    // Copy-back failure takes precedence, even after a valid command prefix
    // changed thread state. Do not prevalidate the output and reorder effects.
    if (artbox_vm_write(endpoint.vm, argument, bytes, sizeof(bytes))) return -14;
    return result;
}
}

struct artbox_binder_device {
    std::mutex lock;
    std::condition_variable changed;
    size_t waiters = 0;
    std::array<Observer, 64> observers{};
    uint64_t next_subscription = 1;
    std::vector<Endpoint> endpoints;
    size_t thread_limit;
    uint64_t next_token = 1, manager = 0;
    bool uid_set = false;
    uint32_t manager_uid = 0;
    unsigned char manager_object[24] = {};
    artbox_binder_memory_ops memory{};
    size_t receive_limit = 0;
    uint64_t next_transaction = 1;
    std::vector<Transaction> transactions;
    std::vector<Work> work;
    std::vector<Reference> references;
    std::vector<Death> deaths;
    uint64_t manager_node = 0, next_node = 1;
    std::vector<Node> nodes;
    std::vector<Claim> claims;
};

// The device mutex is held. Listeners may only publish an ordinary-context
// hint; none can acquire VFS/VM locks or reenter this device.
static void notify_changed(artbox_binder_device *device) {
    device->changed.notify_all();
    for (const auto &observer : device->observers) {
        if (observer.subscription) observer.notify(observer.context);
    }
}

extern "C" size_t artbox_binder_device_waiter_count(artbox_binder_device *device) {
    if (!device) return 0;
    std::lock_guard<std::mutex> guard(device->lock);
    return device->waiters;
}
extern "C" int artbox_binder_device_observe(artbox_binder_device *device,
    void (*notify)(void *), void *context, uint64_t *subscription) {
    if (subscription) *subscription = 0;
    if (!device || !notify || !subscription) return -22;
    std::lock_guard<std::mutex> guard(device->lock);
    if (!device->next_subscription) return -24;
    for (auto &observer : device->observers) if (!observer.subscription) {
        observer.subscription = device->next_subscription++;
        observer.context = context; observer.notify = notify;
        *subscription = observer.subscription;
        return 0;
    }
    return -28;
}
extern "C" int artbox_binder_device_unobserve(artbox_binder_device *device, uint64_t subscription) {
    if (!device || !subscription) return -22;
    std::lock_guard<std::mutex> guard(device->lock);
    for (auto &observer : device->observers) if (observer.subscription == subscription) {
        observer = Observer{};
        return 0;
    }
    return -2;
}

namespace {
Endpoint *endpoint_for(artbox_binder_device *device, uint64_t token) {
    for (auto &e : device->endpoints) if (e.token == token && token) return &e;
    return nullptr;
}
Thread *thread_for(Endpoint *endpoint, int32_t tid) {
    if (endpoint) for (auto &thread : endpoint->threads) if (thread.tid == tid) return &thread;
    return nullptr;
}
Thread *admit_thread(artbox_binder_device *device, Endpoint &endpoint, int32_t tid) {
    if (Thread *thread = thread_for(&endpoint, tid)) return thread;
    if (endpoint.threads.size() == device->thread_limit) return nullptr;
    endpoint.threads.push_back({tid, 0, 0, 0, 0, 0, false, true, false});
    return &endpoint.threads.back();
}
Transaction *transaction_for(artbox_binder_device *device, uint64_t id) {
    for (auto &t : device->transactions) if (id && t.id == id) return &t;
    return nullptr;
}
Reference *reference_for(artbox_binder_device *device, uint64_t endpoint, uint32_t handle) {
    for (auto &ref : device->references) if (ref.endpoint == endpoint && ref.handle == handle) return &ref;
    return nullptr;
}
Node *node_for(artbox_binder_device *device, uint64_t id) {
    for (auto &node : device->nodes) if (id && node.id == id) return &node;
    return nullptr;
}
Node *owned_node(artbox_binder_device *device, uint64_t owner, uint64_t pointer) {
    for (auto &node : device->nodes) if (node.id && !node.dead && node.owner == owner && node.pointer == pointer) return &node;
    return nullptr;
}
Node *create_node(artbox_binder_device *device, uint64_t owner, uint64_t pointer,
                  uint64_t cookie, uint32_t flags, int32_t tid, bool manager) {
    if (!device->next_node) return nullptr;
    for (auto &node : device->nodes) if (!node.id) {
        node = {device->next_node++, owner, pointer, cookie, flags, tid,
                manager, false, manager, manager, false, false, 0};
        return &node;
    }
    return nullptr;
}
void node_usage(artbox_binder_device *device, const Node &node, bool &strong, bool &weak) {
    strong = (node.manager && !node.dead) || node.pending_strong;
    weak = node.pending_weak;
    for (const auto &ref : device->references) if (ref.endpoint && ref.node == node.id) {
        strong = strong || ref.strong || ref.temporary_strong;
        weak = true;
    }
    for (const auto &claim : device->claims)
        if (claim.endpoint && claim.node == node.id && !claim.remote) strong = true;
    weak = weak || strong;
}
void collect_nodes(artbox_binder_device *device) {
    for (auto &node : device->nodes) {
        if (!node.id || node.has_strong || node.has_weak) continue;
        bool strong, weak;
        node_usage(device, node, strong, weak);
        if (!strong && !weak) node = {};
    }
}
Reference *ensure_reference(artbox_binder_device *device, uint64_t endpoint, const Node &node) {
    Reference *available = nullptr;
    for (auto &ref : device->references) {
        if (ref.endpoint == endpoint && ref.node == node.id) return &ref;
        if (!ref.endpoint && !available) available = &ref;
    }
    if (!available) return nullptr;
    uint32_t handle = node.id == device->manager_node ? 0 : 1;
    while (reference_for(device, endpoint, handle)) {
        if (handle == UINT32_MAX) return nullptr;
        ++handle;
    }
    *available = {endpoint, node.owner, node.id, handle, 0, 0, 0, 0};
    return available;
}
void drop_reference(artbox_binder_device *device, Reference &ref) {
    if (ref.death) device->deaths[ref.death - 1] = {};
    ref = {};
}
int reference_command(artbox_binder_device *device, Endpoint &endpoint,
                      uint64_t address, uint32_t command_word) {
    if (!device->receive_limit) return -95;
    unsigned char bytes[8];
    if (artbox_vm_read(endpoint.vm, address, bytes, sizeof(bytes))) return -14;
    const uint32_t handle = read32(bytes + 4);
    const bool increment = command_word == ARTBOX_BC_INCREFS || command_word == ARTBOX_BC_ACQUIRE;
    const bool strong = command_word == ARTBOX_BC_ACQUIRE || command_word == ARTBOX_BC_RELEASE;
    Reference *ref = reference_for(device, endpoint.token, handle);
    if (increment && !handle && device->manager) {
        if (device->manager == endpoint.token) return -22;
        Node *node = node_for(device, device->manager_node);
        if (!node) return -5;
        ref = ensure_reference(device, endpoint.token, *node);
        if (!ref) return -12;
    }
    if (!ref) return 0; // Linux consumes unmatched reference operations.
    uint32_t &count = strong ? ref->strong : ref->weak;
    if (increment) {
        if (count == UINT32_MAX) return -12;
        ++count;
    } else if (count) --count;
    if (!ref->strong && !ref->weak && !ref->temporary_strong) drop_reference(device, *ref);
    collect_nodes(device);
    return 0;
}
int node_done_command(artbox_binder_device *device, Endpoint &endpoint,
                      uint64_t address, uint32_t command_word) {
    if (!device->receive_limit) return -95;
    unsigned char bytes[20];
    if (artbox_vm_read(endpoint.vm, address, bytes, sizeof(bytes))) return -14;
    Node *node = owned_node(device, endpoint.token, read64(bytes + 4));
    if (!node || node->cookie != read64(bytes + 12)) return 0;
    if (command_word == ARTBOX_BC_INCREFS_DONE) node->pending_weak = false;
    else node->pending_strong = false;
    if (!node->pending_weak && !node->pending_strong) {
        bool strong, weak;
        node_usage(device, *node, strong, weak);
        // An INCREFS acknowledgement may precede delivery of ACQUIRE when the
        // read buffer was short. Keep that initial callback on the exporter.
        if (!(strong && !node->has_strong) && !(weak && !node->has_weak)) node->notification_tid = 0;
    }
    collect_nodes(device);
    return 0;
}
void queue_clear(Death &death, const Thread &thread) {
    death.state = clear_queued;
    death.tid = (thread.looper & (entered | registered)) ? thread.tid : 0;
}
int death_command(artbox_binder_device *device, Endpoint &endpoint, Thread &thread,
                  uint64_t address, uint32_t command_word) {
    if (!device->receive_limit) return -95;
    unsigned char bytes[16];
    const bool done = command_word == ARTBOX_BC_DEAD_BINDER_DONE;
    if (artbox_vm_read(endpoint.vm, address, bytes, done ? 12 : 16)) return -14;
    if (done) {
        const uint64_t cookie = read64(bytes + 4);
        for (auto &death : device->deaths) {
            if (death.endpoint != endpoint.token || death.cookie != cookie || death.state != death_delivered) continue;
            if (death.clearing) queue_clear(death, thread);
            else death.state = death_acknowledged;
            break;
        }
        return 0;
    }
    Reference *ref = reference_for(device, endpoint.token, read32(bytes + 4));
    if (!ref) return 0;
    const uint64_t cookie = read64(bytes + 8);
    if (command_word == ARTBOX_BC_REQUEST_DEATH_NOTIFICATION) {
        if (ref->death) return 0;
        for (size_t i = 0; i < device->deaths.size(); ++i) {
            Death &death = device->deaths[i];
            if (death.endpoint) continue;
            death = {endpoint.token, ref->owner, cookie, 0,
                     endpoint_for(device, ref->owner) ? armed : death_queued, false};
            ref->death = i + 1;
            return 0;
        }
        return -12;
    }
    if (!ref->death) return 0;
    Death &death = device->deaths[ref->death - 1];
    if (death.cookie != cookie) return 0;
    ref->death = 0;
    death.clearing = true;
    if (death.state == armed || death.state == death_acknowledged) queue_clear(death, thread);
    return 0;
}
bool accepts_process_work(const Thread &thread, bool polling = false) {
    // Read routing retains its entered-looper contract. A readiness snapshot
    // may observe process work before ENTER_LOOPER, without changing that state.
    return !thread.incoming && !thread.outgoing && !thread.dead_reply &&
           (polling || ((thread.looper & entered) && !(thread.looper & (invalid | exited))));
}
uint32_t node_command(artbox_binder_device *device, const Node &node) {
    bool strong, weak;
    node_usage(device, node, strong, weak);
    if (weak && !node.has_weak) return ARTBOX_BR_INCREFS;
    if (strong && !node.has_strong) return ARTBOX_BR_ACQUIRE;
    if (!strong && node.has_strong) return ARTBOX_BR_RELEASE;
    if (!weak && node.has_weak) return ARTBOX_BR_DECREFS;
    return 0;
}
Node *node_event_for(artbox_binder_device *device, const Endpoint &endpoint, const Thread &thread, bool polling = false) {
    for (auto &node : device->nodes) {
        if (node.id && node.owner == endpoint.token && !node.dead && !node.manager &&
            (node.notification_tid == thread.tid || (!node.notification_tid && accepts_process_work(thread, polling))) &&
            node_command(device, node)) return &node;
    }
    return nullptr;
}
size_t death_for(artbox_binder_device *device, const Endpoint &endpoint, const Thread &thread, bool polling = false) {
    for (size_t i = 0; i < device->deaths.size(); ++i) {
        const Death &death = device->deaths[i];
        if (death.endpoint == endpoint.token &&
            (death.state == death_queued || death.state == clear_queued) &&
            (death.tid == thread.tid || (!death.tid && accepts_process_work(thread, polling)))) return i;
    }
    return device->deaths.size();
}
void release_references(artbox_binder_device *device, uint64_t token) {
    for (auto &ref : device->references) if (ref.endpoint == token) drop_reference(device, ref);
    for (auto &death : device->deaths) {
        if (death.endpoint == token) death = {};
        else if (death.endpoint && death.owner == token && death.state == armed) death.state = death_queued;
    }
    for (auto &node : device->nodes) if (node.id && node.owner == token) {
        node.dead = true;
        node.has_strong = node.has_weak = node.pending_strong = node.pending_weak = false;
    }
    collect_nodes(device);
}
void release_claims(artbox_binder_device *device, uint64_t endpoint, uint64_t buffer = 0) {
    for (auto &claim : device->claims) {
        if (claim.endpoint != endpoint || (buffer && claim.buffer != buffer)) continue;
        if (claim.remote) {
            Reference *ref = reference_for(device, endpoint, claim.handle);
            if (ref && ref->node == claim.node) {
                if (ref->temporary_strong) --ref->temporary_strong;
                if (!ref->temporary_strong && !ref->strong && !ref->weak) drop_reference(device, *ref);
            }
        }
        claim = {};
    }
    collect_nodes(device);
}
int add_claim(artbox_binder_device *device, Endpoint &target, uint64_t buffer,
              const Node &node, bool remote, uint32_t &handle) {
    Claim *available = nullptr;
    for (auto &claim : device->claims) if (!claim.endpoint) { available = &claim; break; }
    if (!available) return -12;
    if (remote) {
        Reference *ref = ensure_reference(device, target.token, node);
        if (!ref || ref->temporary_strong == UINT32_MAX) return -12;
        ++ref->temporary_strong;
        handle = ref->handle;
    }
    *available = {target.token, buffer, node.id, handle, remote};
    return 0;
}
void drop_work(artbox_binder_device *device, size_t index) {
    const Work &work = device->work[index];
    Endpoint *owner = endpoint_for(device, work.endpoint);
    if (owner && owner->arena) (void)artbox_binder_arena_cancel(owner->arena, work.value.data_buffer);
    release_claims(device, work.endpoint, work.value.data_buffer);
    device->work.erase(device->work.begin() + static_cast<ptrdiff_t>(index));
}
// Reaping a process or thread must not strand its peer's synchronous wait.
// Error slots are reserved in thread metadata, so cleanup needs no allocation.
void cancel_transactions(artbox_binder_device *device, uint64_t token, int32_t tid = 0) {
    for (auto &t : device->transactions) {
        if (!t.id || !((t.source == token && (!tid || t.source_tid == tid)) ||
                      (t.target == token && (!tid || t.target_tid == tid)))) continue;
        Thread *source = thread_for(endpoint_for(device, t.source), t.source_tid);
        Thread *target = thread_for(endpoint_for(device, t.target), t.target_tid);
        if (source && source->outgoing == t.id) { source->outgoing = 0; source->error = ARTBOX_BR_DEAD_REPLY; }
        if (target && target->incoming == t.id) { target->incoming = 0; target->dead_reply = true; }
        for (size_t i = 0; i < device->work.size();) {
            if (device->work[i].transaction == t.id) drop_work(device, i);
            else ++i;
        }
        t.id = 0;
    }
    for (size_t i = 0; i < device->work.size();) {
        if (device->work[i].endpoint == token && (!tid || device->work[i].tid == tid)) drop_work(device, i);
        else ++i;
    }
}
int reserve_payload(artbox_binder_device *device, Endpoint &source, Thread &thread,
                    Endpoint &target, const artbox_binder_transaction &input,
                    const Node *target_node, artbox_binder_buffer &buffer) {
    if ((artbox_vm_mapping_watch_state(target.receive_watch) & 3u) != 3u) return -95;
    if (input.data_size > target.receive_size || input.offsets_size > target.receive_size) return -12;
    if (input.offsets_size % 8) return -22;
    if (input.offsets_size / 8 > object_limit) return -7;
    if (!target.arena) {
        target.arena = artbox_binder_arena_create(target.receive_writable, target.receive_base,
                                                target.receive_size, packet_limit);
        if (!target.arena) return -12;
    }
    std::vector<unsigned char> snapshot, offsets;
    try {
        snapshot.resize(static_cast<size_t>(input.data_size));
        offsets.resize(static_cast<size_t>(input.offsets_size));
    }
    catch (const std::exception &) { return -12; }
    if (artbox_vm_read(source.vm, input.data_buffer, snapshot.data(), snapshot.size())) return -14;
    if (artbox_vm_read(source.vm, input.offsets_buffer, offsets.data(), offsets.size())) return -14;
    size_t count = 0;
    if (artbox_binder_validate_objects(snapshot.data(), snapshot.size(), offsets.data(), offsets.size(), &count) != ARTBOX_BINDER_OK) return -22;
    for (size_t i = 0; i < count; ++i) {
        const uint32_t type = read32(snapshot.data() + static_cast<size_t>(read64(offsets.data() + i * 8)));
        if (type != ARTBOX_BINDER_TYPE_BINDER && type != ARTBOX_BINDER_TYPE_HANDLE) return -95;
    }
    int result = artbox_binder_arena_reserve(target.arena, input.data_size, input.offsets_size, 0, 0, &buffer);
    if (result) return result;
    if (target_node && !target_node->manager) {
        uint32_t unused = 0;
        result = add_claim(device, target, buffer.address, *target_node, false, unused);
    }
    for (size_t i = 0; !result && i < count; ++i) {
        unsigned char *object = snapshot.data() + static_cast<size_t>(read64(offsets.data() + i * 8));
        Node *node = nullptr;
        if (read32(object) == ARTBOX_BINDER_TYPE_BINDER) {
            node = owned_node(device, source.token, read64(object + 8));
            if (node && node->cookie != read64(object + 16)) { result = -22; break; }
            if (!node) node = create_node(device, source.token, read64(object + 8), read64(object + 16),
                                          read32(object + 4), thread.tid, false);
            if (!node) { result = -12; break; }
        } else {
            Reference *ref = reference_for(device, source.token, read32(object + 8));
            if (ref && (ref->strong || ref->temporary_strong)) node = node_for(device, ref->node);
            if (!node) { result = -22; break; }
        }
        const bool remote = node->owner != target.token;
        uint32_t handle = 0;
        result = add_claim(device, target, buffer.address, *node, remote, handle);
        if (result) break;
        write32(object, remote ? ARTBOX_BINDER_TYPE_HANDLE : ARTBOX_BINDER_TYPE_BINDER);
        write64(object + 8, remote ? handle : node->pointer);
        write64(object + 16, remote ? 0 : node->cookie);
    }
    if (!result) result = artbox_binder_arena_write(target.arena, buffer.address, 0, snapshot.data(), snapshot.size());
    if (!result) result = artbox_binder_arena_write(target.arena, buffer.address,
        buffer.offsets_address - buffer.address, offsets.data(), offsets.size());
    if (result) {
        release_claims(device, target.token, buffer.address);
        (void)artbox_binder_arena_cancel(target.arena, buffer.address);
    }
    return result;
}
int transfer_packet(artbox_binder_device *device, Endpoint &source, Thread &thread,
    uint64_t address, uint32_t command_word) {
    if (!device->receive_limit) return -95;
    unsigned char bytes[68];
    if (artbox_vm_read(source.vm, address, bytes, sizeof(bytes))) return -14;
    artbox_binder_frame frame{command_word, bytes + 4, 64};
    artbox_binder_transaction input;
    if (artbox_binder_decode_transaction(&frame, &input) != ARTBOX_BINDER_OK) return -22;
    // TF_ONE_WAY and TF_ACCEPT_FDS. FD objects themselves remain unsupported.
    // Replies never create asynchronous node work; other flags need contracts.
    const bool reply = command_word == ARTBOX_BC_REPLY;
    const bool oneway = !reply && (input.flags & 1);
    if (input.flags & ~(reply ? UINT32_C(0x10) : UINT32_C(0x11))) return -95;
    if (thread.error || thread.completions == packet_limit || device->work.size() == packet_limit) return -12;
    Endpoint *target = nullptr;
    Thread *caller = nullptr;
    Transaction *transaction = nullptr;
    Node *target_node = nullptr;
    if (!reply) {
        if (!oneway && (thread.incoming || thread.outgoing)) return -95;
        const uint32_t handle = static_cast<uint32_t>(input.target);
        if (!handle) target_node = node_for(device, device->manager_node);
        else {
            Reference *ref = reference_for(device, source.token, handle);
            if (ref && (ref->strong || ref->temporary_strong)) target_node = node_for(device, ref->node);
            if (!target_node) { thread.error = ARTBOX_BR_FAILED_REPLY; return 0; }
        }
        target = target_node && !target_node->dead ? endpoint_for(device, target_node->owner) : nullptr;
        if (!target) { thread.error = ARTBOX_BR_DEAD_REPLY; return 0; }
        if (target->token == source.token || (!handle && target->pid == source.pid)) {
            thread.error = ARTBOX_BR_FAILED_REPLY; return 0;
        }
        if (!oneway) {
            if (!device->next_transaction) return -12;
            for (auto &t : device->transactions) if (!t.id) { transaction = &t; break; }
            if (!transaction) return -12;
        }
    } else {
        if (thread.dead_reply) { thread.dead_reply = false; thread.error = ARTBOX_BR_DEAD_REPLY; return 0; }
        transaction = transaction_for(device, thread.incoming);
        if (!transaction || thread.outgoing) return -95;
        target = endpoint_for(device, transaction->source);
        caller = thread_for(target, transaction->source_tid);
        if (!caller || caller->outgoing != transaction->id) return -95;
    }
    artbox_binder_buffer buffer;
    int result = reserve_payload(device, source, thread, *target, input, target_node, buffer);
    if (result) {
        if (result == -22 || result == -14) { thread.error = ARTBOX_BR_FAILED_REPLY; return 0; }
        return result; // Explicit admission/unsupported-operation limits.
    }
    Work work{};
    work.endpoint = target->token;
    work.tid = reply ? transaction->source_tid : 0;
    work.command = reply ? ARTBOX_BR_REPLY : ARTBOX_BR_TRANSACTION;
    work.value = input;
    work.value.target = reply ? 0 : target_node->pointer;
    work.value.cookie = reply ? 0 : target_node->cookie;
    work.value.sender_pid = reply || oneway ? 0 : source.pid;
    work.value.sender_euid = source.uid;
    work.value.data_buffer = buffer.address;
    work.value.offsets_buffer = buffer.offsets_address;
    if (reply) {
        thread.incoming = 0; caller->outgoing = 0; transaction->id = 0;
    } else if (oneway) {
        work.async_node = target_node->id;
        if (!target_node->async_buffer) target_node->async_buffer = buffer.address;
    } else {
        *transaction = {device->next_transaction++, source.token, target->token, thread.tid, 0};
        thread.outgoing = transaction->id;
        work.transaction = transaction->id;
    }
    ++thread.completions;
    device->work.push_back(work); // Reserved capacity, trivial metadata.
    return 0;
}
int free_packet(artbox_binder_device *device, Endpoint &endpoint, uint64_t address) {
    if (!device->receive_limit) return -95;
    unsigned char bytes[12];
    if (artbox_vm_read(endpoint.vm, address, bytes, sizeof(bytes))) return -14;
    if (!endpoint.arena) return -95;
    // Invalid/interior/duplicate frees are not part of this initial contract.
    const uint64_t buffer = read64(bytes + 4);
    if (artbox_binder_arena_release(endpoint.arena, buffer)) return -95;
    for (auto &node : device->nodes) {
        if (node.owner != endpoint.token || node.async_buffer != buffer) continue;
        node.async_buffer = 0;
        // The first undispatched call for this node becomes process work.
        // Sender/thread teardown cannot cancel an accepted one-way parcel.
        for (const auto &work : device->work) if (work.async_node == node.id) {
            node.async_buffer = work.value.data_buffer;
            break;
        }
        break;
    }
    release_claims(device, endpoint.token, buffer);
    return 0;
}
size_t work_for(artbox_binder_device *device, Endpoint &endpoint, Thread &thread, bool polling = false) {
    const bool process_work = accepts_process_work(thread, polling);
    for (size_t i = 0; i < device->work.size(); ++i) {
        const Work &work = device->work[i];
        if (work.async_node) {
            const Node *node = node_for(device, work.async_node);
            if (!node || node->async_buffer != work.value.data_buffer) continue;
        }
        if (work.endpoint == endpoint.token && (work.tid == thread.tid || (!work.tid && process_work))) return i;
    }
    return device->work.size();
}
bool read_ready(artbox_binder_device *device, Endpoint &endpoint, Thread &thread, bool polling = false) {
    return thread.initial_return || thread.error || thread.completions ||
        work_for(device, endpoint, thread, polling) != device->work.size() ||
        death_for(device, endpoint, thread, polling) != device->deaths.size() ||
        node_event_for(device, endpoint, thread, polling);
}
int read_work(artbox_binder_device *device, Endpoint &endpoint, Thread &thread, unsigned char *header) {
    if (!device->receive_limit) return -95;
    const uint64_t size = read64(header + 24), address = read64(header + 40);
    if (size < 4 || read64(header + 32)) return -95;
    if (size > 65536) return -7;
    if (address > UINT64_MAX - size) return -14;
    unsigned char bytes[68]{};
    write32(bytes, ARTBOX_BR_NOOP);
    if (artbox_vm_write(endpoint.vm, address, bytes, 4)) return -14;
    size_t consumed = 4;
    if (!read_ready(device, endpoint, thread)) { write64(header + 32, 0); return -11; }
    while (size - consumed >= 4) {
        const size_t index = work_for(device, endpoint, thread);
        const size_t death_index = death_for(device, endpoint, thread);
        const bool error = thread.error != 0;
        Node *node = error ? nullptr : node_event_for(device, endpoint, thread);
        const uint32_t node_word = node ? node_command(device, *node) : 0;
        const bool completion = !error && !node && thread.completions;
        const bool notification = !error && !node && !completion && index == device->work.size() && death_index != device->deaths.size();
        if (!error && !node && !completion && !notification && index == device->work.size()) break;
        const size_t length = error || completion ? 4 : node ? 20 : notification ? 12 : 68;
        if (length > size - consumed) break;
        if (error) write32(bytes, thread.error);
        else if (node) {
            write32(bytes, node_word); write64(bytes + 4, node->pointer); write64(bytes + 12, node->cookie);
        }
        else if (completion) write32(bytes, ARTBOX_BR_TRANSACTION_COMPLETE);
        else if (notification) {
            const Death &death = device->deaths[death_index];
            write32(bytes, death.state == clear_queued ? ARTBOX_BR_CLEAR_DEATH_NOTIFICATION_DONE : ARTBOX_BR_DEAD_BINDER);
            write64(bytes + 4, death.cookie);
        }
        else {
            const Work &work = device->work[index];
            const auto &t = work.value;
            write32(bytes, work.command);
            write64(bytes + 4, t.target); write64(bytes + 12, t.cookie);
            write32(bytes + 20, t.code); write32(bytes + 24, t.flags);
            write32(bytes + 28, static_cast<uint32_t>(t.sender_pid)); write32(bytes + 32, t.sender_euid);
            write64(bytes + 36, t.data_size); write64(bytes + 44, t.offsets_size);
            write64(bytes + 52, t.data_buffer); write64(bytes + 60, t.offsets_buffer);
        }
        if (artbox_vm_write(endpoint.vm, address + consumed, bytes, length)) {
            write64(header + 32, consumed); return -14;
        }
        consumed += length;
        if (error) thread.error = 0;
        else if (node) {
            if (node_word == ARTBOX_BR_INCREFS) node->has_weak = node->pending_weak = true;
            else if (node_word == ARTBOX_BR_ACQUIRE) node->has_strong = node->pending_strong = true;
            else if (node_word == ARTBOX_BR_RELEASE) node->has_strong = false;
            else node->has_weak = false;
            collect_nodes(device);
        }
        else if (completion) --thread.completions;
        else if (notification) {
            Death &death = device->deaths[death_index];
            if (death.state == clear_queued) death = {};
            else { death.state = death_delivered; break; } // One death return ends this read.
        }
        else {
            const Work work = device->work[index];
            if (artbox_binder_arena_publish(endpoint.arena, work.value.data_buffer)) return -5;
            if (work.transaction) {
                Transaction *transaction = transaction_for(device, work.transaction);
                if (!transaction) return -5;
                transaction->target_tid = thread.tid;
                thread.incoming = work.transaction;
            }
            device->work.erase(device->work.begin() + static_cast<ptrdiff_t>(index));
        }
    }
    write64(header + 32, consumed);
    return 0;
}
}

// The device lock is held. No guest mapping owns an Endpoint or calls here:
// watches publish passive state, avoiding a VM-to-device lock inversion.
static int release_endpoint(artbox_binder_device *device, Endpoint &endpoint) {
    cancel_transactions(device, endpoint.token);
    release_references(device, endpoint.token);
    release_claims(device, endpoint.token);
    if (endpoint.arena) {
        artbox_binder_arena_discard_all(endpoint.arena);
        (void)artbox_binder_arena_destroy(endpoint.arena);
        endpoint.arena = nullptr;
    }
    int result = endpoint.receive_owner ? artbox_vm_destroy(endpoint.receive_owner) : 0;
    artbox_vm_mapping_watch_destroy(endpoint.receive_watch);
    if (device->manager == endpoint.token) {
        device->manager = 0;
        device->manager_node = 0;
        std::memset(device->manager_object, 0, sizeof(device->manager_object));
    }
    endpoint.token = 0; endpoint.vm = nullptr; endpoint.opened = false;
    endpoint.receive_owner = nullptr; endpoint.receive_watch = nullptr;
    endpoint.receive_writable = nullptr; endpoint.receive_base = 0; endpoint.receive_size = 0;
    endpoint.threads.clear();
    return result;
}
static int reap_closed(artbox_binder_device *device) {
    int result = 0;
    bool changed = false;
    for (auto &endpoint : device->endpoints) {
        if (!endpoint.token || endpoint.opened ||
            (artbox_vm_mapping_watch_state(endpoint.receive_watch) & ARTBOX_VM_MAPPING_LIVE)) continue;
        int error = release_endpoint(device, endpoint);
        changed = true;
        if (!result) result = error;
    }
    if (changed) notify_changed(device);
    return result;
}

namespace {
int read_wait(artbox_binder_device *device, Endpoint &endpoint, int32_t tid, unsigned char *header,
    std::unique_lock<std::mutex> &guard, artbox_kernel_thread *owner, uint64_t epoch) {
    Thread *thread = thread_for(&endpoint, tid);
    if (!thread) return -9;
    int result = read_work(device, endpoint, *thread, header);
    if (result != -11 || endpoint.nonblocking) return result;
    // Publish effects of the write half before sleeping for the read half.
    notify_changed(device);
    struct Waiter {
        artbox_binder_device *device;
        explicit Waiter(artbox_binder_device *d) : device(d) { ++device->waiters; }
        ~Waiter() { --device->waiters; }
    } waiter(device);
    const bool interruptible = owner && artbox_signals_interrupt_number(owner);
    try {
        for (;;) {
            if (interruptible && artbox_signals_interrupt_epoch(owner) != epoch) return -4;
            // Erasing a different thread can shift this endpoint's vector.
            // Never retain its elements across an unlocked wait.
            thread = thread_for(&endpoint, tid);
            if (!thread) return -9;
            if (read_ready(device, endpoint, *thread)) return read_work(device, endpoint, *thread, header);
            // Ordinary mutations notify immediately. Passive mapping teardown
            // and signal-handler epochs require bounded ordinary-context
            // polling; neither callback may enter this mutex or condition.
            device->changed.wait_for(guard, std::chrono::milliseconds(5));
            if ((result = reap_closed(device))) return result;
        }
    } catch (const std::exception &) { return -5; }
}
}

extern "C" artbox_binder_device *artbox_binder_device_create(size_t endpoints, size_t threads) {
    if (!endpoints || endpoints > 1024 || !threads || threads > 1024) return nullptr;
    auto *device = new (std::nothrow) artbox_binder_device;
    if (!device) return nullptr;
    device->thread_limit = threads;
    try {
        device->transactions.resize(packet_limit);
        device->references.resize(packet_limit);
        device->deaths.resize(packet_limit);
        device->nodes.resize(packet_limit);
        device->claims.resize(claim_limit);
        device->work.reserve(packet_limit);
        device->endpoints.resize(endpoints);
        for (auto &endpoint : device->endpoints) endpoint.threads.reserve(threads);
    } catch (const std::exception &) { delete device; return nullptr; }
    return device;
}
extern "C" int artbox_binder_device_set_memory(artbox_binder_device *device,
    const artbox_binder_memory_ops *ops, size_t maximum) {
    if (!device || !ops || !ops->create || !ops->close || !ops->mapping.acquire ||
        !ops->mapping.release || !ops->mapping.map || !ops->mapping.sync ||
        !ops->memory.reserve || !ops->memory.protect || !ops->memory.reset || !ops->memory.release)
        return -22;
    const size_t page = ops->memory.page_size;
    if (page < 4096 || page > 65536 || (page & (page - 1)) || !maximum ||
        maximum > 4 * 1024 * 1024 || maximum % page) return -22;
    std::lock_guard<std::mutex> guard(device->lock);
    if (device->receive_limit) return -114;
    if (device->next_token != 1) return -16;
    device->memory = *ops; device->receive_limit = maximum;
    return 0;
}
extern "C" int artbox_binder_device_destroy(artbox_binder_device *device) {
    if (!device) return -22;
    {
        std::lock_guard<std::mutex> guard(device->lock);
        int result = reap_closed(device);
        if (result) return result;
        for (const auto &observer : device->observers) if (observer.subscription) return -16;
        for (const auto &endpoint : device->endpoints) if (endpoint.token) return -16;
    }
    delete device;
    return 0;
}
extern "C" int artbox_binder_device_open(artbox_binder_device *device, artbox_vm *vm,
    int32_t pid, uint32_t uid, uint64_t *token) {
    if (!device || !vm || pid <= 0 || !token) return -22;
    std::lock_guard<std::mutex> guard(device->lock);
    int result = reap_closed(device);
    if (result) return result;
    if (!device->next_token) return -24;
    for (auto &endpoint : device->endpoints) {
        if (endpoint.token) continue;
        endpoint.token = device->next_token++;
        endpoint.opened = true; endpoint.mapped_once = false; endpoint.nonblocking = false;
        endpoint.vm = vm; endpoint.pid = pid; endpoint.uid = uid;
        endpoint.max_threads = 0;
        endpoint.threads.clear();
        *token = endpoint.token;
        return 0;
    }
    return -24;
}
extern "C" int artbox_binder_device_close(artbox_binder_device *device, uint64_t token) {
    if (!device) return -22;
    if (!token) return -9;
    std::lock_guard<std::mutex> guard(device->lock);
    for (auto &endpoint : device->endpoints) {
        if (endpoint.token != token || !endpoint.opened) continue;
        if (endpoint.active_calls) return -16;
        endpoint.opened = false;
        endpoint.vm = nullptr;
        notify_changed(device);
        return reap_closed(device);
    }
    return -9;
}
extern "C" int artbox_binder_device_set_nonblocking(artbox_binder_device *device, uint64_t token, int enabled) {
    if (!device || (enabled != 0 && enabled != 1)) return -22;
    std::lock_guard<std::mutex> guard(device->lock);
    Endpoint *endpoint = endpoint_for(device, token);
    if (!endpoint || !endpoint->opened) return -9;
    endpoint->nonblocking = enabled != 0;
    return 0;
}
extern "C" int64_t artbox_binder_device_mmap(artbox_binder_device *device, uint64_t token,
    uint64_t address, uint64_t length, uint64_t prot, uint64_t flags, uint64_t offset) {
    if (!device) return -22;
    std::lock_guard<std::mutex> guard(device->lock);
    int error = reap_closed(device);
    if (error) return error;
    auto found = std::find_if(device->endpoints.begin(), device->endpoints.end(),
        [token](const Endpoint &e) { return e.opened && e.token == token; });
    if (found == device->endpoints.end()) return -9;
    Endpoint &endpoint = *found;
    const size_t page = artbox_vm_page_size(endpoint.vm);
    if (!length || length > SIZE_MAX - (page - 1) || offset % page) return -22;
    if (prot & 6) return -1; // Binder forbids writes; ARTBox also forbids executable data.
    if (prot & ~UINT64_C(7)) return -22;
    const unsigned sharing = static_cast<unsigned>(flags & 0xf);
    if (sharing != 1 && sharing != 2) return -22;
    if (offset || (flags & ~UINT64_C(0x4003))) return -95;
    if (endpoint.mapped_once) return -16;
    if (!device->receive_limit || device->memory.memory.page_size != page) return -95;
    const size_t size = (static_cast<size_t>(length) + page - 1) & ~(page - 1);
    if (size > device->receive_limit) return -12;
    const auto &ops = device->memory;
    artbox_vm *owner = artbox_vm_create(&ops.memory, size, 1);
    if (!owner) return -12;
    void *file = nullptr;
    error = ops.create(ops.context, size, &file);
    if (error || !file) { (void)artbox_vm_destroy(owner); return error ? error : -5; }
    const int64_t writable = artbox_vm_map_file(owner, 0, size, 3, 1, 0, file, &ops.mapping, 3);
    if (writable < 0) {
        (void)ops.close(file); (void)artbox_vm_destroy(owner); return writable;
    }
    artbox_vm_mapping_watch *watch = nullptr;
    // Binder's MAP_PRIVATE is a special shared receive VMA, not ordinary COW.
    const int64_t mapped = artbox_vm_map_file_watched(endpoint.vm, address, size,
        prot, 1, 0, file, &ops.mapping, 1, &watch);
    (void)ops.close(file);
    if (mapped < 0) { (void)artbox_vm_destroy(owner); return mapped; }
    endpoint.mapped_once = true;
    endpoint.receive_owner = owner; endpoint.receive_watch = watch;
    endpoint.receive_writable = reinterpret_cast<void *>(static_cast<uintptr_t>(writable));
    endpoint.receive_base = static_cast<uint64_t>(mapped); endpoint.receive_size = size;
    return mapped;
}
extern "C" int artbox_binder_device_events(artbox_binder_device *device, uint64_t token, int32_t tid) {
    if (!device || tid <= 0) return -22;
    std::lock_guard<std::mutex> guard(device->lock);
    int error = reap_closed(device);
    if (error) return error;
    Endpoint *endpoint = endpoint_for(device, token);
    if (!endpoint) return -9;
    if (!device->receive_limit) return -95;
    Thread *thread = admit_thread(device, *endpoint, tid);
    if (!thread) return 8; // POLLERR, matching Binder's failed thread admission.
    if (thread->active) return -16; // Trusted TIDs cannot run concurrently.
    return read_ready(device, *endpoint, *thread, true) ? 1 : 0; // POLLIN only.
}
static int64_t binder_ioctl(artbox_binder_device *device, uint64_t token,
    int32_t tid, uint32_t request, uint64_t argument, artbox_kernel_thread *owner) {
    if (!device || tid <= 0) return -22;
    if (!token) return -9;
    const uint64_t epoch = owner ? artbox_signals_interrupt_epoch(owner) : 0;
    std::unique_lock<std::mutex> guard(device->lock);
    int error = reap_closed(device);
    if (error) return error;
    auto found = std::find_if(device->endpoints.begin(), device->endpoints.end(),
        [token](const Endpoint &e) { return e.opened && e.token == token; });
    if (found == device->endpoints.end()) return -9;
    Endpoint &endpoint = *found;
    if (owner && owner->vm != endpoint.vm) return -95;
    Thread *thread = admit_thread(device, endpoint, tid);
    if (!thread) return -12;
    if (thread->active) return -16; // A trusted guest TID cannot enter two ioctls concurrently.
    thread->active = true;
    ++endpoint.active_calls;
    unsigned char value[24] = {};
    const auto invoke = [&]() -> int64_t {
        switch (request) {
        case ARTBOX_BINDER_VERSION:
            value[0] = ARTBOX_BINDER_PROTOCOL_VERSION;
            return artbox_vm_write(endpoint.vm, argument, value, 4) ? -22 : 0;
        case ARTBOX_BINDER_SET_MAX_THREADS:
            if (artbox_vm_read(endpoint.vm, argument, value, 4)) return -22;
            endpoint.max_threads = read32(value);
            return 0;
        case ARTBOX_BINDER_SET_CONTEXT_MGR_EXT:
        case ARTBOX_BINDER_SET_CONTEXT_MGR:
            if (request == ARTBOX_BINDER_SET_CONTEXT_MGR_EXT &&
                artbox_vm_read(endpoint.vm, argument, value, sizeof(value))) return -22;
            if (device->manager) return -16;
            if (device->uid_set && device->manager_uid != endpoint.uid) return -1;
            if (owned_node(device, token, read64(value + 8))) return -95;
            {
                Node *node = create_node(device, token, read64(value + 8), read64(value + 16), read32(value + 4), 0, true);
                if (!node) return -12;
                device->manager_node = node->id;
            }
            device->manager = token; device->manager_uid = endpoint.uid; device->uid_set = true;
            std::memcpy(device->manager_object, value, sizeof(value));
            return 0;
        case ARTBOX_BINDER_THREAD_EXIT:
            cancel_transactions(device, endpoint.token, tid);
            for (auto &node : device->nodes)
                if (node.id && node.owner == endpoint.token && node.notification_tid == tid) node.notification_tid = 0;
            for (auto &death : device->deaths)
                if (death.endpoint == endpoint.token && death.tid == tid) death = {};
            endpoint.threads.erase(endpoint.threads.begin() + (thread - endpoint.threads.data()));
            return 0;
        case ARTBOX_BINDER_WRITE_READ:
            return write_read(device, endpoint, *thread, argument, guard, owner, epoch);
        case ARTBOX_BINDER_GET_NODE_DEBUG_INFO: case ARTBOX_BINDER_GET_NODE_INFO_FOR_REF:
        case ARTBOX_BINDER_FREEZE: case ARTBOX_BINDER_GET_FROZEN_INFO:
        case ARTBOX_BINDER_ENABLE_ONEWAY_SPAM_DETECTION: case ARTBOX_BINDER_GET_EXTENDED_ERROR:
            return -95;
        default:
            return -22;
        }
    };
    const int64_t result = invoke();
    // Native Binder marks a newly allocated thread for an initial return, then
    // clears it at every ioctl exit, including errors and non-read requests.
    // THREAD_EXIT erased this element; the next call creates a new thread.
    if (request != ARTBOX_BINDER_THREAD_EXIT) {
        Thread *current = thread_for(&endpoint, tid);
        if (current) { current->initial_return = false; current->active = false; }
    }
    --endpoint.active_calls;
    notify_changed(device);
    return result;
}

extern "C" int64_t artbox_binder_device_ioctl(artbox_binder_device *device, uint64_t token,
    int32_t tid, uint32_t request, uint64_t argument) {
    return binder_ioctl(device, token, tid, request, argument, nullptr);
}
extern "C" int64_t artbox_binder_device_ioctl_interruptible(artbox_binder_device *device, uint64_t token,
    artbox_kernel_thread *thread, uint32_t request, uint64_t argument) {
    if (!thread) return -22;
    return binder_ioctl(device, token, thread->tid, request, argument, thread);
}
