#include "artbox/vm.h"
#include <algorithm>
#include <exception>
#include <cstring>
#include <mutex>
#include <memory>
#include <new>
#include <vector>

namespace {
constexpr unsigned mapped = 0x80;
constexpr unsigned file_page = 0x40, deny_write = 0x20;
struct FileMapping {
    artbox_vm_file_ops ops{};
    void *reference = nullptr;
    uint64_t offset = 0;
    unsigned sharing = 0;
    ~FileMapping() { if (reference) ops.release(reference); }
};
struct Region {
    uintptr_t base = 0;
    size_t length = 0;
    size_t live = 0;
    bool owned = true;
    bool protectable = true;
    std::vector<unsigned char> pages;
    std::shared_ptr<FileMapping> file;
};
int protection(uint64_t value) {
    if (value & ~UINT64_C(7)) return -22;
    if (value & 4) return -1;
    // The target is Linux AArch64: writable user pages are also readable.
    return value == 2 ? 3 : static_cast<int>(value);
}
bool overlaps(uint64_t a, uint64_t a_size, uint64_t b, uint64_t b_size) {
    return a < b + b_size && b < a + a_size;
}
}

struct artbox_vm {
    artbox_vm_ops ops;
    uint64_t limit = 0, reserved = 0;
    size_t region_limit = 0;
    bool poisoned = false;
    std::mutex lock;
    std::vector<Region> regions;

    size_t rounded(uint64_t length) const {
        size_t mask = ops.page_size - 1;
        if (!length || length > SIZE_MAX - mask) return 0;
        return static_cast<size_t>((length + mask) & ~static_cast<uint64_t>(mask));
    }
    bool range(uint64_t address, size_t length) const {
        return address <= UINTPTR_MAX && !(address & (ops.page_size - 1)) && length <= UINTPTR_MAX - address;
    }
    Region *containing(uint64_t address, size_t length) {
        for (auto &region : regions)
            if (address >= region.base && address - region.base <= region.length &&
                length <= region.length - (address - region.base)) return &region;
        return nullptr;
    }
    bool present(const Region &region, size_t first, size_t count) const {
        for (size_t n = first; n < first + count; ++n)
            if (!(region.pages[n] & mapped)) return false;
        return true;
    }
    bool accessible(uint64_t address, size_t length, unsigned required) {
        if (poisoned) return false;
        if (!length) return true;
        Region *region = containing(address, length);
        if (!region) return false;
        size_t first = static_cast<size_t>(address - region->base) / ops.page_size;
        size_t last = static_cast<size_t>(address - region->base + length - 1) / ops.page_size;
        for (size_t n = first; n <= last; ++n)
            if ((region->pages[n] & (mapped | required)) != (mapped | required)) return false;
        return true;
    }
    // A failed native mutation may have changed some pages. Stop subsequent
    // use rather than letting stale metadata authorize access to them.
    int mutation(int result) {
        if (result) poisoned = true;
        return result;
    }
};

artbox_vm *artbox_vm_create(const artbox_vm_ops *ops, uint64_t limit, size_t region_limit) {
    if (!ops || !ops->reserve || !ops->protect || !ops->reset || !ops->release || !region_limit ||
        ops->page_size < 4096 || ops->page_size > 65536 || (ops->page_size & (ops->page_size - 1)) ||
        limit < ops->page_size || limit > SIZE_MAX) return nullptr;
    artbox_vm *space = new (std::nothrow) artbox_vm;
    if (!space) return nullptr;
    space->ops = *ops;
    space->limit = limit;
    space->region_limit = region_limit;
    try { space->regions.reserve(region_limit); }
    catch (const std::exception &) { delete space; return nullptr; }
    return space;
}

int artbox_vm_destroy(artbox_vm *space) {
    if (!space) return -22;
    int result = 0;
    for (const auto &region : space->regions)
        if (region.owned && space->ops.release(reinterpret_cast<void *>(region.base), region.length)) result = -5;
    delete space;
    return result;
}

