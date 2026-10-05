/* Original Linux eventfd expectations, not kernel code. SPDX-License-Identifier: MIT */
#include "check.h"
#include <stdio.h>
#define CHECK(x) do { ++cases; if (!(x)) { fprintf(stderr, "eventfd line %d: %s\n", __LINE__, #x); return -1; } } while (0)

int artbox_eventfd_check(void *c, const artbox_eventfd_ops *ops) {
    int cases = 0;
    const uint64_t flags = UINT64_C(0x80800), canary = UINT64_C(0xfedc12345678abcd);
    uint64_t value[2] = {0, canary};
    CHECK(ops->create(c, 0, flags | 2) == -22);
    CHECK(ops->create(c, 0, flags | UINT64_C(0x80000000)) == -22);
    /* The syscall's unsigned-int initial value and int flags discard high bits. */
    int fd = ops->create(c, UINT64_C(0x100000003), flags | UINT64_C(0x100000000));
    CHECK(fd >= 0);
    CHECK(ops->snapshot(c, fd, 5) == 5);
    CHECK(ops->read(c, fd, value, 16) == 8 && value[0] == 3 && value[1] == canary);
    CHECK(ops->snapshot(c, fd, 5) == 4);
    CHECK(ops->snapshot(c, fd, 1) == 0);
    CHECK(ops->read(c, fd, value, 8) == -11 && value[0] == 3 && value[1] == canary);
    CHECK(ops->read(c, fd, NULL, 8) == -11);
    CHECK(ops->read(c, fd, NULL, 7) == -22);
    CHECK(ops->read(c, fd, value, 0) == -22);
    CHECK(ops->write(c, fd, NULL, 0) == -22);
    CHECK(ops->write(c, fd, NULL, 7) == -22);
    CHECK(ops->write(c, fd, NULL, 9) == -22);
    CHECK(ops->write(c, fd, value, 16) == -22);
    CHECK(ops->write(c, fd, NULL, 8) == -14);
    CHECK(ops->snapshot(c, fd, 5) == 4);
    value[0] = UINT64_MAX;
    CHECK(ops->write(c, fd, value, 8) == -22);
    value[0] = 0;
    CHECK(ops->write(c, fd, value, 8) == 8);
    CHECK(ops->snapshot(c, fd, 5) == 4);
    value[0] = UINT64_C(0x100000000);
    CHECK(ops->write(c, fd, value, 8) == 8);
    value[0] = 7;
    CHECK(ops->write(c, fd, value, 8) == 8);
    CHECK(ops->snapshot(c, fd, 5) == 5 && ops->snapshot(c, fd, 1) == 1);
    CHECK(ops->read(c, fd, value, 8) == 8 && value[0] == UINT64_C(0x100000007));
    value[0] = UINT64_MAX - 1;
    CHECK(ops->write(c, fd, value, 8) == 8);
    CHECK(ops->snapshot(c, fd, 5) == 1 && ops->snapshot(c, fd, 4) == 0);
    value[0] = 1;
    CHECK(ops->write(c, fd, value, 8) == -11);
    CHECK(ops->write(c, fd, NULL, 8) == -14);
    value[0] = 0;
    CHECK(ops->write(c, fd, value, 8) == 8);
    CHECK(ops->read(c, fd, value, 8) == 8 && value[0] == UINT64_MAX - 1);
    CHECK(ops->snapshot(c, fd, 5) == 4);
    /* Linux commits the read before copying out, including the fault path. */
    value[0] = 19;
    CHECK(ops->write(c, fd, value, 8) == 8);
    CHECK(ops->read(c, fd, NULL, 8) == -14);
    CHECK(ops->read(c, fd, value, 8) == -11);
    CHECK(ops->snapshot(c, fd, 5) == 4);
    CHECK(ops->seek(c, fd, 123, 0) == 0);
    CHECK(ops->seek(c, fd, -1, 1) == 0);
    CHECK(ops->seek(c, fd, 17, 4) == 0);
    CHECK(ops->seek(c, fd, 0, 5) == -22);
    CHECK(ops->close(c, fd) == 0);
    CHECK(ops->read(c, fd, value, 8) == -9);
    CHECK(ops->write(c, fd, value, 8) == -9);
    CHECK(ops->close(c, fd) == -9);

    fd = ops->create(c, 3, flags | 1);
    CHECK(fd >= 0);
    CHECK(ops->read(c, fd, value, 16) == 8 && value[0] == 1 && value[1] == canary);
    CHECK(ops->read(c, fd, NULL, 8) == -14);
    CHECK(ops->snapshot(c, fd, 5) == 5);
    CHECK(ops->read(c, fd, value, 8) == 8 && value[0] == 1);
    CHECK(ops->read(c, fd, value, 8) == -11);
    value[0] = UINT64_MAX - 1;
    CHECK(ops->write(c, fd, value, 8) == 8);
    CHECK(ops->snapshot(c, fd, 5) == 1);
    CHECK(ops->read(c, fd, value, 8) == 8 && value[0] == 1);
    CHECK(ops->snapshot(c, fd, 5) == 5);
    value[0] = 2;
    CHECK(ops->write(c, fd, value, 8) == -11);
    value[0] = 1;
    CHECK(ops->write(c, fd, value, 8) == 8);
    CHECK(ops->snapshot(c, fd, 5) == 1);
    CHECK(ops->close(c, fd) == 0);
    fd = ops->create(c, UINT32_MAX, flags);
    CHECK(fd >= 0);
    int other = ops->create(c, 2, flags);
    CHECK(other >= 0 && other != fd);
    CHECK(ops->read(c, other, value, 8) == 8 && value[0] == 2);
    CHECK(ops->read(c, fd, value, 8) == 8 && value[0] == UINT32_MAX);
    CHECK(ops->snapshot(c, fd, 5) == 4 && ops->snapshot(c, other, 5) == 4);
    CHECK(ops->close(c, other) == 0 && ops->close(c, fd) == 0);
    return cases;
}
