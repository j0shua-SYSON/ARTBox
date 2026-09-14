#ifndef ARTBOX_DEVICES_H
#define ARTBOX_DEVICES_H
#include "artbox/vfs.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef artbox_vfs artbox_devices;
/* Initial virtual descriptor table: null, zero and a read-only urandom device.
 * Descriptors start at 3. No host descriptor or host path is exposed to guests.
 * Share one table across a guest process; destroy only after its threads stop. */
artbox_devices *artbox_devices_create(size_t descriptor_limit);
void artbox_devices_destroy(artbox_devices *devices);
int64_t artbox_devices_call(artbox_devices *devices, artbox_kernel_thread *thread,
                            uint64_t number, uint64_t a0, uint64_t a1,
                            uint64_t a2, uint64_t a3);
#ifdef __cplusplus
}
#endif
#endif
