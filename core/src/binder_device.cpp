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
    artbox_vm *vm = nullptr;
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
};

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
extern "C" int artbox_binder_device_destroy(artbox_binder_device *device) {
    if (!device) return -22;
    {
        std::lock_guard<std::mutex> guard(device->lock);
        for (const auto &endpoint : device->endpoints) if (endpoint.token) return -16;
    }
    delete device;
    return 0;
}
extern "C" int artbox_binder_device_open(artbox_binder_device *device, artbox_vm *vm,
    int32_t pid, uint32_t uid, uint64_t *token) {
    if (!device || !vm || pid <= 0 || !token) return -22;
    std::lock_guard<std::mutex> guard(device->lock);
    if (!device->next_token) return -24;
    for (auto &endpoint : device->endpoints) {
        if (endpoint.token) continue;
        endpoint.token = device->next_token++;
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
        if (endpoint.token != token) continue;
        endpoint.token = 0;
        endpoint.vm = nullptr;
        endpoint.threads.clear();
        if (device->manager == token) {
            device->manager = 0;
            std::memset(device->manager_object, 0, sizeof(device->manager_object));
        }
        return 0;
    }
    return -9;
}
extern "C" int64_t artbox_binder_device_ioctl(artbox_binder_device *device, uint64_t token,
    int32_t tid, uint32_t request, uint64_t argument) {
    if (!device || tid <= 0) return -22;
    if (!token) return -9;
    std::lock_guard<std::mutex> guard(device->lock);
    auto found = std::find_if(device->endpoints.begin(), device->endpoints.end(),
        [token](const Endpoint &e) { return e.token == token; });
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
