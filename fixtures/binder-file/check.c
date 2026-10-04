/* Original paired Binder descriptor expectations. SPDX-License-Identifier: MIT */
#include "check.h"
#include <stdio.h>
#define CHECK(x) do { ++cases; if (!(x)) { fprintf(stderr, "Binder file reference line %d: %s\n", __LINE__, #x); return -1; } } while (0)
int artbox_binder_file_check(void *context, const artbox_binder_file_ops *ops, void *scratch) {
    int cases = 0;
    uint64_t address = (uint64_t)(uintptr_t)scratch;
    int fd = ops->open(context, 2 | 0x80800);
    CHECK(fd >= 0);
    CHECK(ops->type(context, fd) == 0020000);
    CHECK(ops->read(context, fd, address, 4) == -22);
    CHECK(ops->read(context, fd, address, 0) == -22);
    CHECK(ops->write(context, fd, address, 4) == -22);
    CHECK(ops->write(context, fd, address, 0) == -22);
    CHECK(ops->seek(context, fd, 0, 0) == -29);
    CHECK(ops->seek(context, fd, 4, 1) == -29);
    CHECK(ops->seek(context, fd, 0, 5) == -22);
    CHECK(ops->close(context, fd) == 0);
    CHECK(ops->close(context, fd) == -9);
    CHECK(ops->read(context, fd, address, 4) == -9);
    fd = ops->open(context, 0x80000);
    CHECK(fd >= 0);
    CHECK(ops->write(context, fd, address, 4) == -9);
    CHECK(ops->read(context, fd, address, 4) == -22);
    CHECK(ops->close(context, fd) == 0);
    fd = ops->open(context, 1 | 0x80000);
    CHECK(fd >= 0);
    CHECK(ops->read(context, fd, address, 4) == -9);
    CHECK(ops->write(context, fd, address, 4) == -22);
    CHECK(ops->close(context, fd) == 0);
    CHECK(ops->open(context, 0x84000) == -20);
    CHECK(ops->open(context, 0x800c2) == -17);
    return cases;
}
