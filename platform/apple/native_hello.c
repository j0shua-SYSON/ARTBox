#include "artbox/native_hello.h"
#include "artbox/guest.h"
#include "artbox/native_memory.h"
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>

static int64_t console_output(void *context, int fd, const void *bytes, size_t count) {
    const artbox_host *host = context;
    const char *message = bytes;
    size_t visible = count;
    if (fd != 1 && fd != 2) return -9;
    /* The console accepts messages without their trailing line separator. */
    if (visible && message[visible - 1] == '\n') --visible;
    host->log(host->context, message, visible);
    return (int64_t)count;
}

int artbox_run_native_hello(const char *library, size_t image_size, const artbox_host *host) {
    static const uint64_t numbers[] = {64, 93, 222, 226, 215};
    void *handle, *entry;
    artbox_memory_ops memory = artbox_native_memory();
    artbox_guest *guest;
    unsigned i;
    int result;
    if (!library || !host || !host->log) return -1;
    handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char *error = dlerror();
        if (error) host->log(host->context, error, strlen(error));
        return -1;
    }
    entry = dlsym(handle, "artbox_guest_start");
    if (!entry) { dlclose(handle); return -1; }
    guest = artbox_guest_create(&memory, console_output, (void *)host, entry, image_size);
    if (!guest) { dlclose(handle); return -1; }
    result = artbox_guest_execute(guest, entry, artbox_enter_arm64);
    for (i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i)
        if (artbox_guest_syscall_count(guest, numbers[i]) != 1) result = -1;
    if (artbox_guest_destroy(guest) != 0) result = -1;
    if (dlclose(handle) != 0) result = -1;
    return result;
}
