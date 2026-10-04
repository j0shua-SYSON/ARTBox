#ifndef ARTBOX_WAKE_H
#define ARTBOX_WAKE_H
#ifdef __cplusplus
extern "C" {
#endif

/* Ordinary-context wake hints for a portable readiness loop. Each object has
 * one waiter and any number of concurrent signalers. A signal remains pending
 * until observed by wait; several signals may coalesce into one observation.
 * After every hint, the caller must recheck its own readiness predicate.
 *
 * create returns 0 and an owned, initially unsignaled object, or a negative
 * Linux errno and NULL. signal returns 0 or a negative Linux errno. wait uses
 * milliseconds (-1 infinite, 0 snapshot), returns 1 for a hint, 0 for timeout,
 * or a negative Linux errno, including EINTR. It must not hide interruption.
 *
 * Stop every waiter/signaler before close. No callback is signal-handler or
 * VM-watch safe. Native descriptors/handles never become guest descriptors. */
typedef struct artbox_wake_ops {
    void *context;
    int (*create)(void *context, void **owner);
    int (*signal)(void *owner);
    int (*wait)(void *owner, int milliseconds);
    int (*close)(void *owner);
} artbox_wake_ops;

#ifdef __cplusplus
}
#endif
#endif
