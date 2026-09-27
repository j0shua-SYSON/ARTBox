// SPDX-License-Identifier: MIT
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>

struct rendezvous {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    unsigned ready, release, cases;
    int error;
    int (*stage)(unsigned);
};
static void *worker(void *opaque) {
    struct rendezvous *s = opaque;
    if (s->stage(2) || s->stage(0)) s->error = 1;
    else s->cases = 2;
    pthread_mutex_lock(&s->mutex);
    s->ready = 1;
    pthread_cond_broadcast(&s->condition);
    while (!s->release) pthread_cond_wait(&s->condition, &s->mutex);
    pthread_mutex_unlock(&s->mutex);
    if (s->stage(1)) s->error = 1;
    else ++s->cases;
    return NULL;
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    int (*check)(void) = (int (*)(void))dlsym(library, "artbox_loader_check");
    int (*stage)(unsigned) = (int (*)(unsigned))dlsym(library, "artbox_loader_error_stage");
    if (!check || !stage) return 1;
    int result = check();
    if (result != 32) { fprintf(stderr, "loader case failed: %d\n", result); return 1; }
    struct rendezvous s = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0, 0, 0, stage};
    pthread_t thread;
    if (pthread_create(&thread, NULL, worker, &s)) return 1;
    pthread_mutex_lock(&s.mutex);
    while (!s.ready) pthread_cond_wait(&s.condition, &s.mutex);
    int main_ok = !stage(0) && !stage(1) && !stage(2);
    s.release = 1;
    pthread_cond_broadcast(&s.condition);
    pthread_mutex_unlock(&s.mutex);
    if (pthread_join(thread, NULL) || !main_ok || s.error || s.cases != 3) return 1;
    if (pthread_cond_destroy(&s.condition) || pthread_mutex_destroy(&s.mutex) || dlclose(library)) return 1;
    puts("{\"cases\":32,\"thread_error_checks\":6,\"cleanup\":true}");
    return 0;
}
