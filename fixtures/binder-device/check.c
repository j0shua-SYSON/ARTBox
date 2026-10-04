/* Original Binder ioctl/lifetime expectations, shared with the future driver. MIT. */
#include "check.h"
#include "artbox/binder_wire.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { ++cases; if (!(x)) { fprintf(stderr, "Binder reference line %d: %s\n", __LINE__, #x); return -1; } } while (0)
#define PTR(p) ((uint64_t)(uintptr_t)(p))

int artbox_binder_device_check(void *context, const artbox_binder_device_ops *ops) {
    int cases = 0, a, b, attempts;
    int64_t result;
    uint32_t version[3] = {0xfeedface, 0, 0xc001cafe}, threads = 15;
    uint32_t commands[2] = {ARTBOX_BC_ENTER_LOOPER, UINT32_C(0xffffffff)};
    uint64_t write_read[6] = {0};
    /* flat_binder_object with TXN_SECURITY_CTX, null binder/cookie. */
    uint32_t manager[6] = {0, 0x1000, 0, 0, 0, 0};
    CHECK(sizeof(uintptr_t) == 8);
    a = ops->open(context); CHECK(a >= 0);
    b = ops->open(context); CHECK(b >= 0 && b != a);
    CHECK(ops->ioctl(context, a, ARTBOX_BINDER_VERSION, PTR(&version[1])) == 0);
    CHECK(version[1] == 8 && version[0] == 0xfeedface && version[2] == 0xc001cafe);
    /* BINDER_VERSION historically reports EINVAL for a failed put_user,
     * unlike the EFAULT from the copy-based requests below. */
    CHECK(ops->ioctl(context, a, ARTBOX_BINDER_VERSION, 1) == -22);
    CHECK(ops->ioctl(context, a, UINT32_C(0xffffffff), 1) == -22);
    CHECK(ops->ioctl(context, a, ARTBOX_BINDER_SET_MAX_THREADS, PTR(&threads)) == 0);
    threads = 0;
    CHECK(ops->ioctl(context, a, ARTBOX_BINDER_SET_MAX_THREADS, PTR(&threads)) == 0);
    threads = UINT32_MAX;
    CHECK(ops->ioctl(context, a, ARTBOX_BINDER_SET_MAX_THREADS, PTR(&threads)) == 0);
    CHECK(ops->ioctl(context, a, ARTBOX_BINDER_SET_MAX_THREADS, 1) == -14);
    CHECK(ops->ioctl(context, a, ARTBOX_BINDER_WRITE_READ, PTR(write_read)) == 0);
    CHECK(write_read[1] == 0 && write_read[4] == 0);
    CHECK(ops->ioctl(context, a, ARTBOX_BINDER_WRITE_READ, 1) == -14);
    write_read[0] = 8; write_read[2] = PTR(commands); write_read[4] = 37;
    CHECK(ops->ioctl(context, a, ARTBOX_BINDER_WRITE_READ, PTR(write_read)) == -22);
    CHECK(write_read[1] == 4 && write_read[4] == 0);
    /* Resume at the valid prefix, replacing the rejected command. */
    commands[1] = ARTBOX_BC_EXIT_LOOPER;
    CHECK(ops->ioctl(context, a, ARTBOX_BINDER_WRITE_READ, PTR(write_read)) == 0);
    CHECK(write_read[1] == 8 && write_read[4] == 0);
    CHECK(ops->ioctl(context, a, ARTBOX_BINDER_THREAD_EXIT, 0) == 0);
    version[1] = 0;
    CHECK(ops->ioctl(context, a, ARTBOX_BINDER_VERSION, PTR(&version[1])) == 0 && version[1] == 8);
    /* The legacy form ignores its nominal argument; EXT copies the object. */
    CHECK(ops->ioctl(context, a, ARTBOX_BINDER_SET_CONTEXT_MGR_EXT, 1) == -14);
    CHECK(ops->ioctl(context, a, ARTBOX_BINDER_SET_CONTEXT_MGR, 0) == 0);
    CHECK(ops->ioctl(context, a, ARTBOX_BINDER_SET_CONTEXT_MGR, 1) == -16);
    CHECK(ops->ioctl(context, b, ARTBOX_BINDER_SET_CONTEXT_MGR, 0) == -16);
    CHECK(ops->ioctl(context, b, ARTBOX_BINDER_SET_CONTEXT_MGR_EXT, 1) == -14);
    CHECK(ops->ioctl(context, b, ARTBOX_BINDER_SET_CONTEXT_MGR_EXT, PTR(manager)) == -16);
    CHECK(ops->close(context, a) == 0);
    /* Linux releases a closed Binder endpoint asynchronously. The same opener
     * UID can become manager afterward; do not require immediate reclamation. */
    result = -16;
    for (attempts = 0; attempts < 2000 && result == -16; ++attempts) {
        result = ops->ioctl(context, b, ARTBOX_BINDER_SET_CONTEXT_MGR_EXT, PTR(manager));
        if (result == -16) ops->pause(context);
    }
    CHECK(result == 0);
    CHECK(ops->ioctl(context, b, ARTBOX_BINDER_SET_CONTEXT_MGR, 0) == -16);
    CHECK(ops->close(context, b) == 0);
    return cases;
}
