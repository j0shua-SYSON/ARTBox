// Original contract for unchanged VINTF handling of an unavailable policy. MIT.
#include <vintf/RuntimeInfo.h>
#include <cerrno>
extern "C" int security_policyvers();
struct PolicyRuntimeInfo : android::vintf::RuntimeInfo {
    using RuntimeInfo::fetchAllInformation;
    void seed(size_t value) { mKernelSepolicyVersion=value; }
};
extern "C" int artbox_service_kernel_policy_check() {
    errno=0;
    if (security_policyvers()!=-1 || errno!=ENOTSUP) return -101;
    PolicyRuntimeInfo info;
    if (info.kernelSepolicyVersion()!=0) return -102;
    // The aggregate deliberately logs individual fetch errors and returns OK.
    // Preserve that original behavior; do not claim SELinux support from OK.
    if (info.fetchAllInformation(PolicyRuntimeInfo::POLICYVERS)!=0 || info.kernelSepolicyVersion()!=0) return -103;
    info.seed(47);
    if (info.fetchAllInformation(PolicyRuntimeInfo::POLICYVERS)!=0 || info.kernelSepolicyVersion()!=47) return -104;
    if (info.fetchAllInformation(PolicyRuntimeInfo::NONE)!=0 || info.kernelSepolicyVersion()!=47) return -105;
    return 5;
}
