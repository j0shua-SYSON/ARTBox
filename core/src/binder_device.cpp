// Original user-mode Binder endpoint boundary. SPDX-License-Identifier: MIT
#include "artbox/binder_device.h"
#include <algorithm>
#include <cstring>
#include <exception>
#include <mutex>
#include <new>
#include <vector>

namespace {
enum { entered = 1, registered = 2, exited = 4, invalid = 8 };
struct Thread { int32_t tid; unsigned looper; };
struct Endpoint {
    uint64_t token = 0;
    bool opened = false, mapped_once = false;
    artbox_vm *vm = nullptr;
    artbox_vm *receive_owner = nullptr;
    artbox_vm_mapping_watch *receive_watch = nullptr;
    uint64_t receive_base = 0;
    size_t receive_size = 0;
    void *receive_writable = nullptr;
    int32_t pid = 0;
    uint32_t uid = 0, max_threads = 0;
    std::vector<Thread> threads;
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
// Only no-payload looper commands are enabled here. Recognition of other wire
// words does not imply that their payloads have been copied or handled.
int command(Endpoint &endpoint, Thread &thread, uint64_t address) {
    unsigned char bytes[76] = {};
    if (artbox_vm_read(endpoint.vm, address, bytes, 4)) return -14;
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
    default:
        artbox_binder_frame frame;
        size_t cursor = 0;
        return artbox_binder_next(ARTBOX_BINDER_WRITE, bytes, sizeof(bytes), &cursor, &frame)
            == ARTBOX_BINDER_OK ? -95 : -22;
    }
}
int write_read(Endpoint &endpoint, Thread &thread, uint64_t argument) {
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
        result = command(endpoint, thread, base + consumed);
        if (!result) consumed += 4;
    }
    write64(bytes + 8, consumed);
    if (result) write64(bytes + 32, 0); // A failed write never attempts the read.
    else if (read64(bytes + 24)) result = -95; // Receive queue is not enabled yet.
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
};

// The device lock is held. No guest mapping owns an Endpoint or calls here:
// watches publish passive state, avoiding a VM-to-device lock inversion.
static int release_endpoint(artbox_binder_device *device, Endpoint &endpoint) {
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
        endpoint.opened = true; endpoint.mapped_once = false;
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
        endpoint.threads.clear();
        return reap_closed(device);
    }
    return -9;
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
        endpoint.threads.push_back({tid, 0});
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
        endpoint.threads.erase(thread);
        return 0;
    case ARTBOX_BINDER_WRITE_READ:
        return write_read(endpoint, *thread, argument);
    case ARTBOX_BINDER_GET_NODE_DEBUG_INFO: case ARTBOX_BINDER_GET_NODE_INFO_FOR_REF:
    case ARTBOX_BINDER_FREEZE: case ARTBOX_BINDER_GET_FROZEN_INFO:
    case ARTBOX_BINDER_ENABLE_ONEWAY_SPAM_DETECTION: case ARTBOX_BINDER_GET_EXTENDED_ERROR:
        return -95;
    default:
        return -22;
    }
}
