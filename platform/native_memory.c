#include "artbox/native_memory.h"
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static DWORD protection(unsigned prot) {
    return prot == 0 ? PAGE_NOACCESS : prot == 1 ? PAGE_READONLY : PAGE_READWRITE;
}
static int native_map(size_t size, unsigned prot, void **address) {
    *address = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, protection(prot));
    return *address ? 0 : -12;
}
static int native_protect(void *address, size_t size, unsigned prot) {
    DWORD previous;
    return VirtualProtect(address, size, protection(prot), &previous) ? 0 : -22;
}
static int native_unmap(void *address, size_t size) {
    (void)size;
    return VirtualFree(address, 0, MEM_RELEASE) ? 0 : -22;
}
artbox_memory_ops artbox_native_memory(void) {
    SYSTEM_INFO info;
    artbox_memory_ops result;
    GetSystemInfo(&info);
    result.page_size = info.dwPageSize; result.map = native_map;
    result.protect = native_protect; result.unmap = native_unmap;
    return result;
}
#else
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
static int linux_error(void) {
    switch (errno) {
    case EINVAL: return -22; case ENOMEM: return -12;
    case EACCES: return -13; case EPERM: return -1;
    default: return -5;
    }
}
static int protection(unsigned prot) {
    return (prot & 1 ? PROT_READ : 0) | (prot & 2 ? PROT_WRITE : 0);
}
static int native_map(size_t size, unsigned prot, void **address) {
    void *result = mmap(NULL, size, protection(prot), MAP_PRIVATE | MAP_ANON, -1, 0);
    if (result == MAP_FAILED) { *address = NULL; return linux_error(); }
    *address = result; return 0;
}
static int native_protect(void *address, size_t size, unsigned prot) {
    return mprotect(address, size, protection(prot)) == 0 ? 0 : linux_error();
}
static int native_unmap(void *address, size_t size) {
    return munmap(address, size) == 0 ? 0 : linux_error();
}
artbox_memory_ops artbox_native_memory(void) {
    artbox_memory_ops result;
    long page = sysconf(_SC_PAGESIZE);
    result.page_size = page > 0 ? (size_t)page : 0;
    result.map = native_map; result.protect = native_protect; result.unmap = native_unmap;
    return result;
}
#endif