int64_t artbox_vm_mmap(artbox_vm *space, uint64_t address, uint64_t length, uint64_t prot,
                      uint64_t flags, int64_t fd, uint64_t offset) {
    if (!space) return -22;
    std::lock_guard<std::mutex> guard(space->lock);
    if (space->poisoned) return -5;
    size_t size = space->rounded(length);
    if (!size || (offset & (space->ops.page_size - 1))) return -22;
    int permissions = protection(prot);
    if (permissions < 0) return permissions;
    if ((flags & ~UINT64_C(0x4010)) != 0x22) return -95;
    (void)fd; // Ignored for MAP_ANONYMOUS, as on Linux.
    if (flags & 0x10) {
        if (!space->range(address, size)) return -22;
        Region *region = space->containing(address, size);
        if (!region) return -95;
        if (!region->owned) return -1;
        int result = space->mutation(space->ops.reset(reinterpret_cast<void *>(address), size, static_cast<unsigned>(permissions)));
        if (result) return result;
        size_t first = static_cast<size_t>(address - region->base) / space->ops.page_size;
        for (size_t n = first; n < first + size / space->ops.page_size; ++n) {
            if (!(region->pages[n] & mapped)) ++region->live;
            region->pages[n] = static_cast<unsigned char>(mapped | static_cast<unsigned>(permissions));
        }
        return static_cast<int64_t>(address);
    }
    // An ordinary address argument is a hint; the host may choose another VA.
    if (space->regions.size() == space->region_limit || size > space->limit - space->reserved) return -12;
    Region region;
    region.length = size;
    region.live = size / space->ops.page_size;
    try { region.pages.assign(region.live, static_cast<unsigned char>(mapped | static_cast<unsigned>(permissions))); }
    catch (const std::exception &) { return -12; }
    void *storage = nullptr;
    int result = space->ops.reserve(size, &storage);
    if (result) return result;
    region.base = reinterpret_cast<uintptr_t>(storage);
    if (!region.base || region.base > INT64_MAX || !space->range(region.base, size)) {
        if (storage) space->ops.release(storage, size);
        return -12;
    }
    if (permissions && (result = space->ops.protect(storage, size, static_cast<unsigned>(permissions)))) {
        if (space->ops.release(storage, size)) {
            // Keep failed rollback storage owned so destruction can retry.
            space->poisoned = true;
            space->reserved += size;
            space->regions.push_back(std::move(region));
        }
        return result;
    }
    space->reserved += size;
    space->regions.push_back(std::move(region));
    return static_cast<int64_t>(reinterpret_cast<uintptr_t>(storage));
}

int64_t artbox_vm_map_file(artbox_vm *space, uint64_t address, uint64_t length,
    uint64_t prot, uint64_t flags, uint64_t offset, void *file,
    const artbox_vm_file_ops *ops, unsigned maximum) {
    if (!space || !file || !ops || !ops->acquire || !ops->release || !ops->map || !ops->sync ||
        (maximum != 1 && maximum != 3)) return -22;
    std::lock_guard<std::mutex> guard(space->lock);
    if (space->poisoned) return -5;
    size_t size = space->rounded(length);
    if (!size || (offset & (space->ops.page_size - 1))) return -22;
    if (size > static_cast<uint64_t>(INT64_MAX) || offset > static_cast<uint64_t>(INT64_MAX) - size) return -75;
    int permissions = protection(prot);
    if (permissions < 0) return permissions;
    unsigned sharing = static_cast<unsigned>(flags & 0xf);
    if (sharing != 1 && sharing != 2) return -22;
    if (flags & ~UINT64_C(0x4003)) return -95;
    if (static_cast<unsigned>(permissions) & ~maximum) return -13;
    (void)address; // Ordinary hints may be ignored, as with anonymous storage.
    if (space->regions.size() == space->region_limit || size > space->limit - space->reserved) return -12;
    Region region;
    region.length = size; region.live = size / space->ops.page_size;
    try {
        region.pages.assign(region.live, static_cast<unsigned char>(mapped | file_page |
            (maximum == 1 ? deny_write : 0) | static_cast<unsigned>(permissions)));
        region.file = std::make_shared<FileMapping>();
    } catch (const std::exception &) { return -12; }
    region.file->ops = *ops; region.file->offset = offset; region.file->sharing = sharing;
    int error = ops->acquire(file, &region.file->reference);
    if (error) return error;
    if (!region.file->reference) return -5;
    void *storage = nullptr;
    error = space->ops.reserve(size, &storage);
    if (error) return error;
    region.base = reinterpret_cast<uintptr_t>(storage);
    if (!region.base || region.base > INT64_MAX || !space->range(region.base, size)) {
        if (storage) space->ops.release(storage, size);
        return -12;
    }
    error = ops->map(region.file->reference, storage, size, static_cast<unsigned>(permissions), sharing, offset);
    if (error) {
        if (space->ops.release(storage, size)) {
            space->poisoned = true;
            space->reserved += size;
            space->regions.push_back(std::move(region));
        }
        return error;
    }
    space->reserved += size;
    space->regions.push_back(std::move(region));
    return static_cast<int64_t>(reinterpret_cast<uintptr_t>(storage));
}

