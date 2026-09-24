// Original ARTBox contract: exercise actual AOSP MemMap and CardTable operations.
#include "artbox_art_heap.h"
#include "base/mem_map.h"
#include "class_table-inl.h"
#include "gc/accounting/card_table-inl.h"
#include "gc/accounting/space_bitmap-inl.h"
#include "gc/space/space.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <sys/mman.h>

#define HEAP_CHECK(x) do { if (!(x)) { std::fprintf(stderr, "ART heap window line %d: %s\n", __LINE__, #x); return false; } } while (0)

namespace {
class BitmapSpace final : public art::gc::space::DiscontinuousSpace {
 public:
  BitmapSpace() : DiscontinuousSpace("ARTBox large bitmap contract",
      art::gc::space::kGcRetentionPolicyAlwaysCollect) {}
  bool Contains(const art::mirror::Object* object) const override {
    return live_bitmap_.HasAddress(object);
  }
  art::gc::space::SpaceType GetType() const override {
    return art::gc::space::kSpaceTypeLargeObjectSpace;
  }
  bool CanMoveObjects() const override { return false; }
};

bool check_large_object_bitmaps(const artbox_reference_window& window) {
  // Use the real DiscontinuousSpace constructor; bitmap operations only inspect addresses.
  BitmapSpace space;
  auto* live = space.GetLiveBitmap();
  auto* mark = space.GetMarkBitmap();
  auto* first = reinterpret_cast<art::mirror::Object*>(window.base + window.guard_bytes);
  auto* last = reinterpret_cast<art::mirror::Object*>(window.base + window.length - art::kMinPageSize);
  HEAP_CHECK(live->HeapBegin() == window.base && mark->HeapBegin() == window.base);
  HEAP_CHECK(live->HeapLimit() == window.base + window.length &&
             mark->HeapLimit() == window.base + window.length);
  HEAP_CHECK(live->HasAddress(first) && live->HasAddress(last));
  HEAP_CHECK(!live->HasAddress(reinterpret_cast<void*>(window.base - art::kMinPageSize)) &&
             !live->HasAddress(reinterpret_cast<void*>(window.base + window.length)));
  HEAP_CHECK(!live->Set(first) && !live->Set(last) && live->Set(first));
  HEAP_CHECK(live->Test(first) && live->Test(last) && !mark->Test(first) && !mark->Test(last));
  mark->CopyFrom(live);
  HEAP_CHECK(mark->Test(first) && mark->Test(last));
  HEAP_CHECK(live->Clear(first) && !live->Test(first) && live->Test(last) && mark->Test(first));
  live->Clear();
  mark->Clear();
  HEAP_CHECK(!live->Test(first) && !live->Test(last) && !mark->Test(first) && !mark->Test(last));
  return true;
}

struct MoveClassRoot {
  art::mirror::Object* before;
  art::mirror::Object* after;
  bool* visited;
  void VisitRoot(art::mirror::CompressedReference<art::mirror::Object>* root) const {
    *visited = root->AsMirrorPtr() == before;
    root->Assign(after);
  }
};

bool check_class_slots(uint8_t* storage) {
  // Only exercise slot storage: these aligned addresses are not initialized classes.
  // Release, no-read-barrier operations do not dereference their class contents.
  static_assert(!art::kIsDebugBuild);
  using Slot = art::ClassTable::TableSlot;
  auto* first = reinterpret_cast<art::mirror::Class*>(storage);
  auto* second = reinterpret_cast<art::mirror::Class*>(storage + 64);
  const uint32_t encoded = artbox_art_reference_compress(first);
  const uint32_t moved = artbox_art_reference_compress(second);
  Slot empty;
  HEAP_CHECK(empty.Data() == 0 && empty.IsNull());
  for (uint32_t bits = 0; bits < art::kObjectAlignment; ++bits) {
    const uint32_t hash = 0x9abcde00u | bits;
    Slot slot(art::ObjPtr<art::mirror::Class>(first), hash);
    HEAP_CHECK(slot.Data() == (encoded | bits) && slot.NonHashData() == encoded);
    HEAP_CHECK(slot.Hash() == bits && slot.MaskedHashEquals(hash));
    HEAP_CHECK(!slot.MaskedHashEquals(hash ^ 1u));
    HEAP_CHECK(slot.Read<art::kWithoutReadBarrier>().Ptr() == first && !slot.IsNull());
    Slot raw(encoded, hash), copy(slot), assigned;
    assigned = slot;
    HEAP_CHECK(raw.Data() == slot.Data() && copy.Data() == slot.Data() &&
               assigned.Data() == slot.Data());
    HEAP_CHECK(raw.Read<art::kWithoutReadBarrier>().Ptr() == first);
    bool visited = false;
    slot.VisitRoot(MoveClassRoot{first, second, &visited});
    HEAP_CHECK(visited && slot.Data() == (moved | bits));
    HEAP_CHECK(slot.Read<art::kWithoutReadBarrier>().Ptr() == second && slot.Hash() == bits);
    visited = false;
    slot.VisitRoot(MoveClassRoot{second, second, &visited});
    HEAP_CHECK(visited && slot.Data() == (moved | bits));
    visited = false;
    slot.VisitRoot(MoveClassRoot{second, nullptr, &visited});
    HEAP_CHECK(visited && slot.IsNull() && slot.Data() == bits);
    HEAP_CHECK(copy.Read<art::kWithoutReadBarrier>().Ptr() == first);
  }
  return true;
}
}

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
  HEAP_CHECK(check_class_slots(first));
  HEAP_CHECK(check_large_object_bitmaps(window));
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
  std::printf("ARTBox managed window: {\"base\":%llu,\"length\":%llu,\"guard\":%llu,\"memmap_contract\":true,\"class_table_contract\":true,\"large_object_bitmap_contract\":true}\n",
      static_cast<unsigned long long>(window.base), static_cast<unsigned long long>(window.length),
      static_cast<unsigned long long>(window.guard_bytes));
  return true;
}
