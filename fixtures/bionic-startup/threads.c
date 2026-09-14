// Original NDK pthread client, MIT. Uses real Bionic pthreads and allocator.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define ASSERT(c) do { if (!(c)) return (void *)(intptr_t)-__LINE__; } while (0)
#define CHECK(c) do { if (!(c)) return -__LINE__; } while (0)
extern uint64_t artbox_bootstrap_is_guarded(const void *);
extern int artbox_tls_check(unsigned, unsigned);
enum { JOINED = 4, DETACHED = 2, ITERATIONS = 32 };
static pthread_mutex_t lock;
static pthread_cond_t changed;
static pthread_key_t key;
static unsigned ready, go, destructors, counter;
static _Atomic unsigned destructor_errors;
struct argument { unsigned index, guarded; pid_t tid; void *self; int *errno_address; };
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
    ASSERT(artbox_tls_check(a->index, 0) == 0);
    a->tid = gettid(); a->self = (void *)pthread_self(); a->errno_address = &errno;
    ASSERT(a->tid > 10000 && getpid() == 10000 && a->self != NULL);
    ASSERT(pthread_setspecific(key, a) == 0 && pthread_getspecific(key) == a);
    sigset_t mask, add;
    ASSERT(pthread_sigmask(SIG_SETMASK, NULL, &mask) == 0);
    ASSERT(sigismember(&mask, SIGUSR1) == 1 && sigismember(&mask, SIGUSR2) == 0);
    ASSERT(sigemptyset(&add) == 0 && sigaddset(&add, SIGUSR2) == 0);
    ASSERT(pthread_sigmask(SIG_BLOCK, &add, NULL) == 0);
    ASSERT(pthread_sigmask(SIG_SETMASK, NULL, &mask) == 0 && sigismember(&mask, SIGUSR2) == 1);
    char path[64];
    ASSERT(snprintf(path, sizeof(path), "data/thread-%u", a->index) > 0);
    int fd = openat(AT_FDCWD, path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    ASSERT(fd >= 3);
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
        a->guarded += (unsigned)artbox_bootstrap_is_guarded(p);
        memset(p, (int)a->index, size);
        unsigned char *q = realloc(p, size * 2);
        ASSERT(q != NULL);
        for (size_t j = 0; j < size; ++j) ASSERT(q[j] == a->index);
        free(q);
        uint32_t record = a->index * 1000 + i;
        ASSERT(write(fd, &record, sizeof(record)) == sizeof(record));
        ASSERT(pthread_getspecific(key) == a && &errno == a->errno_address);
        // Successful allocation may change errno. Pthread synchronization must
        // preserve this thread's distinct value across competing workers.
        errno = (int)(200 + a->index);
        ASSERT(pthread_mutex_lock(&lock) == 0);
        ++counter;
        ASSERT(pthread_mutex_unlock(&lock) == 0);
        ASSERT(errno == (int)(200 + a->index));
    }
    struct stat st;
    ASSERT(fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == ITERATIONS * 4);
    ASSERT(lseek(fd, 0, SEEK_SET) == 0);
    for (unsigned i = 0; i < ITERATIONS; ++i) {
        uint32_t record;
        ASSERT(read(fd, &record, sizeof(record)) == sizeof(record) && record == a->index * 1000 + i);
    }
    ASSERT(close(fd) == 0);
    ASSERT(artbox_tls_check(a->index, 1) == 0);
    void *result = (void *)(uintptr_t)(a->index + 1);
    if (a->index & 1) pthread_exit(result);
    return result;
}
int artbox_pthread_check(unsigned require_guarded) {
    CHECK(artbox_tls_check(77, 0) == 0);
    pthread_t handles[JOINED];
    sigset_t previous_mask, mask;
    CHECK(sigemptyset(&mask) == 0 && sigaddset(&mask, SIGUSR1) == 0);
    CHECK(pthread_sigmask(SIG_SETMASK, &mask, &previous_mask) == 0);
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
    if (require_guarded) for (unsigned i = 0; i < JOINED + DETACHED; ++i) CHECK(arguments[i].guarded > 0);
    CHECK(pthread_mutex_unlock(&lock) == 0);
    CHECK(atomic_load_explicit(&destructor_errors, memory_order_relaxed) == 0);
    CHECK(errno == 123 && pthread_getspecific(key) == &ready);
    CHECK(pthread_sigmask(SIG_SETMASK, NULL, &mask) == 0 && sigismember(&mask, SIGUSR1) == 1 && sigismember(&mask, SIGUSR2) == 0);
    CHECK(pthread_sigmask(SIG_SETMASK, &previous_mask, NULL) == 0);
    CHECK(pthread_key_delete(key) == 0 && pthread_cond_destroy(&changed) == 0 && pthread_mutex_destroy(&lock) == 0);
    CHECK(artbox_tls_check(77, 1) == 0);
    return 0;
}

uint64_t artbox_pthread_guarded_samples(void) {
    uint64_t count = 0;
    for (unsigned i = 0; i < JOINED + DETACHED; ++i) count += arguments[i].guarded;
    return count;
}
