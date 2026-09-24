// Original ARTBox contract: exercise actual AOSP MemMap and CardTable operations.
#include "artbox_art_heap.h"
#include "base/mem_map.h"
#include "gc/accounting/card_table-inl.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <sys/mman.h>

#define HEAP_CHECK(x) do { if (!(x)) { std::fprintf(stderr, "ART heap window line %d: %s\n", __LINE__, #x); return false; } } while (0)

bool artbox_check_art_heap_window(artbox_vm* vm) {
  using art::MemMap;
  using art::gc::accounting::CardTable;
  const auto window = artbox_art_heap_window();
  const size_t page = artbox_vm_page_size(vm);
  HEAP_CHECK(window.base >= UINT64_C(0x100000000) && window.guard_bytes == page);
  MemMap::Init();
  std::string error;
  auto heap = MemMap::MapAnonymous("ARTBox heap contract", page * 4,
      PROT_READ | PROT_WRITE, true, &error);
  HEAP_CHECK(heap.IsValid() && reinterpret_cast<uintptr_t>(heap.Begin()) == window.base + page);
  uint8_t* first = heap.Begin();
  std::memset(first, 0x5a, page * 4);
  HEAP_CHECK(artbox_vm_access(vm, reinterpret_cast<uintptr_t>(first), page * 4, 3));
  HEAP_CHECK(heap.Protect(PROT_READ));
  HEAP_CHECK(!artbox_vm_access(vm, reinterpret_cast<uintptr_t>(first), page, 2));
  HEAP_CHECK(heap.Protect(PROT_READ | PROT_WRITE));
  auto tail = heap.RemapAtEnd(first + page * 2, "ARTBox tail", PROT_READ | PROT_WRITE, &error);
  HEAP_CHECK(tail.IsValid() && tail.Begin() == first + page * 2 && heap.Size() == page * 2);
  HEAP_CHECK(*heap.Begin() == 0x5a && *tail.Begin() == 0);
  HEAP_CHECK(artbox_vm_access(vm, reinterpret_cast<uintptr_t>(tail.Begin()), page * 2, 3));
  heap.Reset();
  HEAP_CHECK(!artbox_vm_access(vm, reinterpret_cast<uintptr_t>(first), page, 1));
  HEAP_CHECK(artbox_vm_reserved_bytes(vm) == window.length);
  auto reused = MemMap::MapAnonymous("ARTBox reuse", page * 2,
      PROT_READ | PROT_WRITE, true, &error);
  HEAP_CHECK(reused.IsValid() && reused.Begin() == first && *reused.Begin() == 0);
  auto ordinary = MemMap::MapAnonymous("ARTBox ordinary", page,
      PROT_READ | PROT_WRITE, false, &error);
  HEAP_CHECK(ordinary.IsValid() && !artbox_art_heap_overlaps(ordinary.Begin(), ordinary.Size()));
  // ART's mremap path must not move storage out of the ownership registry.
  HEAP_CHECK(!reused.ReplaceWith(&tail, &error) && reused.Begin() == first && tail.IsValid());
  const uint32_t reference = artbox_art_reference_compress(first);
  HEAP_CHECK(reference == page && artbox_art_reference_decompress(reference) == first);
  std::unique_ptr<CardTable> cards(CardTable::Create(
      reinterpret_cast<const uint8_t*>(window.base + window.guard_bytes),
      window.length - window.guard_bytes));
  HEAP_CHECK(cards && cards->AddrIsInCardTable(first));
  auto* last = reinterpret_cast<art::mirror::Object*>(window.base + window.length - 8);
  HEAP_CHECK(cards->AddrIsInCardTable(last));
  cards->MarkCard(first);
  cards->MarkCard(last);
  HEAP_CHECK(cards->IsDirty(reinterpret_cast<art::mirror::Object*>(first)) && cards->IsDirty(last));
  cards->ClearCardTable();
  HEAP_CHECK(cards->IsClean(reinterpret_cast<art::mirror::Object*>(first)) && cards->IsClean(last));
  cards.reset();
  ordinary.Reset();
  reused.Reset();
  tail.Reset();
  HEAP_CHECK(artbox_vm_reserved_bytes(vm) == window.length);
  HEAP_CHECK(!artbox_vm_access(vm, window.base + page, page * 4, 0));
  MemMap::Shutdown();
  std::printf("ARTBox managed window: {\"base\":%llu,\"length\":%llu,\"guard\":%llu,\"memmap_contract\":true}\n",
      static_cast<unsigned long long>(window.base), static_cast<unsigned long long>(window.length),
      static_cast<unsigned long long>(window.guard_bytes));
  return true;
}
