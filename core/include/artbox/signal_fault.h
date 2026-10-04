// SPDX-License-Identifier: MIT
#ifndef ARTBOX_SIGNAL_FAULT_H
#define ARTBOX_SIGNAL_FAULT_H
#include "artbox/signal_context.h"
#include "artbox/vm.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { ARTBOX_FAULT_DATA=1, ARTBOX_FAULT_UNDEFINED=2, ARTBOX_FAULT_BREAKPOINT=3 };
typedef struct artbox_signal_fault { unsigned number,code; uint64_t address; } artbox_signal_fault;
/* Classify measured ARM64 synchronous faults into Linux signal/si_code values.
 * DATA accepts lower-EL translation/permission/alignment faults. Translation
 * and permission faults require a coherent VM snapshot; alignment needs none.
 * A mapped address with sufficient permissions is not classified as SEGV:
 * file truncation, external aborts, MTE and other cases require separate support.
 * UNDEFINED currently covers the UDF encoding, BREAKPOINT the BRK encoding.
 * The platform owner validates the native event, signed PC and fault address.
 * No allocation, locking or guest-address dereference occurs here. Output is unchanged
 * on failure; unsupported syndromes/encodings return ENOTSUP. */
int artbox_signal_classify_fault(unsigned kind,uint32_t instruction,
    const artbox_arm64_signal_state *state,const artbox_vm_fault_info *memory,
    artbox_signal_fault *out);
#ifdef __cplusplus
}
#endif
#endif
