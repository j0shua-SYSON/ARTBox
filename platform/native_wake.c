#include "artbox/native_wake.h"
#include <stdint.h>
#include <stdlib.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
struct native_wake { HANDLE event; };
static int native_error(DWORD error) {
    switch (error) {
    case ERROR_INVALID_HANDLE: return -9;
    case ERROR_INVALID_PARAMETER: return -22;
    case ERROR_TOO_MANY_OPEN_FILES: return -24;
    case ERROR_NOT_ENOUGH_MEMORY: case ERROR_OUTOFMEMORY: case ERROR_NO_SYSTEM_RESOURCES: return -12;
    case ERROR_ACCESS_DENIED: return -13;
    default: return -5;
    }
}
static int initialize(struct native_wake *owner) {
    owner->event = CreateEventW(NULL, FALSE, FALSE, NULL);
    return owner->event ? 0 : native_error(GetLastError());
}
static int wake_signal(void *opaque) {
    struct native_wake *owner = opaque;
    if (!owner) return -22;
    return SetEvent(owner->event) ? 0 : native_error(GetLastError());
}
static int wake_wait(void *opaque, int milliseconds) {
    struct native_wake *owner = opaque;
    if (!owner || milliseconds < -1) return -22;
    DWORD result = WaitForSingleObject(owner->event, milliseconds < 0 ? INFINITE : (DWORD)milliseconds);
    if (result == WAIT_OBJECT_0) return 1;
    if (result == WAIT_TIMEOUT) return 0;
    return result == WAIT_FAILED ? native_error(GetLastError()) : -5;
}
static int release(struct native_wake *owner) {
    return CloseHandle(owner->event) ? 0 : native_error(GetLastError());
}
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/event.h>
#include <time.h>
#elif defined(__linux__)
#include <poll.h>
#include <sys/eventfd.h>
#else
#error Native wake requires Darwin, Linux or Windows
#endif
struct native_wake { int fd; };
static int native_error(int error) {
    switch (error) {
    case EPERM: return -1;
    case EINTR: return -4;
    case EBADF: return -9;
    case EAGAIN: return -11;
    case ENOMEM: return -12;
    case EACCES: return -13;
    case EINVAL: return -22;
    case ENFILE: return -23;
    case EMFILE: return -24;
    default: return -5;
    }
}
#if defined(__APPLE__)
static int initialize(struct native_wake *owner) {
    owner->fd = kqueue();
    if (owner->fd < 0) return native_error(errno);
    struct kevent change;
    EV_SET(&change, 1, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, NULL);
    if (fcntl(owner->fd, F_SETFD, FD_CLOEXEC) < 0 || kevent(owner->fd, &change, 1, NULL, 0, NULL) < 0) {
        const int error = native_error(errno);
        close(owner->fd);
        return error;
    }
    return 0;
}
static int wake_signal(void *opaque) {
    struct native_wake *owner = opaque;
    if (!owner) return -22;
    struct kevent change;
    EV_SET(&change, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
    return kevent(owner->fd, &change, 1, NULL, 0, NULL) < 0 ? native_error(errno) : 0;
}
static int wake_wait(void *opaque, int milliseconds) {
    struct native_wake *owner = opaque;
    if (!owner || milliseconds < -1) return -22;
    struct timespec timeout = {0, 0};
    if (milliseconds >= 0) {
        timeout.tv_sec = milliseconds / 1000;
        timeout.tv_nsec = (milliseconds % 1000) * 1000000L;
    }
    struct kevent event;
    int count = kevent(owner->fd, NULL, 0, &event, 1, milliseconds < 0 ? NULL : &timeout);
    if (count < 0) return native_error(errno);
    if (!count) return 0;
    if (count != 1 || event.ident != 1 || event.filter != EVFILT_USER || (event.flags & EV_EOF)) return -5;
    if (event.flags & EV_ERROR) return -5;
    return 1;
}
#else
static int initialize(struct native_wake *owner) {
    owner->fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    return owner->fd < 0 ? native_error(errno) : 0;
}
static int wake_signal(void *opaque) {
    struct native_wake *owner = opaque;
    if (!owner) return -22;
    const uint64_t value = 1;
    ssize_t count = write(owner->fd, &value, sizeof(value));
    if (count == (ssize_t)sizeof(value)) return 0;
    // A full counter already represents a retained wake hint.
    return count < 0 ? (errno == EAGAIN ? 0 : native_error(errno)) : -5;
}
static int wake_wait(void *opaque, int milliseconds) {
    struct native_wake *owner = opaque;
    if (!owner || milliseconds < -1) return -22;
    struct pollfd watched = {owner->fd, POLLIN, 0};
    int count = poll(&watched, 1, milliseconds);
    if (count < 0) return native_error(errno);
    if (!count) return 0;
    if (watched.revents != POLLIN) return -5;
    uint64_t value = 0;
    ssize_t bytes = read(owner->fd, &value, sizeof(value));
    if (bytes < 0) return native_error(errno);
    return bytes == (ssize_t)sizeof(value) && value ? 1 : -5;
}
#endif
static int release(struct native_wake *owner) {
    // Do not retry close after EINTR: that descriptor could have been reused.
    return close(owner->fd) < 0 ? native_error(errno) : 0;
}
#endif
static int wake_create(void *context, void **output) {
    (void)context;
    if (!output) return -22;
    *output = NULL;
    struct native_wake *owner = malloc(sizeof(*owner));
    if (!owner) return -12;
    int result = initialize(owner);
    if (result) { free(owner); return result; }
    *output = owner;
    return 0;
}
static int wake_close(void *opaque) {
    struct native_wake *owner = opaque;
    if (!owner) return -22;
    int result = release(owner);
    free(owner);
    return result;
}
artbox_wake_ops artbox_native_wake(void) {
    const artbox_wake_ops ops = {NULL, wake_create, wake_signal, wake_wait, wake_close};
    return ops;
}
