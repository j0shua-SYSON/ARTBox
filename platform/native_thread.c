#include "artbox/native_thread.h"
#if defined(_WIN32)
static int start(void *stack, size_t size, void (*entry)(void *), void *arg, void **handle) {
    (void)stack; (void)size; (void)entry; (void)arg; (void)handle;
    return -95;
}
static int join(void *handle) { (void)handle; return -95; }
#else
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
typedef struct native_worker {
    pthread_t thread;
    void (*entry)(void *);
    void *arg;
} native_worker;
static int linux_error(int error) {
    switch (error) {
        case 0: return 0;
        case EINVAL: return -22;
        case EAGAIN: return -11;
        case ENOMEM: return -12;
        case EPERM: return -1;
        case ESRCH: return -3;
        case EDEADLK: return -35;
        default: return -5;
    }
}
static void *enter(void *argument) {
    native_worker *w = argument;
    w->entry(w->arg);
    return NULL;
}
static int start(void *stack, size_t size, void (*entry)(void *), void *arg, void **handle) {
    if (!stack || !size || !entry || !handle) return -22;
    native_worker *w = malloc(sizeof(*w));
    if (!w) return -12;
    w->entry = entry; w->arg = arg;
    pthread_attr_t attr;
    int error = pthread_attr_init(&attr);
    if (error) { free(w); return linux_error(error); }
    error = pthread_attr_setguardsize(&attr, 0); // VM owns guest guard pages.
    if (!error) error = pthread_attr_setstack(&attr, stack, size);
    if (!error) error = pthread_create(&w->thread, &attr, enter, w);
    (void)pthread_attr_destroy(&attr);
    if (error) { free(w); return linux_error(error); }
    *handle = w;
    return 0;
}
static int join(void *handle) {
    if (!handle) return -22;
    native_worker *w = handle;
    int error = pthread_join(w->thread, NULL);
    if (!error) free(w);
    return linux_error(error);
}
#endif
artbox_thread_ops artbox_native_threads(void) {
    const artbox_thread_ops ops = {start, join};
    return ops;
}
