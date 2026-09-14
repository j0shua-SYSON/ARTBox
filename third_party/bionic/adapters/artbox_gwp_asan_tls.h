// ARTBox integration header, MIT. Included by GWP-ASan's platform TLS hook
// after it defines ThreadLocalPackedVariables; no upstream source is copied.
#ifndef ARTBOX_GWP_ASAN_TLS_H
#define ARTBOX_GWP_ASAN_TLS_H
#include <bionic/tls.h>
#include <bionic/tls_defines.h>

namespace gwp_asan {
inline ThreadLocalPackedVariables* getThreadLocals() {
    // The runtime owns one initialized, aligned object per guest thread and
    // binds its address before allocator entry. This slot belongs to ARTBox;
    // it neither replaces another Android TLS slot nor allocates recursively.
    return static_cast<ThreadLocalPackedVariables*>(
        __get_tls()[TLS_SLOT_NATIVE_BRIDGE_GUEST_STATE]);
}
}  // namespace gwp_asan
#endif
