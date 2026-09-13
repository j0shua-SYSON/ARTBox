#ifndef ARTBOX_RUNTIME_H
#define ARTBOX_RUNTIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum artbox_result {
    ARTBOX_OK = 0,
    ARTBOX_INVALID_ARGUMENT = 1
} artbox_result;

/* Called synchronously on the caller's thread. Bytes are borrowed for the
 * duration of the call, without a trailing newline or NUL in the length.
 * The host owns the callback/context and must copy bytes it keeps. */
typedef void (*artbox_log_fn)(void *context, const char *message, size_t length);

typedef struct artbox_host {
    artbox_log_fn log;
    void *context;
} artbox_host;

/* M0 emits one startup message. No process, global state or allocation is
 * created. This does not start ART or execute any Android code. */
artbox_result artbox_start(const artbox_host *host);

#ifdef __cplusplus
}
#endif
#endif
