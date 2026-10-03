// SPDX-License-Identifier: MIT
#ifndef ARTBOX_SIGNAL_CONTEXT_H
#define ARTBOX_SIGNAL_CONTEXT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { ARTBOX_ARM64_UCONTEXT_BYTES = 4560 };
typedef struct artbox_arm64_signal_state {
    uint64_t x[31], sp, pc, pstate, fault_address, esr;
    uint32_t fpsr, fpcr;
    unsigned char v[32][16];
} artbox_arm64_signal_state;
typedef struct artbox_signal_frame_info {
    uint64_t mask, stack_address, stack_size;
    uint32_t stack_flags;
} artbox_signal_frame_info;

/* Encode the Linux ARM64 ucontext wire format in caller-owned storage. This is
 * data conversion only: no handler installation, allocation, locks or syscalls.
 * The storage may be unaligned; inputs and outputs must not overlap. */
int artbox_signal_context_encode(void *frame, size_t size,
    const artbox_arm64_signal_state *state, const artbox_signal_frame_info *info);

/* Read handler edits from a frame produced above. Preserve interrupted x18
 * and all PSTATE bits except NZCV. Strip SIGKILL/SIGSTOP from the returned mask.
 * Accept only the emitted FPSIMD/ESR record layout; SVE/SME/extra state requires
 * an explicit future adapter. Reject invalid PC/SP alignment and leave both
 * outputs unchanged on failure. This does not validate guest code/stack ranges
 * or apply anything to a host context; the platform bridge owns those checks. */
int artbox_signal_context_resume(const void *frame, size_t size,
    const artbox_arm64_signal_state *interrupted, artbox_arm64_signal_state *resume,
    uint64_t *mask);
#ifdef __cplusplus
}
#endif
#endif
