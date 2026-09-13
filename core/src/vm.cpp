#include "artbox/vm.h"
#include <algorithm>
#include <exception>
#include <cstring>
#include <mutex>
#include <new>
#include <vector>

namespace {
constexpr unsigned mapped = 0x80;
struct Region {
    uintptr_t base = 0;
    size_t length = 0;
    size_t live = 0;
    bool owned = true;
    bool protectable = true;
    std::vector<unsigned char> pages;
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
    int result = space->mutation(space->ops.protect(reinterpret_cast<void *>(address), size, static_cast<unsigned>(permissions)));
    if (!result) std::fill_n(region->pages.begin() + static_cast<ptrdiff_t>(first), count,
                            static_cast<unsigned char>(mapped | static_cast<unsigned>(permissions)));
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
        int result = space->mutation(space->ops.reset(reinterpret_cast<void *>(region->base + n * space->ops.page_size),
                                   (end - n) * space->ops.page_size, region->pages[n] & 3));
        if (result) return result;
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

size_t artbox_vm_page_size(const artbox_vm *space) {
    return space ? space->ops.page_size : 0;
}

int64_t artbox_vm_syscall(void *context, uint64_t number, uint64_t a0, uint64_t a1,
                         uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    auto *space = static_cast<artbox_vm *>(context);
    switch (number) {
        case 222: return artbox_vm_mmap(space, a0, a1, a2, a3, static_cast<int64_t>(a4), a5);
        case 226: return artbox_vm_mprotect(space, a0, a1, a2);
        case 215: return artbox_vm_munmap(space, a0, a1);
        case 233: return artbox_vm_madvise(space, a0, a1, static_cast<int>(static_cast<uint32_t>(a2)));
        default: return -38;
    }
}
