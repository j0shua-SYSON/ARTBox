// SPDX-License-Identifier: MIT
#ifndef ARTBOX_SERVICE_ACCESS_H
#define ARTBOX_SERVICE_ACCESS_H
#include "artbox/service_policy.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Trusted bootstrap only, before publishing this service instance. The guest
 * image owns one policy for its process lifetime; successful configuration is
 * irreversible. A failed configuration leaves the default deny state intact. */
int artbox_service_access_configure(const artbox_service_rule *rules,size_t count);
/* Separate one-shot contract processes: 0 positive; 1 wrong client UID;
 * 2 wrong manager PID. A successful check leaves its diagnostic policy bound. */
int artbox_service_access_check(unsigned mutation);
#ifdef __cplusplus
}
#endif
#endif
