// SPDX-License-Identifier: MIT
#ifndef ARTBOX_NATIVE_DLFCN_H
#define ARTBOX_NATIVE_DLFCN_H
#include "artbox/guest_dlfcn.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Bind the calling native thread while it executes this guest. Restore the
 * previous binding before destroying the thread object. Uses host TLS only. */
artbox_guest_dl_thread *artbox_native_dlfcn_swap(artbox_guest_dl_thread *thread);
/* Seven explicit unversioned imports of the pinned AOSP libdl frontend. */
artbox_elf_result artbox_native_dlfcn_resolve(void *context,
    const artbox_dynamic *dynamic, uint32_t index, uint64_t *address);
#ifdef __cplusplus
}
#endif
#endif
