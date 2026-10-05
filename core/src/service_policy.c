/* Original immutable service authorization. SPDX-License-Identifier: MIT */
#include "artbox/service_policy.h"
#include <stdlib.h>
#include <string.h>
typedef struct stored_rule {
    int32_t pid;
    uint32_t uid;
    unsigned operations;
    size_t length;
    char name[128];
} stored_rule;
struct artbox_service_policy { size_t count; stored_rule *rules; };
static int valid_name(const char *name,size_t length) {
    if (!name || !length || length>127) return 0;
    for (size_t i=0;i<length;++i) {
        unsigned char c=(unsigned char)name[i];
        if (!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') ||
              c=='_' || c=='-' || c=='.' || c=='/')) return 0;
    }
    return 1;
}
int artbox_service_policy_create(const artbox_service_rule *rules,size_t count,artbox_service_policy **out) {
    if (!out) return -22;
    *out=NULL;
    if (count>1024 || (count && !rules)) return -22;
    artbox_service_policy *policy=calloc(1,sizeof(*policy));
    if (!policy) return -12;
    if (count) {
        policy->rules=calloc(count,sizeof(stored_rule));
        if (!policy->rules) { free(policy); return -12; }
    }
    for (size_t i=0;i<count;++i) {
        const artbox_service_rule *source=&rules[i];
        if (source->pid<=0 || source->uid==UINT32_MAX || !source->operations ||
            (source->operations & ~7u)) goto invalid;
        if (source->operations & ARTBOX_SERVICE_LIST) {
            if (source->operations!=ARTBOX_SERVICE_LIST || source->name_length) goto invalid;
        } else if (!valid_name(source->name,source->name_length)) goto invalid;
        stored_rule *rule=&policy->rules[i];
        rule->pid=source->pid; rule->uid=source->uid; rule->operations=source->operations;
        rule->length=source->name_length;
        if (rule->length) memcpy(rule->name,source->name,rule->length);
        for (size_t j=0;j<i;++j) {
            const stored_rule *other=&policy->rules[j];
            if (rule->pid==other->pid && rule->uid==other->uid && rule->length==other->length &&
                !memcmp(rule->name,other->name,rule->length)) goto invalid;
        }
    }
    policy->count=count; *out=policy;
    return 0;
invalid:
    free(policy->rules);
    free(policy);
    return -22;
}
int artbox_service_policy_allows(const artbox_service_policy *policy,int32_t pid,uint32_t uid,
    unsigned operation,const char *name,size_t length) {
    if (!policy || pid<=0 || uid==UINT32_MAX) return 0;
    if (operation==ARTBOX_SERVICE_LIST) {
        if (length) return 0;
    } else if ((operation!=ARTBOX_SERVICE_FIND && operation!=ARTBOX_SERVICE_ADD) || !valid_name(name,length)) return 0;
    for (size_t i=0;i<policy->count;++i) {
        const stored_rule *rule=&policy->rules[i];
        if (rule->pid==pid && rule->uid==uid && (rule->operations & operation) && rule->length==length &&
            (!length || !memcmp(rule->name,name,length))) return 1;
    }
    return 0;
}
void artbox_service_policy_destroy(artbox_service_policy *policy) {
    if (policy) { free(policy->rules); free(policy); }
}
