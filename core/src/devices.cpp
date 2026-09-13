#include "artbox/devices.h"
#include <cstring>
#include <mutex>
#include <new>
#include <vector>

struct descriptor { unsigned kind = 0, flags = 0; };
struct artbox_devices { std::mutex lock; std::vector<descriptor> descriptors; };

extern "C" artbox_devices *artbox_devices_create(size_t limit) {
    if (!limit || limit > 4096) return nullptr;
    artbox_devices *devices = new (std::nothrow) artbox_devices;
    if (!devices) return nullptr;
    try { devices->descriptors.resize(limit); }
    catch (const std::bad_alloc&) { delete devices; return nullptr; }
    return devices;
}
extern "C" void artbox_devices_destroy(artbox_devices *devices) { delete devices; }

static int device_path(artbox_vm *vm, uint64_t address, int32_t dirfd, unsigned *kind) {
    char path[4096];
    size_t size = 0;
    for (; size < sizeof(path); ++size) {
        if (address > UINT64_MAX - size || artbox_vm_read(vm, address + size, path + size, 1)) return -14;
        if (!path[size]) break;
    }
    if (size == sizeof(path)) return -36;
    if (!size) return -2;
    if (path[0] != '/' && dirfd != -100) return -9;
    // The initial virtual cwd is '/'. Normalize separators and '.'; parent
    // traversal is an explicit confinement error, never a host path lookup.
    char normalized[4096];
    size_t used = 0, cursor = 0;
    while (cursor < size) {
        while (cursor < size && path[cursor] == '/') ++cursor;
        size_t begin = cursor;
        while (cursor < size && path[cursor] != '/') ++cursor;
        size_t length = cursor - begin;
        if (!length || (length == 1 && path[begin] == '.')) continue;
        if (length == 2 && path[begin] == '.' && path[begin + 1] == '.') return -1;
        if (used && used < sizeof(normalized)) normalized[used++] = '/';
        if (length >= sizeof(normalized) - used) return -36;
        std::memcpy(normalized + used, path + begin, length); used += length;
    }
    normalized[used] = 0;
    *kind = !std::strcmp(normalized, "dev/null") ? 1u : !std::strcmp(normalized, "dev/zero") ? 2u :
            !std::strcmp(normalized, "dev/urandom") ? 3u : 0u;
    if (!*kind) return std::strncmp(normalized, "dev/", 4) ? -38 : -2;
    if (path[size - 1] == '/') return -20;
    return 0;
}
static void put(unsigned char *out, uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) out[i] = static_cast<unsigned char>(value >> (8 * i));
}
extern "C" int64_t artbox_devices_call(artbox_devices *devices, artbox_kernel_thread *thread,
                                        uint64_t number, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    (void)a3;
    if (!devices || !thread || !thread->vm || !thread->system.random) return -22;
    if (number != 48 && number != 56 && number != 57 && number != 62 && number != 63 && number != 64 && number != 80)
        return -38;
    std::lock_guard<std::mutex> guard(devices->lock);
    if (number == 48 || number == 56) {
        unsigned kind = 0, flags = static_cast<uint32_t>(a2);
        if (number == 48 && (flags & ~7u)) return -22;
        int error = device_path(thread->vm, a1, static_cast<int32_t>(a0), &kind);
        if (error) return error;
        if (number == 48) return (flags & 1u) || (kind == 3 && (flags & 2u)) ? -13 : 0;
        if ((flags & 0xc0u) == 0xc0u) return -17;
        if (flags & 0x10000u) return -20;
        if ((flags & 3u) == 3u) return -22;
        if (flags & ~(3u | 0x40u | 0x80u | 0x100u | 0x200u | 0x400u | 0x800u | 0x8000u | 0x20000u | 0x80000u)) return -95;
        if (kind == 3 && (flags & 3u)) return -13; // Deliberately read-only virtual entropy source.
        for (size_t i = 0; i < devices->descriptors.size(); ++i) if (!devices->descriptors[i].kind) {
            devices->descriptors[i].kind = kind;
            devices->descriptors[i].flags = flags;
            return static_cast<int64_t>(i + 3);
        }
        return -24;
    }
    int32_t fd = static_cast<int32_t>(a0);
    if (fd < 3 || static_cast<size_t>(fd - 3) >= devices->descriptors.size()) return -9;
    descriptor &file = devices->descriptors[static_cast<size_t>(fd - 3)];
    if (!file.kind) return -9;
    if (number == 57) { file = descriptor{}; return 0; }
    if (number == 62) return static_cast<uint32_t>(a2) <= 4 ? 0 : -22;
    if (number == 80) {
        // AArch64 Linux stat is 128 bytes; no native struct stat crosses ABI.
        unsigned char bytes[128] = {};
        put(bytes, 1, 8); put(bytes + 8, file.kind, 8);
        put(bytes + 16, 0020000u | (file.kind == 3 ? 0444u : 0666u), 4);
        put(bytes + 20, 1, 4);
        put(bytes + 32, 0x100u + (file.kind == 1 ? 3u : file.kind == 2 ? 5u : 9u), 8);
        put(bytes + 56, 4096, 4);
        return artbox_vm_write(thread->vm, a1, bytes, sizeof(bytes));
    }
    bool writing = number == 64;
    unsigned access = file.flags & 3u;
    if ((writing && !access) || (!writing && access == 1)) return -9;
    size_t page = artbox_vm_page_size(thread->vm);
    uint64_t maximum = static_cast<uint64_t>(INT32_MAX) & ~(static_cast<uint64_t>(page) - 1);
    uint64_t count = a2 > maximum ? maximum : a2;
    // Linux's null/zero write handlers consume a count without reading memory.
    if (writing) return static_cast<int64_t>(count);
    if (file.kind == 1) return 0;
    uint64_t done = 0;
    while (done < count) {
        if (a1 > UINT64_MAX - done) return done ? static_cast<int64_t>(done) : -14;
        unsigned char bytes[256] = {};
        uint64_t address = a1 + done;
        size_t chunk = count - done > sizeof(bytes) ? sizeof(bytes) : static_cast<size_t>(count - done);
        size_t remaining_page = page - static_cast<size_t>(address % page);
        if (chunk > remaining_page) chunk = remaining_page;
        int error = file.kind == 3 ? thread->system.random(bytes, chunk) : 0;
        if (!error) error = artbox_vm_write(thread->vm, address, bytes, chunk);
        if (error) return done ? static_cast<int64_t>(done) : error < 0 ? error : -5;
        done += chunk;
    }
    return static_cast<int64_t>(done);
}
