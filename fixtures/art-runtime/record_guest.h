// Original Linux/Android fixture output adapter. SPDX-License-Identifier: MIT
#ifndef ARTBOX_RUNTIME_RECORD_GUEST_H
#define ARTBOX_RUNTIME_RECORD_GUEST_H
#include "record.h"
#include <unistd.h>
inline int64_t artbox_runtime_stdout(const void *bytes, size_t size) {
    return write(STDOUT_FILENO, bytes, size);
}
#define ARTBOX_RUNTIME_RECORD(...) artbox_runtime_record(artbox_runtime_stdout, __VA_ARGS__)
#endif
