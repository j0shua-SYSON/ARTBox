/* SPDX-License-Identifier: MIT */
#ifndef ARTBOX_LOOPER_CHECK_H
#define ARTBOX_LOOPER_CHECK_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct artbox_looper_result {
    uint32_t cases;
    uint32_t failure;
    uint32_t wake_threads;
    uint32_t message_calls;
    uint32_t fd_callbacks;
    uint32_t timer_callbacks;
};
/* 0: contract; 1: retain a self-removing callback; 2: omit the worker wake. */
int artbox_native_looper_check(unsigned mutation, struct artbox_looper_result* result);
#ifdef __cplusplus
}
#endif
#endif
