// Original user-mode Binder endpoint boundary. SPDX-License-Identifier: MIT
#include "artbox/binder_device.h"
#include "artbox/binder_arena.h"
#include <algorithm>
#include <cstring>
#include <exception>
#include <mutex>
#include <new>
#include <vector>

namespace {
enum { entered = 1, registered = 2, exited = 4, invalid = 8 };
constexpr size_t packet_limit = 1024;
struct Thread {
    int32_t tid; unsigned looper;
    uint64_t incoming, outgoing;
    uint32_t error; unsigned completions;
    bool dead_reply;
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
    artbox_binder_transaction value;
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
int free_packet(artbox_binder_device *, Endpoint &, uint64_t);
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
    default:
        artbox_binder_frame frame;
        size_t cursor = 0;
        return artbox_binder_next(ARTBOX_BINDER_WRITE, bytes, sizeof(bytes), &cursor, &frame)
            == ARTBOX_BINDER_OK ? -95 : -22;
    }
}
int write_read(artbox_binder_device *device, Endpoint &endpoint, Thread &thread, uint64_t argument) {
    unsigned char bytes[48];
    if (artbox_vm_read(endpoint.vm, argument, bytes, sizeof(bytes))) return -14;
    const uint64_t size = read64(bytes), base = read64(bytes + 16);
    uint64_t consumed = read64(bytes + 8);
    int result = 0;
    if (size > 65536) result = -7; // Explicit host work bound, not a Linux quota.
    while (!result && consumed < size) {
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
    else if (read64(bytes + 24)) result = read_work(device, endpoint, thread, bytes);
    // Copy-back failure takes precedence, even after a valid command prefix
    // changed thread state. Do not prevalidate the output and reorder effects.
    if (artbox_vm_write(endpoint.vm, argument, bytes, sizeof(bytes))) return -14;
    return result;
}
}

struct artbox_binder_device {
    std::mutex lock;
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
};

