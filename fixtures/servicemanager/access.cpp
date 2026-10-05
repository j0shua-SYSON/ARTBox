// Original ARTBox service-policy adapter. SPDX-License-Identifier: MIT
#include "artbox_access.h"
#include "Access.h"
#include <binder/IPCThreadState.h>

// One immutable policy per separately loaded guest service image. The trusted
// owner configures it before any role thread can dispatch a Binder request.
static artbox_service_policy *policy;
extern "C" int artbox_service_access_configure(const artbox_service_rule *rules,size_t count) {
    if (policy) return -114;
    return artbox_service_policy_create(rules,count,&policy);
}
namespace android {
Access::Access() = default;
Access::~Access() = default;
std::string Access::CallingContext::toDebugString() const {
    return "ARTBox caller pid="+std::to_string(debugPid)+",uid="+std::to_string(uid);
}
Access::CallingContext Access::getCallingContext() {
    auto *ipc=IPCThreadState::self();
    // These fields are set by libbinder from the driver's transaction header.
    // No identity from service Parcel arguments and no fabricated SELinux SID.
    return {ipc->getCallingPid(),ipc->getCallingUid(),{}};
}
bool Access::canFind(const CallingContext &caller,const std::string &name) {
    return artbox_service_policy_allows(policy,caller.debugPid,caller.uid,
        ARTBOX_SERVICE_FIND,name.data(),name.size());
}
bool Access::canAdd(const CallingContext &caller,const std::string &name) {
    return artbox_service_policy_allows(policy,caller.debugPid,caller.uid,
        ARTBOX_SERVICE_ADD,name.data(),name.size());
}
bool Access::canList(const CallingContext &caller) {
    return artbox_service_policy_allows(policy,caller.debugPid,caller.uid,
        ARTBOX_SERVICE_LIST,nullptr,0);
}
}
