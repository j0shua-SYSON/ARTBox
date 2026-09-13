#include "artbox/runtime.h"

#include <stdio.h>
#include <string.h>

typedef struct capture {
    unsigned calls;
    size_t size;
    char message[64];
    int overflow;
} capture;

static void capture_log(void *opaque, const char *message, size_t size) {
    capture *log = opaque;
    ++log->calls;
    log->size = size;
    if (size > sizeof(log->message)) {
        log->overflow = 1;
        return;
    }
    memcpy(log->message, message, size);
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

int main(void) {
    capture first = {0};
    capture second = {0};
    const artbox_host first_host = {capture_log, &first};
    const artbox_host second_host = {capture_log, &second};
    const artbox_host missing_callback = {NULL, &first};

    CHECK(artbox_start(NULL) == ARTBOX_INVALID_ARGUMENT);
    CHECK(artbox_start(&missing_callback) == ARTBOX_INVALID_ARGUMENT);
    CHECK(first.calls == 0);

    CHECK(artbox_start(&first_host) == ARTBOX_OK);
    CHECK(first.calls == 1);
    CHECK(first.overflow == 0);
    CHECK(first.size == sizeof("ARTBox ready") - 1);
    CHECK(memcmp(first.message, "ARTBox ready", first.size) == 0);

    CHECK(artbox_start(&second_host) == ARTBOX_OK);
    CHECK(first.calls == 1);
    CHECK(second.calls == 1);
    CHECK(second.size == first.size);
    CHECK(memcmp(second.message, first.message, first.size) == 0);

    puts("startup: exact log, synchronous callback, isolated hosts, invalid arguments PASS");
    return 0;
}