int artbox_vm_mprotect(artbox_vm *space, uint64_t address, uint64_t length, uint64_t prot) {
    if (!space) return -22;
    std::lock_guard<std::mutex> guard(space->lock);
    if (space->poisoned) return -5;
    int permissions = protection(prot);
    if (permissions < 0) return permissions;
    size_t size = space->rounded(length);
    if (!space->range(address, size) || (!size && length)) return -22;
    if (!length) return 0;
    Region *region = space->containing(address, size);
    if (!region) return -12;
    if (!region->protectable) return -1;
    size_t first = static_cast<size_t>(address - region->base) / space->ops.page_size;
    size_t count = size / space->ops.page_size;
    if (!space->present(*region, first, count)) return -12;
    if (permissions & 2)
        for (size_t n = first; n < first + count; ++n)
            if (region->pages[n] & deny_write) return -13;
    int result = space->mutation(space->ops.protect(reinterpret_cast<void *>(address), size, static_cast<unsigned>(permissions)));
    if (!result)
        for (size_t n = first; n < first + count; ++n)
            region->pages[n] = static_cast<unsigned char>((region->pages[n] & ~3u) | static_cast<unsigned>(permissions));
    return result;
}

int artbox_vm_munmap(artbox_vm *space, uint64_t address, uint64_t length) {
    if (!space) return -22;
    std::lock_guard<std::mutex> guard(space->lock);
    if (space->poisoned) return -5;
    size_t size = space->rounded(length);
    if (!size || !space->range(address, size)) return -22;
    for (const auto &region : space->regions)
        if (!region.owned && overlaps(address, size, region.base, region.length)) return -1;
    for (size_t i = 0; i < space->regions.size();) {
        Region &region = space->regions[i];
        if (!overlaps(address, size, region.base, region.length)) { ++i; continue; }
        uint64_t begin = std::max<uint64_t>(address, region.base);
        uint64_t end = std::min<uint64_t>(address + size, region.base + region.length);
        size_t first = static_cast<size_t>(begin - region.base) / space->ops.page_size;
        size_t count = static_cast<size_t>(end - begin) / space->ops.page_size;
        size_t removed = 0;
        for (size_t n = first; n < first + count; ++n) removed += !!(region.pages[n] & mapped);
        if (removed == region.live) {
            int result = space->mutation(space->ops.release(reinterpret_cast<void *>(region.base), region.length));
            if (result) return result;
            space->reserved -= region.length;
            space->regions.erase(space->regions.begin() + static_cast<ptrdiff_t>(i));
            continue;
        }
        if (removed) {
            int result = space->mutation(space->ops.reset(reinterpret_cast<void *>(begin), static_cast<size_t>(end - begin), 0));
            if (result) return result;
            std::fill_n(region.pages.begin() + static_cast<ptrdiff_t>(first), count, static_cast<unsigned char>(0));
            region.live -= removed;
        }
        ++i;
    }
    return 0;
}

