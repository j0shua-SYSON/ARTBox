// Exercise ART's actual feature probe with deterministic syscall failures.
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

constexpr int PROT_READ = 1, PROT_WRITE = 2, MAP_ANONYMOUS = 32, MAP_SHARED = 1;
constexpr int MREMAP_MAYMOVE = 1, MREMAP_DONTUNMAP = 4;
#define MAP_FAILED reinterpret_cast<void*>(-1)
#define CHECK_NE(a, b) do { if ((a) == (b)) std::_Exit(90); } while (0)
#define CHECK_EQ(a, b) do { if ((a) != (b)) std::_Exit(90); } while (0)

static unsigned calls;
static const char* scenario;
static void* const old_address = reinterpret_cast<void*>(static_cast<uintptr_t>(0x10000));
static void* const new_address = reinterpret_cast<void*>(static_cast<uintptr_t>(0x20000));
static size_t GetPageSizeSlow() { return 16384; }
static void require(bool value) { if (!value) std::_Exit(91); }
static bool is(const char* value) { return std::strcmp(scenario, value) == 0; }

static void* mmap(void* address, size_t size, int prot, int flags, int fd, int offset) {
    require(++calls == 1 && !address && size == 16384 && prot == 3 && flags == 33 && fd == -1 && !offset);
    if (is("map-unsupported") || is("map-unimplemented") || is("map-no-memory")) {
        errno = is("map-unsupported") ? EOPNOTSUPP : is("map-unimplemented") ? ENOSYS : ENOMEM;
        return MAP_FAILED;
    }
    return old_address;
}
static void* mremap(void* address, size_t old_size, size_t new_size, int flags, void* target) {
    require(++calls == 2 && address == old_address && old_size == 16384 && new_size == 16384 && flags == 5 && !target);
    if (is("remap-unsupported") || is("remap-no-memory")) {
        errno = is("remap-unsupported") ? ENOSYS : ENOMEM;
        return MAP_FAILED;
    }
    return new_address;
}
static int munmap(void* address, size_t size) {
    ++calls;
    require(size == 16384 && ((calls == 3 && address == old_address) || (calls == 4 && address == new_address)));
    return (calls == 3 && is("old-unmap-fails")) || (calls == 4 && is("new-unmap-fails")) ? -1 : 0;
}

// Generated from the hash-verified AOSP file, retaining its license header.
#include "mremap_probe.inc"

int main(int argc, char** argv) {
    require(argc == 2);
    scenario = argv[1];
    bool supported = HaveMremapDontunmap();
    require(supported == is("success"));
    require(calls == (is("success") ? 4u : is("map-unsupported") || is("map-unimplemented") ? 1u : 3u));
    std::puts(supported ? "supported" : "unsupported");
    return 0;
}
