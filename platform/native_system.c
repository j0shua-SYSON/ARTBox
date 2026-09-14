#include "artbox/native_system.h"
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

static int random_bytes(void *buffer, size_t length) {
    if (length > UINT32_MAX) return -22;
    return BCryptGenRandom(NULL, buffer, (ULONG)length, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? 0 : -5;
}
static int clock_value(unsigned id, artbox_timespec *value) {
    if (id == 0) {
        FILETIME filetime;
        GetSystemTimePreciseAsFileTime(&filetime);
        uint64_t ticks = ((uint64_t)filetime.dwHighDateTime << 32) | filetime.dwLowDateTime;
        if (ticks > INT64_MAX) return -5;
        int64_t unix_ticks = (int64_t)ticks - INT64_C(116444736000000000);
        value->seconds = unix_ticks / 10000000;
        int64_t remainder = unix_ticks % 10000000;
        if (remainder < 0) { --value->seconds; remainder += 10000000; }
        value->nanoseconds = remainder * 100;
        return 0;
    }
    if (id == 1) {
        LARGE_INTEGER ticks, frequency;
        if (!QueryPerformanceCounter(&ticks) || !QueryPerformanceFrequency(&frequency) ||
            ticks.QuadPart < 0 || frequency.QuadPart <= 0 ||
            (uint64_t)frequency.QuadPart > UINT64_MAX / UINT64_C(1000000000)) return -5;
        value->seconds = ticks.QuadPart / frequency.QuadPart;
        value->nanoseconds = (int64_t)((uint64_t)(ticks.QuadPart % frequency.QuadPart) * UINT64_C(1000000000) /
                                      (uint64_t)frequency.QuadPart);
        return 0;
    }
    return -22;
}
#else
#include <errno.h>
#include <time.h>
#if defined(__linux__)
#include <sys/random.h>
static int random_bytes(void *buffer, size_t length) {
    unsigned char *bytes = buffer;
    while (length) {
        ssize_t count = getrandom(bytes, length, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -5;
        bytes += (size_t)count;
        length -= (size_t)count;
    }
    return 0;
}
#else
#include <stdlib.h>
static int random_bytes(void *buffer, size_t length) {
    arc4random_buf(buffer, length);
    return 0;
}
#endif
static int clock_value(unsigned id, artbox_timespec *value) {
    if (id > 1) return -22;
    struct timespec native;
    if (clock_gettime(id == 0 ? CLOCK_REALTIME : CLOCK_MONOTONIC, &native)) return errno == EINVAL ? -22 : -5;
    value->seconds = native.tv_sec;
    value->nanoseconds = native.tv_nsec;
    return 0;
}
#endif

artbox_system_ops artbox_native_system(void) {
    artbox_system_ops result = {clock_value, random_bytes};
    return result;
}
