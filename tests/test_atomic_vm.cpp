#include "artbox/native_atomic.h"
#include "artbox/native_vm.h"
#include <cstdio>
#include <cstdlib>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
int main() {
    artbox_vm_ops memory = artbox_native_vm();
    artbox_atomic_u32_ops atomic = artbox_native_atomic_u32();
    artbox_vm *vm = artbox_vm_create(&memory, memory.page_size, 1);
    CHECK(vm);
    int64_t mapping = artbox_vm_mmap(vm, 0, memory.page_size, 3, 0x22, -1, 0);
    CHECK(mapping > 0);
    uint64_t address = static_cast<uint64_t>(mapping);
    uint32_t value = 99;
    CHECK(artbox_vm_load_u32(vm, address, &atomic, &value) == 0 && value == 0);
    CHECK(artbox_vm_store_u32(vm, address, &atomic, 0xffffffff) == 0);
    CHECK(artbox_vm_load_u32(vm, address, &atomic, &value) == 0 && value == 0xffffffff);
    CHECK(artbox_vm_load_u32(vm, address + 1, &atomic, &value) == -22);
    CHECK(artbox_vm_store_u32(vm, address + 1, &atomic, 1) == -22);
    CHECK(artbox_vm_load_u32(vm, 0, &atomic, &value) == -14);
    CHECK(artbox_vm_store_u32(vm, address, &atomic, 0) == 0);
    auto *word = reinterpret_cast<void*>(static_cast<uintptr_t>(address));
    unsigned payload = 0;
    std::thread writer([&] { payload = 42; atomic.store_release(word, 1); });
    do { CHECK(artbox_vm_load_u32(vm, address, &atomic, &value) == 0); } while (!value);
    CHECK(payload == 42); // Publication through the native ABI acquire/release pair.
    writer.join();
    CHECK(artbox_vm_mprotect(vm, address, memory.page_size, 1) == 0);
    CHECK(artbox_vm_load_u32(vm, address, &atomic, &value) == 0 && value == 1);
    CHECK(artbox_vm_store_u32(vm, address, &atomic, 2) == -14);
    CHECK(artbox_vm_mprotect(vm, address, memory.page_size, 0) == 0);
    CHECK(artbox_vm_load_u32(vm, address, &atomic, &value) == -14);
    CHECK(artbox_vm_munmap(vm, address, memory.page_size) == 0);
    CHECK(artbox_vm_store_u32(vm, address, &atomic, 0) == -14);
    CHECK(artbox_vm_destroy(vm) == 0);
    std::puts("Atomic VM words: native publication, read-only load and inaccessible-word rejection passed");
    return 0;
}