namespace {
Endpoint *endpoint_for(artbox_binder_device *device, uint64_t token) {
    for (auto &e : device->endpoints) if (e.token == token && token) return &e;
    return nullptr;
}
Thread *thread_for(Endpoint *endpoint, int32_t tid) {
    if (endpoint) for (auto &thread : endpoint->threads) if (thread.tid == tid) return &thread;
    return nullptr;
}
Transaction *transaction_for(artbox_binder_device *device, uint64_t id) {
    for (auto &t : device->transactions) if (id && t.id == id) return &t;
    return nullptr;
}
void drop_work(artbox_binder_device *device, size_t index) {
    const Work &work = device->work[index];
    Endpoint *owner = endpoint_for(device, work.endpoint);
    if (owner && owner->arena) (void)artbox_binder_arena_cancel(owner->arena, work.value.data_buffer);
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
int reserve_payload(Endpoint &source, Endpoint &target, const artbox_binder_transaction &input,
                    artbox_binder_buffer &buffer) {
    if ((artbox_vm_mapping_watch_state(target.receive_watch) & 3u) != 3u) return -95;
    if (input.data_size > target.receive_size) return -12;
    if (!target.arena) {
        target.arena = artbox_binder_arena_create(target.receive_writable, target.receive_base,
                                                target.receive_size, packet_limit);
        if (!target.arena) return -12;
    }
    std::vector<unsigned char> snapshot;
    try { snapshot.resize(static_cast<size_t>(input.data_size)); }
    catch (const std::exception &) { return -12; }
    if (artbox_vm_read(source.vm, input.data_buffer, snapshot.data(), snapshot.size())) return -14;
    int result = artbox_binder_arena_reserve(target.arena, input.data_size, 0, 0, 0, &buffer);
    if (result) return result;
    result = artbox_binder_arena_write(target.arena, buffer.address, 0, snapshot.data(), snapshot.size());
    if (result) (void)artbox_binder_arena_cancel(target.arena, buffer.address);
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
    // Initial synchronous byte parcels. Objects/FDs, oneway, nested calls and
    // other flags need their paired reference contracts before enabling them.
    if (input.offsets_size || (input.flags & ~UINT32_C(0x10))) return -95;
    if (thread.error || thread.completions == packet_limit || device->work.size() == packet_limit) return -12;
    const bool reply = command_word == ARTBOX_BC_REPLY;
    Endpoint *target = nullptr;
    Thread *caller = nullptr;
    Transaction *transaction = nullptr;
    if (!reply) {
        if (static_cast<uint32_t>(input.target) || thread.incoming || thread.outgoing) return -95;
        target = endpoint_for(device, device->manager);
        if (!target) { thread.error = ARTBOX_BR_DEAD_REPLY; return 0; }
        if (target->pid == source.pid) { thread.error = ARTBOX_BR_FAILED_REPLY; return 0; }
        if (!device->next_transaction) return -12;
        for (auto &t : device->transactions) if (!t.id) { transaction = &t; break; }
        if (!transaction) return -12;
    } else {
        if (thread.dead_reply) { thread.dead_reply = false; thread.error = ARTBOX_BR_DEAD_REPLY; return 0; }
        transaction = transaction_for(device, thread.incoming);
        if (!transaction || thread.outgoing) return -95;
        target = endpoint_for(device, transaction->source);
        caller = thread_for(target, transaction->source_tid);
        if (!caller || caller->outgoing != transaction->id) return -95;
    }
    artbox_binder_buffer buffer;
    int result = reserve_payload(source, *target, input, buffer);
    if (result) return result;
    Work work{};
    work.endpoint = target->token;
    work.tid = reply ? transaction->source_tid : 0;
    work.command = reply ? ARTBOX_BR_REPLY : ARTBOX_BR_TRANSACTION;
    work.value = input;
    work.value.target = reply ? 0 : read64(device->manager_object + 8);
    work.value.cookie = reply ? 0 : read64(device->manager_object + 16);
    work.value.sender_pid = reply ? 0 : source.pid;
    work.value.sender_euid = source.uid;
    work.value.data_buffer = buffer.address;
    work.value.offsets_buffer = buffer.offsets_address;
    if (reply) {
        thread.incoming = 0; caller->outgoing = 0; transaction->id = 0;
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
    return artbox_binder_arena_release(endpoint.arena, read64(bytes + 4)) ? -95 : 0;
}
size_t work_for(artbox_binder_device *device, Endpoint &endpoint, Thread &thread) {
    const bool process_work = (thread.looper & entered) && !(thread.looper & (invalid | exited)) &&
                              !thread.incoming && !thread.outgoing && !thread.dead_reply;
    for (size_t i = 0; i < device->work.size(); ++i) {
        const Work &work = device->work[i];
        if (work.endpoint == endpoint.token && (work.tid == thread.tid || (!work.tid && process_work))) return i;
    }
    return device->work.size();
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
    const bool pending = thread.error || thread.completions || work_for(device, endpoint, thread) != device->work.size();
    if (!pending) { write64(header + 32, 0); return endpoint.nonblocking ? -11 : -95; }
    while (size - consumed >= 4) {
        const size_t index = work_for(device, endpoint, thread);
        const bool error = thread.error != 0, completion = !error && thread.completions;
        if (!error && !completion && index == device->work.size()) break;
        const size_t length = error || completion ? 4 : 68;
        if (length > size - consumed) break;
        if (error) write32(bytes, thread.error);
        else if (completion) write32(bytes, ARTBOX_BR_TRANSACTION_COMPLETE);
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
        else if (completion) --thread.completions;
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
    if (endpoint.arena) {
        artbox_binder_arena_discard_all(endpoint.arena);
        (void)artbox_binder_arena_destroy(endpoint.arena);
        endpoint.arena = nullptr;
    }
    int result = endpoint.receive_owner ? artbox_vm_destroy(endpoint.receive_owner) : 0;
    artbox_vm_mapping_watch_destroy(endpoint.receive_watch);
    if (device->manager == endpoint.token) {
        device->manager = 0;
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
    for (auto &endpoint : device->endpoints) {
        if (!endpoint.token || endpoint.opened ||
            (artbox_vm_mapping_watch_state(endpoint.receive_watch) & ARTBOX_VM_MAPPING_LIVE)) continue;
        int error = release_endpoint(device, endpoint);
        if (!result) result = error;
    }
    return result;
}

extern "C" artbox_binder_device *artbox_binder_device_create(size_t endpoints, size_t threads) {
    if (!endpoints || endpoints > 1024 || !threads || threads > 1024) return nullptr;
    auto *device = new (std::nothrow) artbox_binder_device;
    if (!device) return nullptr;
    device->thread_limit = threads;
    try {
        device->transactions.resize(packet_limit);
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
        endpoint.opened = false;
        endpoint.vm = nullptr;
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
extern "C" int64_t artbox_binder_device_ioctl(artbox_binder_device *device, uint64_t token,
    int32_t tid, uint32_t request, uint64_t argument) {
    if (!device || tid <= 0) return -22;
    if (!token) return -9;
    std::lock_guard<std::mutex> guard(device->lock);
    int error = reap_closed(device);
    if (error) return error;
    auto found = std::find_if(device->endpoints.begin(), device->endpoints.end(),
        [token](const Endpoint &e) { return e.opened && e.token == token; });
    if (found == device->endpoints.end()) return -9;
    Endpoint &endpoint = *found;
    auto thread = std::find_if(endpoint.threads.begin(), endpoint.threads.end(),
        [tid](const Thread &t) { return t.tid == tid; });
    if (thread == endpoint.threads.end()) {
        if (endpoint.threads.size() == device->thread_limit) return -12;
        endpoint.threads.push_back({tid, 0, 0, 0, 0, 0, false});
        thread = endpoint.threads.end() - 1;
    }
    unsigned char value[24] = {};
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
        device->manager = token; device->manager_uid = endpoint.uid; device->uid_set = true;
        std::memcpy(device->manager_object, value, sizeof(value));
        return 0;
    case ARTBOX_BINDER_THREAD_EXIT:
        cancel_transactions(device, endpoint.token, tid);
        endpoint.threads.erase(thread);
        return 0;
    case ARTBOX_BINDER_WRITE_READ:
        return write_read(device, endpoint, *thread, argument);
    case ARTBOX_BINDER_GET_NODE_DEBUG_INFO: case ARTBOX_BINDER_GET_NODE_INFO_FOR_REF:
    case ARTBOX_BINDER_FREEZE: case ARTBOX_BINDER_GET_FROZEN_INFO:
    case ARTBOX_BINDER_ENABLE_ONEWAY_SPAM_DETECTION: case ARTBOX_BINDER_GET_EXTENDED_ERROR:
        return -95;
    default:
        return -22;
    }
}
