// SPDX-License-Identifier: MIT
// Original ARTBox tests of actual pinned ART storage types, not a collector.
#include "lock_word-inl.h"
#include "jni/local_reference_table-inl.h"
#include <stdint.h>
#include <new>

using Object = art::mirror::Object;
using Heap = art::mirror::HeapReference<Object>;
using Root = art::GcRoot<Object>;
using Word = art::LockWord;
using Local = art::jni::LrtEntry;
static_assert(sizeof(Word) == 4 && sizeof(Root) == 4 && sizeof(Local) == 4);

template<class T> static uint32_t bits(const T& value) {
    const volatile unsigned char* p = reinterpret_cast<const volatile unsigned char*>(&value);
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

extern "C" __attribute__((visibility("default")))
int artbox_art_storage_check(void* first, void* second, uint32_t first_bits, uint32_t second_bits,
                             uint32_t* observations, void* storage) {
    int cases = 0;
#define REQUIRE(x) do { ++cases; if (!(x)) return -cases; } while (0)
    Object* a = static_cast<Object*>(first);
    Object* b = static_cast<Object*>(second);
    REQUIRE(Word::Default().GetState() == Word::kUnlocked);
    auto thin = Word::FromThinLockId(17, 3, 0);
    REQUIRE(thin.GetState() == Word::kThinLocked && thin.ThinLockOwner() == 17 && thin.ThinLockCount() == 3);
    auto hash = Word::FromHashCode(1234, 0);
    REQUIRE(hash.GetState() == Word::kHashCode && hash.GetHashCode() == 1234);
    auto forward = Word::FromForwardingAddress(reinterpret_cast<uintptr_t>(a));
    REQUIRE(forward.GetState() == Word::kForwardingAddress);
    REQUIRE(forward.ForwardingAddress() == reinterpret_cast<uintptr_t>(a));
    REQUIRE(bits(forward) == ((first_bits >> Word::kForwardingAddressShift) | Word::kStateForwardingAddressShifted));
    Word& saved = *new (storage) Word(forward);
    REQUIRE(bits(saved) == bits(forward));
    forward = Word::FromForwardingAddress(reinterpret_cast<uintptr_t>(b));
    REQUIRE(saved.ForwardingAddress() == reinterpret_cast<uintptr_t>(a));
    REQUIRE(forward.ForwardingAddress() == reinterpret_cast<uintptr_t>(b));
    REQUIRE(bits(forward) == ((second_bits >> Word::kForwardingAddressShift) | Word::kStateForwardingAddressShifted));
    observations[3] = bits(forward);
    saved.~Word();

    Root root;
    REQUIRE(root.IsNull() && bits(root) == 0);
    root = Root(a);
    REQUIRE(!root.IsNull() && bits(root) == first_bits);
    REQUIRE(root.Read<art::kWithoutReadBarrier>() == a);
    root.AddressWithoutBarrier()->Assign(b);
    REQUIRE(root.Read<art::kWithoutReadBarrier>() == b && bits(root) == second_bits);
    root.AddressWithoutBarrier()->Clear();
    REQUIRE(root.IsNull() && bits(root) == 0);

    Local local;
    REQUIRE(local.IsNull() && bits(local) == 0);
    local.SetReference(a);
    REQUIRE(!local.IsFree() && !local.IsSerialNumber() && bits(local) == first_bits);
    REQUIRE(local.GetReference().Ptr() == a);
    local.SetNextFree(7);
    REQUIRE(local.IsFree() && !local.IsSerialNumber() && local.GetNextFree() == 7);
    local.SetSerialNumber(3);
    REQUIRE(!local.IsFree() && local.IsSerialNumber() && local.GetSerialNumber() == 3);
    local.SetReference(b);
    REQUIRE(!local.IsFree() && !local.IsSerialNumber() && local.GetReference().Ptr() == b && bits(local) == second_bits);
#ifdef ARTBOX_RELATIVE_REFERENCES
    local.SetDeadReference();
#else
    // The original LocalReferenceTable::Remove uses this sentinel setter.
    local.SetReference(reinterpret_cast<Object*>(UINT32_C(0xdead10c0)));
#endif
    REQUIRE(!local.IsFree() && !local.IsSerialNumber() && bits(local) == UINT32_C(0xdead10c0));
    local.SetReference(a);
    REQUIRE(local.GetReference().Ptr() == a && bits(local) == first_bits);

    Heap& heap = *new (storage) Heap();
    heap.Assign<true>(a);
    observations[0] = art::kPoisonHeapReferences;
    observations[1] = bits(heap);
    REQUIRE(heap.AsMirrorPtr<true>() == a);
    heap.Assign<true>(b);
    observations[2] = bits(heap);
    REQUIRE(heap.AsMirrorPtr<true>() == b);
    heap.~Heap();
    root = Root(a);
    *reinterpret_cast<volatile uint64_t*>(root.Read<art::kWithoutReadBarrier>()) = UINT64_C(0x123456789abcdef0);
    REQUIRE(*static_cast<volatile uint64_t*>(first) == UINT64_C(0x123456789abcdef0));
    local.SetReference(b);
    *reinterpret_cast<volatile uint64_t*>(local.GetReference().Ptr()) = UINT64_C(0xfedcba9876543210);
    REQUIRE(*static_cast<volatile uint64_t*>(second) == UINT64_C(0xfedcba9876543210));
    return cases;
#undef REQUIRE
}
