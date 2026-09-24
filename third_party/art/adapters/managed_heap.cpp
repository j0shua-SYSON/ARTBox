// Original ARTBox adapter. MIT. Compiled with the Linux/Android errno ABI.
#include "artbox_art_heap.h"
#include <cerrno>
#include <cstdlib>
#include <sys/mman.h>

namespace {
artbox_vm* owner;
artbox_reference_window window{};
bool contains(const void* address, size_t length) {
  const auto base = reinterpret_cast<uintptr_t>(address);
  return owner && base >= window.base && base - window.base <= window.length &&
      length <= window.length - (base - window.base);
}
int posix_result(int result) {
  if (result < 0) { errno = -result; return -1; }
  return result;
}
}

int artbox_art_heap_initialize(artbox_vm* vm, uint64_t length, size_t guard) {
  if (owner) return -16;
  artbox_reference_window candidate{};
  const int error = artbox_vm_reserve_window(vm, length, guard, &candidate);
  if (error) return error;
  window = candidate;
  owner = vm;
  return 0;
}
void artbox_art_heap_unbind(void) { owner = nullptr; window = {}; }
artbox_reference_window artbox_art_heap_window(void) { return window; }
int artbox_art_heap_overlaps(const void* address, size_t length) {
  if (!owner || !length) return 0;
  const auto base = reinterpret_cast<uintptr_t>(address);
  // An overflowing range is rejected by the bridge, never passed to native mmap.
  if (length > UINTPTR_MAX - base) return 1;
  return base <= window.base ? length > window.base - base : base - window.base < window.length;
}
void* artbox_art_heap_map(void* address, size_t length, int prot, int flags, int fd, int64_t offset) {
  static_assert((MAP_PRIVATE | MAP_ANONYMOUS) == 0x22 && MAP_FIXED == 0x10 &&
      PROT_READ == 1 && PROT_WRITE == 2 && PROT_EXEC == 4, "Linux mapping ABI required");
  const auto result = artbox_vm_mmap_window(owner, window.base,
      reinterpret_cast<uintptr_t>(address), length, prot, flags, fd, offset);
  if (result < 0) { errno = static_cast<int>(-result); return MAP_FAILED; }
  return reinterpret_cast<void*>(static_cast<uintptr_t>(result));
}
int artbox_art_heap_unmap(void* address, size_t length) {
  if (!contains(address, length)) return posix_result(-22);
  return posix_result(artbox_vm_munmap(owner, reinterpret_cast<uintptr_t>(address), length));
}
int artbox_art_heap_protect(void* address, size_t length, int prot) {
  if (!contains(address, length)) return posix_result(-22);
  return posix_result(artbox_vm_mprotect(owner, reinterpret_cast<uintptr_t>(address), length, prot));
}
uint32_t artbox_art_reference_compress(const void* address) {
  // Static ART roots may be initialized before the runtime binds its heap.
  if (!address) return 0;
  uint32_t reference = 0;
  if (!owner || artbox_reference_encode(&window, reinterpret_cast<uintptr_t>(address), &reference))
    std::abort();
  return reference;
}
void* artbox_art_reference_decompress(uint32_t reference) {
  if (!reference) return nullptr;
  uint64_t address = 0;
  if (!owner || artbox_reference_decode(&window, reference, &address)) std::abort();
  return reinterpret_cast<void*>(static_cast<uintptr_t>(address));
}
