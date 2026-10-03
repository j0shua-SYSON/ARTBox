#include "artbox/managed_reference.h"
#include "artbox/native_vm.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "reference contract line %d\n", __LINE__); return 1; } } while (0)

int main(void) {
    artbox_reference_window window, before;
    uint64_t base = UINT64_C(0x1200000000), address = UINT64_C(0xaabbccddeeff0011);
    uint32_t reference = 0xabcdef01;
    CHECK(artbox_reference_window_init(&window, base, UINT64_C(0x100000000), 16384) == 0);
    before = window;
    CHECK(artbox_reference_window_init(&window, base, UINT64_C(0x100000008), 16384) == -22);
    CHECK(memcmp(&before, &window, sizeof(window)) == 0);
    CHECK(artbox_reference_window_init(&window, UINT64_MAX - 7, 16384, 8) == -22);
    CHECK(artbox_reference_window_init(&window, base + 1, 16384, 8) == -22);
    CHECK(artbox_reference_window_init(&window, base, 16384, 0) == -22);
    CHECK(artbox_reference_window_init(&window, base, 16384, 16384) == -22);
    CHECK(artbox_reference_encode(&window, 0, &reference) == 0 && reference == 0);
    CHECK(artbox_reference_decode(&window, 0, &address) == 0 && address == 0);
    CHECK(artbox_reference_encode(&window, base + 16384, &reference) == 0 && reference == 16384);
    CHECK(artbox_reference_decode(&window, reference, &address) == 0 && address == base + 16384);
    CHECK(artbox_reference_encode(&window, base + UINT64_C(0xfffffff8), &reference) == 0 && reference == 0xfffffff8);
    CHECK(artbox_reference_decode(&window, reference, &address) == 0 && address == base + UINT64_C(0xfffffff8));
    const uint64_t bad_addresses[] = {base - 8, base, base + 8, base + 16383,
        base + 16385, base + UINT64_C(0x100000000), UINT64_MAX};
    for (size_t i = 0; i < sizeof(bad_addresses) / sizeof(*bad_addresses); ++i) {
        reference = 0xabcdef01;
        CHECK(artbox_reference_encode(&window, bad_addresses[i], &reference) == -14);
        CHECK(reference == 0xabcdef01);
    }
    const uint32_t bad_references[] = {1, 8, 16383, 16385, UINT32_MAX};
    for (size_t i = 0; i < sizeof(bad_references) / sizeof(*bad_references); ++i) {
        address = UINT64_C(0xaabbccddeeff0011);
        CHECK(artbox_reference_decode(&window, bad_references[i], &address) == -14);
        CHECK(address == UINT64_C(0xaabbccddeeff0011));
    }
    CHECK(artbox_reference_window_init(&window, base, 65536, 16384) == 0);
    CHECK(artbox_reference_decode(&window, 65536, &address) == -14);
    CHECK(artbox_reference_encode(&window, base + 65536, &reference) == -14);
    CHECK(artbox_reference_encode(NULL, 0, &reference) == -22);
    CHECK(artbox_reference_decode(NULL, 0, &address) == -22);
    CHECK(artbox_reference_encode(&window, 0, NULL) == -22);
    CHECK(artbox_reference_decode(&window, 0, NULL) == -22);

    if (sizeof(uintptr_t) >= 8) {
        artbox_vm_ops ops = artbox_native_vm();
        size_t span = ops.page_size * 4;
        void *storage = NULL;
        CHECK(ops.reserve(span, &storage) == 0);
        CHECK((uintptr_t)storage >= UINT64_C(0x100000000));
        CHECK(artbox_reference_window_init(&window, (uintptr_t)storage, span, ops.page_size) == 0);
        unsigned char *object = (unsigned char *)storage + ops.page_size;
        CHECK(ops.protect(object, ops.page_size, 3) == 0);
        CHECK(artbox_reference_encode(&window, (uintptr_t)object, &reference) == 0);
        CHECK(reference == ops.page_size);
        CHECK(artbox_reference_decode(&window, reference, &address) == 0);
        memset((void *)(uintptr_t)address, 0x53, ops.page_size);
        for (size_t i = 0; i < ops.page_size; ++i) CHECK(object[i] == 0x53);
        CHECK(artbox_reference_decode(&window, 0, &address) == 0 && address == 0);
        CHECK(ops.release(storage, span) == 0);
        printf("{\"native_base\":%llu,\"reference\":%u,\"high_address_roundtrip\":true}\n",
               (unsigned long long)window.base, reference);
    }
    return 0;
}
