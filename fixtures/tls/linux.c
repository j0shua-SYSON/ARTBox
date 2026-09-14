// Paired native Linux ELF TLS oracle, MIT.
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
extern int artbox_tls_check(unsigned, unsigned);
extern int artbox_tls_abi_check(void);
static pthread_barrier_t barrier;
static void *worker(void *value) {
    unsigned id = (unsigned)(uintptr_t)value;
    int result = artbox_tls_check(id, 0);
    int wait = pthread_barrier_wait(&barrier);
    if (wait != 0 && wait != PTHREAD_BARRIER_SERIAL_THREAD) result = -8;
    if (!result) result = artbox_tls_check(id, 1);
    return (void *)(intptr_t)result;
}
int main(void) {
    pthread_t threads[6];
    if (artbox_tls_abi_check() || artbox_tls_abi_check()) return 5;
    if (artbox_tls_check(77, 0) || pthread_barrier_init(&barrier, NULL, 6)) return 1;
    for (unsigned i = 0; i < 6; ++i) if (pthread_create(&threads[i], NULL, worker, (void *)(uintptr_t)i)) return 2;
    for (unsigned i = 0; i < 6; ++i) {
        void *result;
        if (pthread_join(threads[i], &result) || result) return 3;
    }
    if (artbox_tls_check(77, 1) || pthread_barrier_destroy(&barrier)) return 4;
    puts("{\"tls_threads\":7,\"tls_modules\":2,\"tls_result\":0}");
    return 0;
}
