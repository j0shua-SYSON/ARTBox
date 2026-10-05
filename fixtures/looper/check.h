/* SPDX-License-Identifier: MIT */
#ifndef ARTBOX_LOOPER_CHECK_H
#define ARTBOX_LOOPER_CHECK_H
#include "../../platform/include/artbox/looper_result.h"
#ifdef __cplusplus
extern "C" {
#endif
/* 0: contract; 1: retain a self-removing callback; 2: omit the worker wake. */
int artbox_native_looper_check(unsigned mutation, struct artbox_looper_result* result);
#ifdef __cplusplus
}
#endif
#endif
