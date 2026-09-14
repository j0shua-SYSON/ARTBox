/* A child process catches only the expected hardware fault and exits directly,
 * avoiding crash-report/core files. Returning normally means a protection bug. */
#include "artbox/native_vm.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static uintptr_t fault_address;
static ULONG_PTR fault_write;
static LONG WINAPI fault_handler(EXCEPTION_POINTERS *info) {
    const EXCEPTION_RECORD *record = info->ExceptionRecord;
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2 &&
        record->ExceptionInformation[0] == fault_write && record->ExceptionInformation[1] == fault_address)
        ExitProcess(77);
    ExitProcess(78);
}
#else
#include <signal.h>
#include <unistd.h>
static void *fault_address;
static void fault_handler(int signal, siginfo_t *info, void *) {
    _exit((signal == SIGSEGV || signal == SIGBUS) && info->si_addr == fault_address ? 77 : 78);
}
#endif

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    bool write = !std::strcmp(argv[1], "readonly");
    bool hole = !std::strcmp(argv[1], "hole");
    bool none = !std::strcmp(argv[1], "none");
    if (!write && !hole && !none) return 2;
    artbox_vm_ops ops = artbox_native_vm();
    artbox_vm *vm = artbox_vm_create(&ops, ops.page_size * 3, 1);
    int64_t address = artbox_vm_mmap(vm, 0, ops.page_size * 3, 3, 0x22, -1, 0);
    if (address <= 0) return 2;
    uint64_t middle = static_cast<uint64_t>(address) + ops.page_size;
    if (hole ? artbox_vm_munmap(vm, middle, ops.page_size) :
               artbox_vm_mprotect(vm, middle, ops.page_size, write ? 1 : 0)) return 2;
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    fault_address = static_cast<uintptr_t>(middle);
    fault_write = write ? 1 : 0;
    if (!AddVectoredExceptionHandler(1, fault_handler)) return 2;
#else
    fault_address = reinterpret_cast<void *>(middle);
    struct sigaction action = {};
    action.sa_sigaction = fault_handler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGSEGV, &action, nullptr) || sigaction(SIGBUS, &action, nullptr)) return 2;
#endif
    std::puts("fault check armed");
    std::fflush(stdout);
    volatile unsigned char *bytes = reinterpret_cast<volatile unsigned char *>(middle);
    if (write) *bytes = 1;
    else if (*bytes == 0xff) return 3;
    return 1;
}
