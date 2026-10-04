/* Original native Binder mapping/lifetime expectations. SPDX-License-Identifier: MIT */
#include "check.h"
#include "artbox/binder_wire.h"
#include <stdio.h>
#define CHECK(x) do { ++cases; if (!(x)) { fprintf(stderr, "Binder mapping reference line %d: %s\n", __LINE__, #x); return -1; } } while (0)
static int64_t wait_manager(void *context, const artbox_binder_device_ops *device, int fd) {
    int64_t result = -16;
    for (int attempt = 0; attempt < 2000 && result == -16; ++attempt) {
        result = device->ioctl(context, fd, ARTBOX_BINDER_SET_CONTEXT_MGR, 0);
        if (result == -16) device->pause(context);
    }
    return result;
}
int artbox_binder_mapping_check(void *context, const artbox_binder_device_ops *device,
    const artbox_binder_mapping_ops *memory) {
    int cases = 0;
    uint64_t page = memory->page_size;
    CHECK(page >= 4096 && page <= 65536 && !(page & (page - 1)));
    int a = device->open(context);
    CHECK(a >= 0);
    CHECK(memory->map(context, a, 0, 1, 2, 0) == -22);
    CHECK(memory->map(context, a, page, 3, 2, 0) == -1);
    CHECK(memory->map(context, a, page, 1, 2, 1) == -22);
    int64_t first = memory->map(context, a, page * 2, 1, 2, 0);
    CHECK(first > 0);
    CHECK(memory->map(context, a, page, 1, 2, 0) == -16);
    CHECK(memory->map(context, a, page, 3, 2, 0) == -1);
    uint64_t address = (uint64_t)first;
    CHECK(memory->protect(context, address, page * 2, 3) == -13);
    CHECK(memory->protect(context, address, page * 2, 0) == 0);
    CHECK(memory->protect(context, address, page * 2, 1) == 0);
    CHECK(wait_manager(context, device, a) == 0);
    CHECK(device->close(context, a) == 0);
    int b = device->open(context);
    CHECK(b >= 0);
    /* Closing an FD alone must not destroy its mapped open description. */
    CHECK(device->ioctl(context, b, ARTBOX_BINDER_SET_CONTEXT_MGR, 0) == -16);
    CHECK(memory->unmap(context, address, page) == 0);
    CHECK(device->ioctl(context, b, ARTBOX_BINDER_SET_CONTEXT_MGR, 0) == -16);
    CHECK(memory->unmap(context, address + page, page) == 0);
    CHECK(wait_manager(context, device, b) == 0);
    first = memory->map(context, b, page, 1, 2, 0);
    CHECK(first > 0);
    CHECK(memory->unmap(context, (uint64_t)first, page) == 0);
    /* Binder's allocator remains initialized even after the VMA is gone. */
    CHECK(memory->map(context, b, page, 1, 2, 0) == -16);
    CHECK(device->close(context, b) == 0);
    int c = device->open(context);
    CHECK(c >= 0);
    first = memory->map(context, c, page, 1, 1, 0);
    CHECK(first > 0);
    CHECK(device->close(context, c) == 0);
    CHECK(memory->protect(context, (uint64_t)first, page, 0) == 0);
    CHECK(memory->unmap(context, (uint64_t)first, page) == 0);
    int readonly = memory->open(context, 0);
    CHECK(readonly >= 0);
    first = memory->map(context, readonly, page, 1, 1, 0);
    CHECK(first > 0);
    CHECK(device->close(context, readonly) == 0);
    CHECK(memory->unmap(context, (uint64_t)first, page) == 0);
    int writeonly = memory->open(context, 1);
    CHECK(writeonly >= 0);
    CHECK(memory->map(context, writeonly, page, 1, 2, 0) == -13);
    CHECK(device->close(context, writeonly) == 0);
    return cases;
}