int artbox_vm_madvise(artbox_vm *space, uint64_t address, uint64_t length, int advice) {
    if (!space) return -22;
    std::lock_guard<std::mutex> guard(space->lock);
    if (space->poisoned) return -5;
    if (advice != 0 && advice != 4) return -95;
    size_t size = space->rounded(length);
    if (!space->range(address, size) || (!size && length)) return -22;
    if (!length) return 0;
    Region *region = space->containing(address, size);
    if (!region) return -12;
    if (!region->owned) return -1;
    size_t first = static_cast<size_t>(address - region->base) / space->ops.page_size;
    size_t count = size / space->ops.page_size;
    if (!space->present(*region, first, count)) return -12;
    if (!advice) return 0;
    for (size_t n = first; n < first + count;) {
        size_t end = n + 1;
        while (end < first + count && region->pages[end] == region->pages[n]) ++end;
        void *start = reinterpret_cast<void *>(region->base + n * space->ops.page_size);
        size_t bytes = (end - n) * space->ops.page_size;
        int result;
        if (region->pages[n] & file_page) {
            const auto &f = *region->file;
            result = f.ops.map(f.reference, start, bytes, region->pages[n] & 3, f.sharing,
                               f.offset + n * space->ops.page_size);
        } else result = space->ops.reset(start, bytes, region->pages[n] & 3);
        result = space->mutation(result);
        if (result) return result;
        n = end;
    }
    return 0;
}

int artbox_vm_msync(artbox_vm *space, uint64_t address, uint64_t length, uint64_t flags) {
    if (!space) return -22;
    std::lock_guard<std::mutex> guard(space->lock);
    if (space->poisoned) return -5;
    size_t size = space->rounded(length);
    if ((flags & ~UINT64_C(7)) || (flags & 5) == 5 || !space->range(address, size) || (!size && length)) return -22;
    if (!length) return 0;
    Region *region = space->containing(address, size);
    if (!region) return -12;
    size_t first = static_cast<size_t>(address - region->base) / space->ops.page_size;
    size_t count = size / space->ops.page_size;
    if (!space->present(*region, first, count)) return -12;
    // Linux MS_ASYNC (including flags=0) does not initiate writeback. PRIVATE
    // pages never propagate their copy-on-write changes into the backing file.
    if (!(flags & 4) || !region->file || region->file->sharing != 1) return 0;
    for (size_t n = first; n < first + count;) {
        if (!(region->pages[n] & file_page)) { ++n; continue; }
        size_t end = n + 1;
        while (end < first + count && (region->pages[end] & file_page)) ++end;
        int error = region->file->ops.sync(region->file->reference,
            reinterpret_cast<void*>(region->base + n * space->ops.page_size),
            (end - n) * space->ops.page_size, static_cast<unsigned>(flags));
        if (error) return error;
        n = end;
    }
    return 0;
}

int artbox_vm_register_data(artbox_vm *space, void *address, size_t length, unsigned prot) {
    if (!space || !address) return -22;
    std::lock_guard<std::mutex> guard(space->lock);
    if (space->poisoned) return -5;
    int permissions = protection(prot);
    if (permissions < 0) return permissions;
    uintptr_t base = reinterpret_cast<uintptr_t>(address);
    if (!length || length % space->ops.page_size || !space->range(base, length)) return -22;
    for (const auto &region : space->regions)
        if (overlaps(base, length, region.base, region.length)) return -17;
    if (space->regions.size() == space->region_limit) return -12;
    Region region;
    region.base = base;
    region.length = length;
    region.live = length / space->ops.page_size;
    region.owned = false;
    try { region.pages.assign(region.live, static_cast<unsigned char>(mapped | static_cast<unsigned>(permissions))); }
    catch (const std::exception &) { return -12; }
    space->regions.push_back(std::move(region));
    return 0;
}

int artbox_vm_access(artbox_vm *space, uint64_t address, uint64_t length, unsigned required) {
    if (!space || (required & ~3u) || length > SIZE_MAX || length > UINT64_MAX - address) return 0;
    std::lock_guard<std::mutex> guard(space->lock);
    return space->accessible(address, static_cast<size_t>(length), required);
}

int artbox_vm_register_readonly(artbox_vm *space, const void *address, size_t length) {
    if (!space || !address || !length || length > UINTPTR_MAX - reinterpret_cast<uintptr_t>(address)) return -22;
    std::lock_guard<std::mutex> guard(space->lock);
    if (space->poisoned) return -5;
    uintptr_t base = reinterpret_cast<uintptr_t>(address);
    for (const auto &region : space->regions)
        if (overlaps(base, length, region.base, region.length)) return -17;
    if (space->regions.size() == space->region_limit) return -12;
    Region region;
    region.base = base;
    region.length = length;
    region.live = (length - 1) / space->ops.page_size + 1;
    region.owned = false;
    region.protectable = false;
    try { region.pages.assign(region.live, static_cast<unsigned char>(mapped | 1)); }
    catch (const std::exception &) { return -12; }
    space->regions.push_back(std::move(region));
    return 0;
}

