#ifndef ARTBOX_KERNEL_H
#define ARTBOX_KERNEL_H
#include "artbox/vm.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Linux ARM64 uses two signed 64-bit words, independently of host time_t. */
typedef struct artbox_timespec { int64_t seconds, nanoseconds; } artbox_timespec;
typedef struct artbox_system_ops {
    /* clock IDs here are Linux values: realtime=0, monotonic=1. */
    int (*clock)(unsigned clock_id, artbox_timespec *value);
    /* Fill a host buffer from the operating system CSPRNG; Linux errno on error. */
    int (*random)(void *buffer, size_t length);
} artbox_system_ops;

/* Borrowed address space, one descriptor per native guest thread. IDs belong
 * to the runtime's process namespace. The owner provides unique positive TIDs
 * and keeps this descriptor/thread binding alive through guest exit. */
typedef struct artbox_kernel_thread {
    artbox_vm *vm;
    artbox_system_ops system;
    int32_t pid, tid;
    uint64_t clear_tid_address;
} artbox_kernel_thread;

int artbox_kernel_thread_init(artbox_kernel_thread *thread, artbox_vm *vm,
                              const artbox_system_ops *system, int32_t pid, int32_t tid);
int64_t artbox_kernel_call(void *thread, uint64_t number, uint64_t a0, uint64_t a1,
                          uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
#ifdef __cplusplus
}
#endif
#endif
