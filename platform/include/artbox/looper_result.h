/* SPDX-License-Identifier: MIT */
#ifndef ARTBOX_LOOPER_RESULT_H
#define ARTBOX_LOOPER_RESULT_H
#include <stdint.h>
/* Fixed-width result shared by the Linux caller, Android image and Apple runner. */
struct artbox_looper_result {
    uint32_t cases;
    uint32_t failure;
    uint32_t wake_threads;
    uint32_t message_calls;
    uint32_t fd_callbacks;
    uint32_t timer_callbacks;
};
#endif
