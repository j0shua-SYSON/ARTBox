#include "artbox/devices.h"
#include "artbox/native_system.h"
#include "artbox/native_vm.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#include <atomic>
#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

template<size_t N> static void path_text(unsigned char* destination, const char (&text)[N]) {
    static_assert(N <= 128, "Path fixture fits its mapped page");
    std::memcpy(destination, text, N);
}
static int failed_random(void*, size_t) { return -5; }
int main() {
    artbox_vm_ops memory = artbox_native_vm();
    artbox_system_ops system = artbox_native_system();
    artbox_vm *vm = artbox_vm_create(&memory, memory.page_size * 4, 4);
    artbox_devices *devices = artbox_devices_create(8);
    artbox_kernel_thread thread;
    CHECK(vm && devices && artbox_kernel_thread_init(&thread, vm, &system, 100, 100) == 0);
    int64_t map = artbox_vm_mmap(vm, 0, memory.page_size * 3, 3, 0x22, -1, 0);
    CHECK(map > 0);
    auto *bytes = reinterpret_cast<unsigned char*>(static_cast<uintptr_t>(map));
    uint64_t path = static_cast<uint64_t>(map), buffer = path + memory.page_size;
    auto call = [&](uint64_t n, uint64_t a0 = 0, uint64_t a1 = 0, uint64_t a2 = 0, uint64_t a3 = 0) {
        return artbox_devices_call(devices, &thread, n, a0, a1, a2, a3);
    };
    path_text(bytes, "/dev/urandom");
    CHECK(call(48, static_cast<uint32_t>(-100), path, 4) == 0);
    CHECK(call(48, static_cast<uint32_t>(-100), path, 1) == -13);
    CHECK(call(48, static_cast<uint32_t>(-100), path, 8) == -22);
    CHECK(call(48, static_cast<uint32_t>(-100), 0, 0) == -14);
    CHECK(call(56, static_cast<uint32_t>(-100), path, 0x4000) == -20);
    CHECK(call(56, static_cast<uint32_t>(-100), path, 0x80 | 0x40) == -17);
    CHECK(call(56, static_cast<uint32_t>(-100), path, 1) == -13);
    int64_t random = call(56, static_cast<uint32_t>(-100), path, 0x80800);
    CHECK(random == 3);
    std::memset(bytes + memory.page_size, 0xa5, memory.page_size * 2);
    CHECK(call(63, static_cast<uint64_t>(random), buffer + 1, 512) == 512);
    CHECK(bytes[memory.page_size] == 0xa5 && bytes[memory.page_size + 513] == 0xa5);
    CHECK(call(63, static_cast<uint64_t>(random), 0, 0) == 0);
    CHECK(call(63, static_cast<uint64_t>(random), 0, 16) == -14);
    CHECK(call(64, static_cast<uint64_t>(random), buffer, 16) == -9);
    CHECK(call(62, static_cast<uint64_t>(random), 100, 0) == 0);
    CHECK(call(80, static_cast<uint64_t>(random), buffer) == 0);
    uint32_t mode; uint64_t rdev;
    std::memcpy(&mode, bytes + memory.page_size + 16, 4);
    std::memcpy(&rdev, bytes + memory.page_size + 32, 8);
    CHECK((mode & 0170000) == 0020000 && rdev == 0x109);
    CHECK(artbox_vm_mprotect(vm, buffer + memory.page_size, memory.page_size, 0) == 0);
    CHECK(call(63, static_cast<uint64_t>(random), buffer + memory.page_size - 17, 34) == 17);
    CHECK(artbox_vm_mprotect(vm, buffer + memory.page_size, memory.page_size, 3) == 0);
    thread.system.random = failed_random;
    CHECK(call(63, static_cast<uint64_t>(random), buffer, 16) == -5);
    thread.system = system;
    CHECK(call(57, static_cast<uint64_t>(random)) == 0 && call(57, static_cast<uint64_t>(random)) == -9);
    CHECK(call(63, static_cast<uint64_t>(random), buffer, 16) == -9);
    path_text(bytes, "/dev/null");
    int64_t null_fd = call(56, 12345, path, 2); // Absolute paths ignore dirfd.
    CHECK(null_fd == 3 && call(63, 3, 0, 16) == 0 && call(64, 3, 0, 16) == 16);
    CHECK(call(57, 3) == 0);
    path_text(bytes, "/dev/zero");
    CHECK(call(56, 12345, path, 0) == 3);
    CHECK(call(63, 3, buffer, 512) == 512);
    for (size_t i = 0; i < 512; ++i) CHECK(bytes[memory.page_size + i] == 0);
    CHECK(call(57, 3) == 0);
    for (unsigned i = 0; i < 8; ++i) CHECK(call(56, 0, path, 0) == static_cast<int64_t>(i + 3));
    CHECK(call(56, 0, path, 0) == -24);
    for (unsigned i = 3; i < 11; ++i) CHECK(call(57, i) == 0);
    std::atomic<unsigned> failures{0};
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 8; ++i) workers.emplace_back([&] {
        for (unsigned j = 0; j < 128; ++j) {
            int64_t fd = call(56, 0, path, 0);
            if (fd < 3 || call(57, static_cast<uint64_t>(fd))) ++failures;
        }
    });
    for (auto &worker : workers) worker.join();
    CHECK(failures == 0);
    path_text(bytes, "dev/./urandom");
    CHECK(call(48, static_cast<uint32_t>(-100), path, 4) == 0);
    CHECK(call(48, 3, path, 4) == -9);
    path_text(bytes, "/dev/../urandom");
    CHECK(call(48, 0, path, 4) == -1);
    path_text(bytes, "/dev/missing");
    CHECK(call(56, 0, path, 0) == -2);
    path_text(bytes, "/dev/zero/");
    CHECK(call(56, 0, path, 0) == -20);
#if defined(__linux__)
    for (const char *name : {"/dev/null", "/dev/zero", "/dev/urandom"}) {
        int fd = openat(12345, name, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        CHECK(fd >= 0 && faccessat(AT_FDCWD, name, R_OK, 0) == 0);
        struct stat st{};
        CHECK(fstat(fd, &st) == 0 && S_ISCHR(st.st_mode) && major(st.st_rdev) == 1);
        CHECK(lseek(fd, 100, SEEK_SET) == 0);
        ssize_t count = read(fd, bytes + memory.page_size, 512);
        CHECK(count == (std::strcmp(name, "/dev/null") ? 512 : 0));
        CHECK(close(fd) == 0);
    }
    CHECK(open("/dev/zero", O_RDONLY | O_DIRECTORY) == -1 && errno == ENOTDIR);
    CHECK(open("/dev/zero", O_RDONLY | O_CREAT | O_EXCL, 0600) == -1 && errno == EEXIST);
    int null_write = open("/dev/null", O_WRONLY);
    CHECK(null_write >= 0 && syscall(SYS_write, null_write, 0, 16) == 16 && close(null_write) == 0);
#endif
    artbox_devices_destroy(devices);
    CHECK(artbox_vm_destroy(vm) == 0);
    std::puts("Virtual devices: entropy, zero/null, descriptor reuse, errors and 1024 concurrent lifecycles passed");
    return 0;
}
