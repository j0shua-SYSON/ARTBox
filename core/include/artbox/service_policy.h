/* Original fixed service authorization policy. SPDX-License-Identifier: MIT */
#ifndef ARTBOX_SERVICE_POLICY_H
#define ARTBOX_SERVICE_POLICY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct artbox_service_policy artbox_service_policy;
enum { ARTBOX_SERVICE_FIND=1, ARTBOX_SERVICE_ADD=2, ARTBOX_SERVICE_LIST=4 };
typedef struct artbox_service_rule {
    int32_t pid;
    uint32_t uid;
    unsigned operations;
    const char *name;
    size_t name_length;
} artbox_service_rule;
/* Copy at most 1024 trusted rules into an immutable policy. Names have 1..127
 * ASCII letters/digits or _-./ bytes; list permission is a separate rule with
 * zero name length. Duplicate (PID, UID, name) entries are invalid. Empty
 * configuration denies everything, including root. Returns negative errno and
 * clears *out on failure. No wildcard, implicit privilege or SELinux claim. */
int artbox_service_policy_create(const artbox_service_rule *rules,size_t count,artbox_service_policy **out);
/* Check one operation against a trusted Binder calling PID/UID, never identity
 * encoded in Parcel payload. Return 1 only for an exact grant, otherwise 0.
 * No allocation or mutation; concurrent queries are allowed until destroy.
 * Caller owns readable name bytes. The adapter must copy/validate guest input
 * before crossing into a host policy. This is not native-code memory isolation. */
int artbox_service_policy_allows(const artbox_service_policy *policy,int32_t pid,uint32_t uid,
    unsigned operation,const char *name,size_t name_length);
/* Stop queries before destruction. NULL is allowed. */
void artbox_service_policy_destroy(artbox_service_policy *policy);
#ifdef __cplusplus
}
#endif
#endif
