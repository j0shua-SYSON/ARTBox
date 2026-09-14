// Original ARTBox contract using pinned AOSP reference types; see THIRD_PARTY.md.
#include "mirror/object_reference.h"
#include <stdint.h>
#include <new>

using Object = art::mirror::Object;
using Plain = art::mirror::ObjectReference<false, Object>;
using Poisoned = art::mirror::ObjectReference<true, Object>;
using Stack = art::mirror::CompressedReference<Object>;
using Heap = art::mirror::HeapReference<Object>;
static_assert(sizeof(Plain) == 4 && sizeof(Poisoned) == 4 && sizeof(Stack) == 4 && sizeof(Heap) == 4);
static_assert(alignof(Plain) == 4 && alignof(Heap) == 4);

template<class T> static uint32_t bits(const T& v) {
    // Read the actual representation; do not let the optimizer replace all
    // storage observations with the constructor arguments. Android ARM64 is LE.
    const volatile unsigned char *p = reinterpret_cast<const volatile unsigned char*>(&v);
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

extern "C" __attribute__((visibility("default")))
int artbox_art_reference_check(void *first, void *second, uint32_t first_bits, uint32_t second_bits,
                              uint32_t *observations, void *heap_storage) {
    int cases = 0;
#define REQUIRE(x) do { ++cases; if (!(x)) return -cases; } while (0)
    Object *a = static_cast<Object*>(first), *b = static_cast<Object*>(second);
    auto p = Plain::FromMirrorPtr(nullptr);
    REQUIRE(p.IsNull() && p.AsMirrorPtr() == nullptr && bits(p) == 0);
    p.Assign(a);
    REQUIRE(p.AsMirrorPtr() == a && !p.IsNull() && bits(p) == first_bits);
    auto copied = p;
    REQUIRE(copied.AsMirrorPtr() == a && bits(copied) == first_bits);
    p.Assign(b);
    REQUIRE(p.AsMirrorPtr() == b && bits(p) == second_bits);
    p.Clear();
    REQUIRE(p.IsNull() && bits(p) == 0);
    REQUIRE(copied.AsMirrorPtr() == a);
    auto poisoned = Poisoned::FromMirrorPtr(a);
    REQUIRE(poisoned.AsMirrorPtr() == a && bits(poisoned) == uint32_t(0u-first_bits));
    poisoned.Assign(b);
    REQUIRE(poisoned.AsMirrorPtr() == b && bits(poisoned) == uint32_t(0u-second_bits));
    poisoned.Clear();
    REQUIRE(poisoned.IsNull() && poisoned.AsMirrorPtr() == nullptr);
    auto stack = Stack::FromMirrorPtr(a);
    REQUIRE(stack.AsMirrorPtr() == a && stack.AsVRegValue() == first_bits);
    stack = Stack::FromVRegValue(second_bits);
    REQUIRE(stack.AsMirrorPtr() == b && stack.AsVRegValue() == second_bits);
    observations[3] = bits(stack);
    stack = Stack::FromVRegValue(0);
    REQUIRE(stack.IsNull() && stack.AsMirrorPtr() == nullptr);
    // External storage forces real atomic accesses. The host supplies aligned
    // bytes; this placement construction establishes the C++ object's lifetime.
    Heap& heap = *new (heap_storage) Heap();
    REQUIRE(heap.IsNull() && bits(heap) == 0 && heap.AsMirrorPtr() == nullptr);
    heap.Assign(a);
    REQUIRE(heap.AsMirrorPtr() == a && bits(heap) == (art::kPoisonHeapReferences ? uint32_t(0u-first_bits) : first_bits));
    observations[0] = art::kPoisonHeapReferences;
    observations[1] = bits(heap);
    heap.Assign<true>(b);
    REQUIRE(heap.AsMirrorPtr<true>() == b && bits(heap) == (art::kPoisonHeapReferences ? uint32_t(0u-second_bits) : second_bits));
    observations[2] = bits(heap);
    heap.Clear();
    REQUIRE(heap.IsNull() && heap.AsMirrorPtr<true>() == nullptr);
    heap.Assign<true>(nullptr);
    REQUIRE(heap.IsNull() && bits(heap) == 0);
    heap.~Heap();
    // These are opaque aligned storage addresses, not constructed ART Objects.
    // Dereferencing only as uint64_t checks that decoded addresses reach memory.
    auto first_slot = Stack::FromMirrorPtr(a);
    auto second_slot = Stack::FromMirrorPtr(b);
    *reinterpret_cast<volatile uint64_t*>(first_slot.AsMirrorPtr()) = UINT64_C(0x123456789abcdef0);
    REQUIRE(*static_cast<volatile uint64_t*>(first) == UINT64_C(0x123456789abcdef0));
    *reinterpret_cast<volatile uint64_t*>(second_slot.AsMirrorPtr()) = UINT64_C(0xfedcba9876543210);
    REQUIRE(*static_cast<volatile uint64_t*>(second) == UINT64_C(0xfedcba9876543210));
    return cases;
#undef REQUIRE
}
