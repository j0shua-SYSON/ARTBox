#include "artbox/devices.h"
// Compatibility facade for the device-only contracts and earlier fixtures.
extern "C" artbox_devices *artbox_devices_create(size_t limit) { return artbox_vfs_create(nullptr, limit); }
extern "C" void artbox_devices_destroy(artbox_devices *devices) { (void)artbox_vfs_destroy(devices); }
extern "C" int64_t artbox_devices_call(artbox_devices *devices, artbox_kernel_thread *thread,
    uint64_t number, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    return artbox_vfs_call(devices, thread, number, a0, a1, a2, a3);
}
