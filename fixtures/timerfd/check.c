/* Original userspace expectations, not kernel implementation. SPDX-License-Identifier: MIT */
#include "check.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { ++cases; if (!(x)) { fprintf(stderr, "timerfd line %d: %s\n", __LINE__, #x); return -1; } } while (0)
static int ready(void *c, const artbox_timerfd_ops *ops, int fd) {
    int64_t start = ops->monotonic_ns(c);
    if (start < 0) return -1;
    for (;;) {
        int events = ops->snapshot(c, fd, 1);
        if (events) return events;
        int64_t now = ops->monotonic_ns(c);
        if (now < start || now - start > INT64_C(2000000000)) return -1;
        ops->pause(c);
    }
}
static artbox_timer_spec absolute(int64_t ns, int64_t interval) {
    artbox_timer_spec spec = {interval / 1000000000, interval % 1000000000,
                             ns / 1000000000, ns % 1000000000};
    return spec;
}
int artbox_timerfd_check(void *c, const artbox_timerfd_ops *ops) {
    int cases = 0;
    const uint64_t flags = UINT64_C(0x80800), canary = UINT64_C(0x12345678fedcabcd);
    uint64_t value[2] = {0, canary};
    artbox_timer_spec zero = {0, 0, 0, 0}, spec = zero, out = zero;
    CHECK(ops->create(c, 1, flags | 1) == -22);
    CHECK(ops->create(c, UINT64_MAX, flags) == -22);
    CHECK(ops->create(c, 2, flags) == -22);
    int fd = ops->create(c, UINT64_C(0x100000001), flags | UINT64_C(0x100000000));
    CHECK(fd >= 0);
    CHECK(ops->gettime(c, fd, &out) == 0 && !memcmp(&out, &zero, sizeof(out)));
    CHECK(ops->snapshot(c, fd, 5) == 0);
    CHECK(ops->read(c, fd, value, 8) == -11 && value[0] == 0 && value[1] == canary);
    CHECK(ops->read(c, fd, NULL, 8) == -11);
    CHECK(ops->read(c, fd, NULL, 7) == -22);
    CHECK(ops->read(c, fd, value, 0) == -22);
    CHECK(ops->write(c, fd, value, 8) == -22);
    CHECK(ops->settime(c, fd, 0, NULL, NULL) == -14);
    CHECK(ops->settime(c, fd, 4, NULL, NULL) == -14);
    CHECK(ops->settime(c, -1, 4, NULL, NULL) == -14);
    CHECK(ops->settime(c, fd, 4, &spec, NULL) == -22);
    CHECK(ops->settime(c, -1, 4, &spec, NULL) == -22);
    CHECK(ops->settime(c, -1, 0, &spec, NULL) == -9);
    CHECK(ops->gettime(c, -1, NULL) == -9);
    CHECK(ops->gettime(c, fd, NULL) == -14);
    spec.value_nanoseconds = 1000000000;
    CHECK(ops->settime(c, fd, 0, &spec, NULL) == -22);
    spec = zero; spec.interval_seconds = -1;
    CHECK(ops->settime(c, fd, 0, &spec, NULL) == -22);
    spec = zero; spec.interval_nanoseconds = -1;
    CHECK(ops->settime(c, fd, 0, &spec, NULL) == -22);
    spec = zero; spec.value_seconds = -1;
    CHECK(ops->settime(c, fd, 0, &spec, NULL) == -22);
    /* Disarmed timers retain the requested interval, but never become ready. */
    spec = zero; spec.interval_seconds = 5;
    CHECK(ops->settime(c, fd, 0, &spec, &out) == 0 && !memcmp(&out, &zero, sizeof(out)));
    CHECK(ops->gettime(c, fd, &out) == 0 && !memcmp(&out, &spec, sizeof(out)));
    CHECK(ops->snapshot(c, fd, 5) == 0);
    spec.value_seconds = 60;
    CHECK(ops->settime(c, fd, 0, &spec, &out) == 0 && out.interval_seconds == 5 && out.value_seconds == 0 && out.value_nanoseconds == 0);
    CHECK(ops->gettime(c, fd, &out) == 0 && out.interval_seconds == 5 && out.interval_nanoseconds == 0 &&
          out.value_seconds >= 0 && out.value_seconds <= 60 && out.value_nanoseconds >= 0 && out.value_nanoseconds < 1000000000);
    CHECK(ops->settime(c, fd, 0, &zero, (artbox_timer_spec *)(uintptr_t)1) == -14);
    CHECK(ops->gettime(c, fd, &out) == 0 && !memcmp(&out, &zero, sizeof(out)));
    CHECK(ops->read(c, fd, value, 8) == -11);
    /* A past absolute deadline is deterministic, without a sleep-as-readiness assumption. */
    int64_t now = ops->monotonic_ns(c); CHECK(now > 100000000);
    spec = absolute(now - 50000000, 0);
    CHECK(ops->settime(c, fd, 1, &spec, NULL) == 0);
    CHECK(ready(c, ops, fd) == 1 && ops->snapshot(c, fd, 4) == 0);
    CHECK(ops->snapshot(c, fd, 5) == 1);
    CHECK(ops->gettime(c, fd, &out) == 0 && !memcmp(&out, &zero, sizeof(out)));
    CHECK(ops->read(c, fd, value, 16) == 8 && value[0] == 1 && value[1] == canary);
    CHECK(ops->snapshot(c, fd, 5) == 0 && ops->read(c, fd, value, 8) == -11);
    CHECK(ops->settime(c, fd, 1, &spec, NULL) == 0 && ready(c, ops, fd) == 1);
    CHECK(ops->read(c, fd, NULL, 8) == -14);
    CHECK(ops->snapshot(c, fd, 5) == 0 && ops->read(c, fd, value, 8) == -11);
    CHECK(ops->settime(c, fd, 1, &spec, NULL) == 0 && ready(c, ops, fd) == 1);
    CHECK(ops->settime(c, fd, 0, &zero, NULL) == 0 && ops->snapshot(c, fd, 5) == 0);
    /* CANCEL_ON_SET is accepted but inert for CLOCK_MONOTONIC. */
    CHECK(ops->settime(c, fd, 3, &spec, NULL) == 0 && ready(c, ops, fd) == 1);
    CHECK(ops->read(c, fd, value, 8) == 8 && value[0] == 1);
    now = ops->monotonic_ns(c); CHECK(now > 100000000);
    spec = absolute(now - 50000000, 1000000);
    CHECK(ops->settime(c, fd, 1, &spec, NULL) == 0 && ready(c, ops, fd) == 1);
    CHECK(ops->gettime(c, fd, &out) == 0 && out.interval_seconds == 0 && out.interval_nanoseconds == 1000000 &&
          out.value_seconds == 0 && out.value_nanoseconds >= 0 && out.value_nanoseconds <= 1000000);
    CHECK(ops->snapshot(c, fd, 1) == 1); // gettime advances periodic deadlines without consuming ticks.
    CHECK(ops->read(c, fd, value, 8) == 8 && value[0] >= 50 && value[1] == canary);
    CHECK(ops->settime(c, fd, 0, &zero, NULL) == 0 && ops->read(c, fd, value, 8) == -11);
    spec = zero; spec.value_seconds = INT64_MAX; spec.interval_seconds = INT64_MAX;
    CHECK(ops->settime(c, fd, 0, &spec, NULL) == 0);
    CHECK(ops->gettime(c, fd, &out) == 0 && out.interval_seconds == INT64_C(9223372036) &&
          out.interval_nanoseconds == 854775807 && out.value_seconds > 86400);
    CHECK(ops->settime(c, fd, 0, &zero, NULL) == 0);
    CHECK(ops->seek(c, fd, 123, 0) == 0 && ops->seek(c, fd, -1, 1) == 0);
    CHECK(ops->seek(c, fd, 0, 4) == 0 && ops->seek(c, fd, 0, 5) == -22);
    CHECK(ops->close(c, fd) == 0);
    CHECK(ops->gettime(c, fd, &out) == -9 && ops->read(c, fd, value, 8) == -9);
    CHECK(ops->close(c, fd) == -9);
    return cases;
}