int artbox_vm_read(artbox_vm *space, uint64_t address, void *destination, size_t length) {
    if (!space || (!destination && length)) return -22;
    std::lock_guard<std::mutex> guard(space->lock);
    if (space->poisoned) return -5;
    if (!space->accessible(address, length, 1)) return -14;
    if (length) std::memcpy(destination, reinterpret_cast<const void *>(address), length);
    return 0;
}

int artbox_vm_write(artbox_vm *space, uint64_t address, const void *source, size_t length) {
    if (!space || (!source && length)) return -22;
    std::lock_guard<std::mutex> guard(space->lock);
    if (space->poisoned) return -5;
    if (!space->accessible(address, length, 2)) return -14;
    if (length) std::memcpy(reinterpret_cast<void *>(address), source, length);
    return 0;
}

uint64_t artbox_vm_reserved_bytes(artbox_vm *space) {
    if (!space) return 0;
    std::lock_guard<std::mutex> guard(space->lock);
    return space->reserved;
}

int artbox_vm_load_u32(artbox_vm *space, uint64_t address,
                       const artbox_atomic_u32_ops *ops, uint32_t *value) {
    if (!space || !ops || !ops->load_acquire || !value || (address & 3)) return -22;
    std::lock_guard<std::mutex> guard(space->lock);
    if (space->poisoned) return -5;
    if (!space->accessible(address, 4, 1)) return -14;
    *value = ops->load_acquire(reinterpret_cast<const void*>(address));
    return 0;
}
int artbox_vm_store_u32(artbox_vm *space, uint64_t address,
                        const artbox_atomic_u32_ops *ops, uint32_t value) {
    if (!space || !ops || !ops->store_release || (address & 3)) return -22;
    std::lock_guard<std::mutex> guard(space->lock);
    if (space->poisoned) return -5;
    if (!space->accessible(address, 4, 2)) return -14;
    ops->store_release(reinterpret_cast<void*>(address), value);
    return 0;
}

int artbox_vm_prepare_store_u32(artbox_vm *space, uint64_t address,
    const artbox_atomic_u32_ops *ops, uint32_t value, int (*prepare)(void *), void *context) {
    if (!space || !ops || !ops->store_release || !prepare || (address & 3)) return -22;
    std::lock_guard<std::mutex> guard(space->lock);
    if (space->poisoned) return -5;
    if (!space->accessible(address, 4, 2)) return -14;
    int error = prepare(context);
    if (error) return error < 0 ? error : -5;
    ops->store_release(reinterpret_cast<void*>(address), value);
    return 0;
}

int64_t artbox_vm_transfer(artbox_vm *space, uint64_t address, size_t length, unsigned access,
    int64_t (*transfer)(void *context, void *buffer, size_t length), void *context) {
    if (!space || !transfer || (access != 1 && access != 2) || length > static_cast<uint64_t>(INT64_MAX)) return -22;
    std::lock_guard<std::mutex> guard(space->lock);
    if (space->poisoned) return -5;
    if (length && !space->accessible(address, length, access)) return -14;
    int64_t result = transfer(context, reinterpret_cast<void*>(address), length);
    return result > static_cast<int64_t>(length) ? -5 : result;
}

size_t artbox_vm_page_size(const artbox_vm *space) {
    return space ? space->ops.page_size : 0;
}

int64_t artbox_vm_syscall(void *context, uint64_t number, uint64_t a0, uint64_t a1,
                         uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    auto *space = static_cast<artbox_vm *>(context);
    switch (number) {
        case 222: return artbox_vm_mmap(space, a0, a1, a2, a3, static_cast<int64_t>(a4), a5);
        case 226: return artbox_vm_mprotect(space, a0, a1, a2);
        case 227: return artbox_vm_msync(space, a0, a1, a2);
        case 215: return artbox_vm_munmap(space, a0, a1);
        case 233: return artbox_vm_madvise(space, a0, a1, static_cast<int>(static_cast<uint32_t>(a2)));
        default: return -38;
    }
}
