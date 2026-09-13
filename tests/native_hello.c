#include "artbox/guest.h"
#include "artbox/native_memory.h"
#include "artbox/native_hello.h"
#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

typedef struct capture { char bytes[64]; size_t size; } capture;
static uint64_t entered_at;

static void demo_log(void *context, const char *message, size_t length) {
    capture *sink = context;
    if (length < sizeof(sink->bytes) - sink->size) {
        memcpy(sink->bytes + sink->size, message, length);
        sink->size += length;
        sink->bytes[sink->size++] = '\n';
    }
}

static uint64_t now(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) { perror("clock_gettime"); exit(2); }
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

static int64_t output(void *context, int fd, const void *bytes, size_t size) {
    capture *sink = context;
    if (fd != 1 || size > sizeof(sink->bytes) - sink->size) return -5;
    memcpy(sink->bytes + sink->size, bytes, size);
    sink->size += size;
    return (int64_t)size;
}

static void measured_enter(const void *entry, artbox_guest *guest,
                           artbox_dispatch_fn dispatch, void *stack) {
    entered_at = now();
    artbox_enter_arm64(entry, guest, dispatch, stack);
}

int main(int argc, char **argv) {
    static const char expected[] = "hello from Android ARM64\n";
    static const uint64_t numbers[] = {64, 93, 222, 226, 215};
    uint64_t samples[100], start, loaded, first_entry = 0, first_exit = 0;
    unsigned i;
    char *end;
    size_t image_size;
    void *library, *entry;
    artbox_guest *guest;
    artbox_memory_ops memory = artbox_native_memory();
    capture sink = {{0}, 0};
    struct rusage usage;
    if (argc != 3 && argc != 4) { fprintf(stderr, "usage: native_hello SIGNED_LIBRARY IMAGE_BYTES [--demo]\n"); return 2; }
    image_size = (size_t)strtoull(argv[2], &end, 10);
    if (*end || image_size < 4 || image_size > 1024u * 1024u) return 2;
    if (argc == 4) {
        artbox_host host = {demo_log, &sink};
        if (strcmp(argv[3], "--demo") || artbox_run_native_hello(argv[1], image_size, &host) != 0 ||
            sink.size != sizeof(expected) - 1 || memcmp(sink.bytes, expected, sizeof(expected) - 1)) return 1;
        puts("app entry: hello and five syscalls PASS");
        return 0;
    }
    start = now();
    library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    entry = dlsym(library, "artbox_guest_start");
    if (!entry) { fprintf(stderr, "dlsym: %s\n", dlerror()); return 1; }
    loaded = now();
    guest = artbox_guest_create(&memory, output, &sink, entry, image_size);
    if (!guest) { fprintf(stderr, "guest creation failed\n"); return 1; }
    for (i = 0; i < 100; ++i) {
        uint64_t begin, finish;
        int status;
        sink.size = 0;
        begin = now();
        status = artbox_guest_execute(guest, entry, measured_enter);
        finish = now();
        if (status != 0 || sink.size != sizeof(expected) - 1 ||
            memcmp(sink.bytes, expected, sizeof(expected) - 1) != 0) {
            fprintf(stderr, "native guest failed: status=%d, output bytes=%zu\n", status, sink.size);
            return 1;
        }
        samples[i] = finish - begin;
        if (i == 0) { first_entry = entered_at; first_exit = finish; }
    }
    for (i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
        if (artbox_guest_syscall_count(guest, numbers[i]) != 100) {
            fprintf(stderr, "unexpected syscall count for %" PRIu64 "\n", numbers[i]); return 1;
        }
    }
    if (getrusage(RUSAGE_SELF, &usage) != 0) { perror("getrusage"); return 1; }
    if (artbox_guest_destroy(guest) != 0 || dlclose(library) != 0) return 1;
    printf("{\"iterations\":100,\"exit_status\":0,\"output\":\"hello from Android ARM64\\n\","
           "\"each_syscall_count\":100,\"dlopen_to_symbol_ns\":%" PRIu64 ","
           "\"load_to_entry_bridge_ns\":%" PRIu64 ",\"load_to_first_exit_ns\":%" PRIu64 ","
           "\"process_peak_rss_bytes\":%ld,\"invocation_ns\":[",
           loaded - start, first_entry - start, first_exit - start, usage.ru_maxrss);
    for (i = 0; i < 100; ++i) printf("%s%" PRIu64, i ? "," : "", samples[i]);
    puts("]}");
    return 0;
}
