// Original public-pthread signal binding. SPDX-License-Identifier: MIT
#define _DARWIN_C_SOURCE 1
#include "artbox/native_signal_binding.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

_Static_assert(ATOMIC_POINTER_LOCK_FREE==2 && ATOMIC_INT_LOCK_FREE==2,"Signal binding atomics must be lock-free");
typedef struct signal_thread {
    void *context;
    _Atomic(const artbox_native_signal_scope *) active;
} signal_thread;
static pthread_once_t once=PTHREAD_ONCE_INIT;
static pthread_key_t key;
static _Atomic int ready;
static void initialize(void) {
    // No destructor: detach is explicit, after all guest handlers have left.
    if(!pthread_key_create(&key,NULL)) atomic_store_explicit(&ready,1,memory_order_release);
}
static signal_thread *current(void) {
    if(!atomic_load_explicit(&ready,memory_order_acquire)) return NULL;
    return pthread_getspecific(key);
}
int artbox_native_signal_attach(void *context) {
    if(pthread_once(&once,initialize) || !atomic_load_explicit(&ready,memory_order_acquire)) return -11;
    if(current()) return -17;
    signal_thread *thread=calloc(1,sizeof(*thread));
    if(!thread) return -12;
    thread->context=context; atomic_init(&thread->active,NULL);
    if(pthread_setspecific(key,thread)) { free(thread); return -11; }
    return 0;
}
int artbox_native_signal_detach(void) {
    signal_thread *thread=current();
    if(!thread) return -22;
    if(atomic_load_explicit(&thread->active,memory_order_acquire)) return -16;
    if(pthread_setspecific(key,NULL)) return -22;
    free(thread);
    return 0;
}
void *artbox_native_signal_thread_context(void) {
    signal_thread *thread=current();
    return thread ? thread->context : NULL;
}
int artbox_native_signal_scope_swap(const artbox_native_signal_scope *scope,
    const artbox_native_signal_scope **previous) {
    signal_thread *thread=current();
    if(!thread || !previous || (scope && !scope->syscall.dispatch)) return -22;
    *previous=atomic_exchange_explicit(&thread->active,scope,memory_order_acq_rel);
    return 0;
}
const artbox_native_signal_scope *artbox_native_signal_current(void) {
    signal_thread *thread=current();
    return thread ? atomic_load_explicit(&thread->active,memory_order_acquire) : NULL;
}
