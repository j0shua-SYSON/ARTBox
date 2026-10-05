// Original unsupported Linux-policy query. SPDX-License-Identifier: MIT
#include <errno.h>
// Match libselinux's error ABI without inventing a policy version. VINTF logs
// the failed fetch and retains its previous/default value; its public aggregate
// fetch still returns OK. ARTBox grants do not imply a kernel SELinux policy.
int security_policyvers(void) { errno=ENOTSUP; return -1; }
