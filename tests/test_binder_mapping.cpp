// Original native receive-alias contract. SPDX-License-Identifier: MIT
#include "artbox/binder_arena.h"
#include "artbox/native_files.h"
#include "artbox/native_vm.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static uintptr_t expected_fault;
static LONG WINAPI fault_handler(EXCEPTION_POINTERS *info) {
    const EXCEPTION_RECORD *record = info->ExceptionRecord;
    ExitProcess(record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2 &&
        record->ExceptionInformation[0] == 1 && record->ExceptionInformation[1] == expected_fault ? 77 : 78);
}
#else
#include <signal.h>
#include <unistd.h>
static void *expected_fault;
static void fault_handler(int signal, siginfo_t *info, void *) {
    _exit((signal == SIGSEGV || signal == SIGBUS) && info->si_addr == expected_fault ? 77 : 78);
}
#endif
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

int main(int argc, char **argv) {
    CHECK(argc == 3);
    const bool fault = !std::strcmp(argv[2], "write-fault");
    CHECK(fault || !std::strcmp(argv[2], "lifetime"));
    artbox_native_files *files = nullptr;
    int result = artbox_native_files_open(argv[1], &files);
    if (result == -95) {
        std::puts("Native file provider unavailable; receive-alias execution not verified");
        return 77;
    }
    CHECK(result == 0);
    const artbox_file_ops ops = artbox_native_files_ops(files);
    const artbox_vm_ops memory = artbox_native_vm();
    const size_t capacity = memory.page_size * 2;
    artbox_vm *guest = artbox_vm_create(&memory, capacity * 2, 4);
    artbox_vm *driver = artbox_vm_create(&memory, capacity * 2, 4);
    CHECK(guest && driver);
    void *file = nullptr;
    CHECK(ops.open(ops.context, nullptr, "receive", 0xc2, 0600, &file) == 0);
    CHECK(ops.unlink(ops.context, nullptr, "receive", 0) == 0);
    CHECK(ops.seek(file, static_cast<int64_t>(capacity - 1), 0) == static_cast<int64_t>(capacity - 1));
    const unsigned char zero = 0;
    CHECK(ops.write(file, &zero, 1) == 1);
    int64_t mapped = artbox_vm_map_file(driver, 0, capacity, 3, 1, 0, file, &ops.mapping, 3);
    CHECK(mapped > 0);
    auto *writable = reinterpret_cast<unsigned char *>(static_cast<uintptr_t>(mapped));
    mapped = artbox_vm_map_file(guest, 0, capacity, 1, 1, 0, file, &ops.mapping, 1);
    CHECK(mapped > 0);
    const uint64_t address = static_cast<uint64_t>(mapped);
    const auto *readonly = reinterpret_cast<const unsigned char *>(static_cast<uintptr_t>(mapped));
    CHECK(readonly != writable);
    CHECK(ops.close(file) == 0); // Both VM mappings own independent references.
    CHECK(artbox_native_files_close(files) == 0);
    CHECK(!artbox_vm_access(guest, reinterpret_cast<uintptr_t>(writable), capacity, 1));
    CHECK(!artbox_vm_access(driver, address, capacity, 1));
    CHECK(artbox_vm_mprotect(guest, address, capacity, 3) == -13);
    CHECK(artbox_vm_mprotect(guest, address, capacity, 5) == -1);
    CHECK(artbox_vm_write(guest, address, &zero, 1) == -14);
    artbox_binder_arena *arena = artbox_binder_arena_create(writable, address, capacity, 8);
    CHECK(arena);
    artbox_binder_buffer buffer;
    const unsigned char payload[] = "Binder shared receive bytes";
    CHECK(artbox_binder_arena_reserve(arena, sizeof(payload), 0, 0, 1, &buffer) == 0);
    CHECK(artbox_binder_arena_write(arena, buffer.address, 0, payload, sizeof(payload)) == 0);
    CHECK(artbox_binder_arena_publish(arena, buffer.address) == 0);
    CHECK(!std::memcmp(readonly, payload, sizeof(payload)));
    unsigned char captured[sizeof(payload)];
    CHECK(artbox_vm_read(guest, address, captured, sizeof(captured)) == 0);
    CHECK(!std::memcmp(captured, payload, sizeof(payload)));
    if (fault) {
#if defined(_WIN32)
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        expected_fault = static_cast<uintptr_t>(address);
        CHECK(AddVectoredExceptionHandler(1, fault_handler));
#else
        expected_fault = reinterpret_cast<void *>(static_cast<uintptr_t>(address));
        struct sigaction action = {};
        action.sa_sigaction = fault_handler; action.sa_flags = SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        CHECK(!sigaction(SIGSEGV, &action, nullptr) && !sigaction(SIGBUS, &action, nullptr));
#endif
        std::puts("receive alias write fault armed"); std::fflush(stdout);
        *reinterpret_cast<volatile unsigned char *>(static_cast<uintptr_t>(address)) = 0xff;
        return 1; // A successful raw store is a protection failure.
    }
    CHECK(artbox_binder_arena_release(arena, buffer.address) == 0);
    for (size_t i = 0; i < buffer.extent; ++i) CHECK(readonly[i] == 0);
    CHECK(artbox_binder_arena_destroy(arena) == 0);
    CHECK(artbox_vm_destroy(driver) == 0); // Guest mapping must survive driver-side close.
    CHECK(artbox_vm_read(guest, address, captured, sizeof(captured)) == 0);
    for (unsigned char value : captured) CHECK(value == 0);
    CHECK(artbox_vm_munmap(guest, address, capacity) == 0);
    CHECK(artbox_vm_read(guest, address, captured, 1) == -14);
    CHECK(artbox_vm_reserved_bytes(guest) == 0 && artbox_vm_destroy(guest) == 0);
    std::puts("Native Binder receive aliases: coherence, permission ceilings and independent mapping lifetimes pass");
    return 0;
}
