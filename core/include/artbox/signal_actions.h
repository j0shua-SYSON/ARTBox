// SPDX-License-Identifier: MIT
#ifndef ARTBOX_SIGNAL_ACTIONS_H
#define ARTBOX_SIGNAL_ACTIONS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct artbox_signal_action {
    uint64_t handler,flags,restorer,mask;
} artbox_signal_action;
typedef struct artbox_signal_actions artbox_signal_actions;
/* Preallocate immutable action records. Writers serialize in normal context;
 * handler snapshots never allocate or lock. Published records remain alive
 * until destroy, which requires every writer and handler to have stopped.
 * Exhaustion returns -ENOMEM without changing an action or output. */
artbox_signal_actions *artbox_signal_actions_create(size_t capacity);
void artbox_signal_actions_destroy(artbox_signal_actions *actions);
/* Query when input is NULL; otherwise publish and return the previous action.
 * Filter unmaskable mask bits. The syscall boundary owns flag capabilities and
 * handler-address validation. Arguments are trusted host buffers, not guest
 * addresses. Queries for SIGKILL/SIGSTOP are valid; changes are rejected. */
int artbox_signal_actions_set(artbox_signal_actions *actions,unsigned number,
    const artbox_signal_action *input,artbox_signal_action *previous);
/* Signal-context operations. Snapshot observes one complete immutable record;
 * reset atomically restores SIG_DFL without taking the writer lock. */
int artbox_signal_actions_snapshot(const artbox_signal_actions *actions,unsigned number,
    artbox_signal_action *output);
int artbox_signal_actions_reset(artbox_signal_actions *actions,unsigned number,
    artbox_signal_action *previous);
#ifdef __cplusplus
}
#endif
#endif
