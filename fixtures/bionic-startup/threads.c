// Original NDK pthread client, MIT. Uses real Bionic pthreads and allocator.
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define ASSERT(c) do { if (!(c)) return (void *)(intptr_t)-__LINE__; } while (0)
#define CHECK(c) do { if (!(c)) return -__LINE__; } while (0)
enum { JOINED = 4, DETACHED = 2, ITERATIONS = 32 };
static pthread_mutex_t lock;
static pthread_cond_t changed;
static pthread_key_t key;
static unsigned ready, go, destructors, counter;
static _Atomic unsigned destructor_errors;
struct argument { unsigned index; pid_t tid; void *self; int *errno_address; };
static struct argument arguments[JOINED + DETACHED];
static void destroy_value(void *value) {
    struct argument *a = value;
    if (pthread_getspecific(key) != NULL || gettid() != a->tid || (void *)pthread_self() != a->self)
        atomic_fetch_add_explicit(&destructor_errors, 1, memory_order_relaxed);
    if (pthread_mutex_lock(&lock)) { atomic_fetch_add_explicit(&destructor_errors, 1, memory_order_relaxed); return; }
    ++destructors;
    if (pthread_cond_broadcast(&changed)) atomic_fetch_add_explicit(&destructor_errors, 1, memory_order_relaxed);
    if (pthread_mutex_unlock(&lock)) atomic_fetch_add_explicit(&destructor_errors, 1, memory_order_relaxed);
}
static void *worker(void *value) {
    struct argument *a = value;
    a->tid = gettid(); a->self = (void *)pthread_self(); a->errno_address = &errno;
    ASSERT(a->tid > 10000 && getpid() == 10000 && a->self != NULL);
    ASSERT(pthread_setspecific(key, a) == 0 && pthread_getspecific(key) == a);
    errno = (int)(200 + a->index);
    ASSERT(pthread_mutex_lock(&lock) == 0);
    ++ready;
    ASSERT(pthread_cond_broadcast(&changed) == 0);
    while (!go) ASSERT(pthread_cond_wait(&changed, &lock) == 0);
    ASSERT(pthread_mutex_unlock(&lock) == 0);
    ASSERT(errno == (int)(200 + a->index));
    for (unsigned i = 0; i < ITERATIONS; ++i) {
        size_t size = (i + 1) * 37;
        unsigned char *p = malloc(size);
        ASSERT(p != NULL);
        memset(p, (int)a->index, size);
        unsigned char *q = realloc(p, size * 2);
        ASSERT(q != NULL);
        for (size_t j = 0; j < size; ++j) ASSERT(q[j] == a->index);
        free(q);
        ASSERT(pthread_getspecific(key) == a && &errno == a->errno_address);
        // Successful allocation may change errno. Pthread synchronization must
        // preserve this thread's distinct value across competing workers.
        errno = (int)(200 + a->index);
        ASSERT(pthread_mutex_lock(&lock) == 0);
        ++counter;
        ASSERT(pthread_mutex_unlock(&lock) == 0);
        ASSERT(errno == (int)(200 + a->index));
    }
    void *result = (void *)(uintptr_t)(a->index + 1);
    if (a->index & 1) pthread_exit(result);
    return result;
}
int artbox_pthread_check(void) {
    pthread_t handles[JOINED];
    CHECK(pthread_mutex_init(&lock, NULL) == 0);
    CHECK(pthread_cond_init(&changed, NULL) == 0);
    CHECK(pthread_key_create(&key, destroy_value) == 0);
    CHECK(pthread_setspecific(key, &ready) == 0);
    errno = 123;
    CHECK(pthread_mutex_lock(&lock) == 0);
    for (unsigned i = 0; i < JOINED; ++i) {
        arguments[i].index = i;
        CHECK(pthread_create(&handles[i], NULL, worker, &arguments[i]) == 0);
    }
    while (ready != JOINED) CHECK(pthread_cond_wait(&changed, &lock) == 0);
    go = 1;
    CHECK(pthread_cond_broadcast(&changed) == 0 && pthread_mutex_unlock(&lock) == 0);
    for (unsigned i = 0; i < JOINED; ++i) {
        void *result = NULL;
        CHECK(pthread_join(handles[i], &result) == 0);
        if ((intptr_t)result < 0) return (int)(intptr_t)result; // Preserve the worker's failing source line.
        CHECK(result == (void *)(uintptr_t)(i + 1));
        CHECK(arguments[i].errno_address != &errno);
        for (unsigned j = 0; j < i; ++j) {
            CHECK(arguments[i].tid != arguments[j].tid);
            CHECK(arguments[i].errno_address != arguments[j].errno_address);
        }
    }
    CHECK(destructors == JOINED && counter == JOINED * ITERATIONS);
    pthread_attr_t attr;
    CHECK(pthread_attr_init(&attr) == 0 && pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) == 0);
    for (unsigned i = JOINED; i < JOINED + DETACHED; ++i) {
        pthread_t detached;
        arguments[i].index = i;
        CHECK(pthread_create(&detached, &attr, worker, &arguments[i]) == 0);
    }
    CHECK(pthread_attr_destroy(&attr) == 0 && pthread_mutex_lock(&lock) == 0);
    while (destructors != JOINED + DETACHED) CHECK(pthread_cond_wait(&changed, &lock) == 0);
    CHECK(counter == (JOINED + DETACHED) * ITERATIONS);
    CHECK(pthread_mutex_unlock(&lock) == 0);
    CHECK(atomic_load_explicit(&destructor_errors, memory_order_relaxed) == 0);
    CHECK(errno == 123 && pthread_getspecific(key) == &ready);
    CHECK(pthread_key_delete(key) == 0 && pthread_cond_destroy(&changed) == 0 && pthread_mutex_destroy(&lock) == 0);
    return 0;
}
