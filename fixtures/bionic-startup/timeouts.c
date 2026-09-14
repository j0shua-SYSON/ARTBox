// Same source is compiled against each platform's pthread ABI, MIT.
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <time.h>
#define CHECK(c) do { if (!(c)) return -__LINE__; ++cases; } while (0)
static int wait_until(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *deadline) {
    int result;
    do { result = pthread_cond_timedwait(cond, mutex, deadline); } while (!result);
    return result;
}
int artbox_timeout_check(void) {
    int cases = 0;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_condattr_t attr;
    struct timespec deadline = {0, 0};
    CHECK(pthread_mutex_init(&mutex, NULL) == 0);
    CHECK(pthread_cond_init(&cond, NULL) == 0);
    CHECK(pthread_mutex_lock(&mutex) == 0);
    CHECK(wait_until(&cond, &mutex, &deadline) == ETIMEDOUT);
    deadline.tv_nsec = 1000000000;
    CHECK(pthread_cond_timedwait(&cond, &mutex, &deadline) == EINVAL);
    CHECK(pthread_mutex_trylock(&mutex) == EBUSY);
    CHECK(pthread_cond_destroy(&cond) == 0);
    CHECK(pthread_condattr_init(&attr) == 0);
    CHECK(pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0);
    CHECK(pthread_cond_init(&cond, &attr) == 0);
    CHECK(clock_gettime(CLOCK_MONOTONIC, &deadline) == 0);
    deadline.tv_nsec += 1000000;
    if (deadline.tv_nsec >= 1000000000) { ++deadline.tv_sec; deadline.tv_nsec -= 1000000000; }
    CHECK(wait_until(&cond, &mutex, &deadline) == ETIMEDOUT);
    struct timespec finished;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &finished) == 0 &&
          (finished.tv_sec > deadline.tv_sec || (finished.tv_sec == deadline.tv_sec && finished.tv_nsec >= deadline.tv_nsec)));
    CHECK(pthread_mutex_trylock(&mutex) == EBUSY);
    CHECK(pthread_mutex_unlock(&mutex) == 0);
    CHECK(pthread_cond_destroy(&cond) == 0);
    CHECK(pthread_condattr_destroy(&attr) == 0);
    CHECK(pthread_mutex_destroy(&mutex) == 0);
    return cases;
}
