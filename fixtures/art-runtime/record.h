// Original acceptance record framing. SPDX-License-Identifier: MIT
#ifndef ARTBOX_RUNTIME_RECORD_H
#define ARTBOX_RUNTIME_RECORD_H
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
using artbox_runtime_writer = int64_t (*)(const void *, size_t);
// One console write owns both boundaries. Bionic puts may split its text and
// newline, allowing a different thread's diagnostic to corrupt a phase line.
// The leading boundary also isolates a prior unfinished diagnostic fragment.
#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
inline bool artbox_runtime_record(artbox_runtime_writer writer, const char *format, ...) {
    if (!writer || !format) return false;
    char bytes[1024]; bytes[0] = '\n';
    va_list args; va_start(args, format);
    int count = std::vsnprintf(bytes + 1, sizeof(bytes) - 1, format, args);
    va_end(args);
    if (count < 0 || static_cast<size_t>(count) >= sizeof(bytes) - 1) return false;
    bytes[count + 1] = '\n';
    const size_t size = static_cast<size_t>(count) + 2;
    return writer(bytes, size) == static_cast<int64_t>(size);
}
#endif
