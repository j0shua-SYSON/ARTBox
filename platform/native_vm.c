#include "artbox/native_vm.h"
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static DWORD permission(unsigned prot) {
    return prot == 0 ? PAGE_NOACCESS : prot == 1 ? PAGE_READONLY : PAGE_READWRITE;
}
static int reserve(size_t length, void **address) {
    *address = VirtualAlloc(NULL, length, MEM_RESERVE, PAGE_NOACCESS);
    return *address ? 0 : -12;
}
static int protect(void *address, size_t length, unsigned prot) {
    DWORD previous;
    if (prot) {
        if (!VirtualAlloc(address, length, MEM_COMMIT, permission(prot))) return -12;
        return VirtualProtect(address, length, permission(prot), &previous) ? 0 : -22;
    }
    // VirtualProtect cannot span uncommitted pages. Reserved pages are already
    // inaccessible; preserve existing committed data while protecting those runs.
    uintptr_t cursor = (uintptr_t)address, end = cursor + length;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info;
        if (!VirtualQuery((void *)cursor, &info, sizeof(info))) return -22;
        uintptr_t next = (uintptr_t)info.BaseAddress + info.RegionSize;
        if (next > end) next = end;
        if (next <= cursor || info.State == MEM_FREE) return -22;
        if (info.State == MEM_COMMIT && !VirtualProtect((void *)cursor, next - cursor, PAGE_NOACCESS, &previous)) return -22;
        cursor = next;
    }
    return 0;
}
static int reset(void *address, size_t length, unsigned prot) {
    if (!VirtualFree(address, length, MEM_DECOMMIT)) return -22;
    return prot ? protect(address, length, prot) : 0;
}
static int release(void *address, size_t length) {
    (void)length;
    return VirtualFree(address, 0, MEM_RELEASE) ? 0 : -22;
}
artbox_vm_ops artbox_native_vm(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    artbox_vm_ops result = {info.dwPageSize, reserve, protect, reset, release};
    return result;
}
#else
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

static int linux_error(void) {
    switch (errno) {
        case EINVAL: return -22;
        case ENOMEM: return -12;
        case EACCES: return -13;
        case EPERM: return -1;
        default: return -5;
    }
}
static int permission(unsigned prot) {
    return (prot & 1 ? PROT_READ : 0) | (prot & 2 ? PROT_WRITE : 0);
}
static int reserve(size_t length, void **address) {
    void *result = mmap(NULL, length, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (result == MAP_FAILED) { *address = NULL; return linux_error(); }
    *address = result;
    return 0;
}
static int protect(void *address, size_t length, unsigned prot) {
    return mprotect(address, length, permission(prot)) ? linux_error() : 0;
}
static int reset(void *address, size_t length, unsigned prot) {
    // Darwin MADV_DONTNEED does not promise Linux's immediate demand-zero
    // behavior. Replacing only an owned anonymous range provides that result.
    void *result = mmap(address, length, permission(prot), MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    return result == MAP_FAILED ? linux_error() : result == address ? 0 : -5;
}
static int release(void *address, size_t length) {
    return munmap(address, length) ? linux_error() : 0;
}
artbox_vm_ops artbox_native_vm(void) {
    long page = sysconf(_SC_PAGESIZE);
    artbox_vm_ops result = {page > 0 ? (size_t)page : 0, reserve, protect, reset, release};
    return result;
}
#endif
