#include "cpu/features.hpp"
#include "libs/log.hpp"
#include <cpuid.h>

namespace kernel::arch {

bool check_feature(unsigned int leaf, int reg_idx, int bit) {
    unsigned int eax, ebx, ecx, edx;

    // For unsupported leaves, we deliberately treat the feature as absent
    // and log at debug level so release builds stay quiet.
    if (!__get_cpuid(leaf, &eax, &ebx, &ecx, &edx)) {
        LOG_DEBUG("CPUID: leaf=0x%x unsupported; (reg=%d bit=%d) assumed absent", leaf, reg_idx,
                  bit);
        return false;
    }

    switch (reg_idx) {
        case 0:
            return (eax >> bit) & 1;
        case 1:
            return (ebx >> bit) & 1;
        case 2:
            return (ecx >> bit) & 1;
        case 3:
            return (edx >> bit) & 1;
        default:
            // Invalid selector is a caller bug; warn once and pretend
            // the feature is not present to keep callers robust.
            LOG_WARN("CPUID: feature check with invalid reg_idx=%d", reg_idx);
            return false;
    }
}

bool check_feature(unsigned int leaf, unsigned int subleaf, int reg_idx, int bit) {
    unsigned int eax, ebx, ecx, edx;

    if (!__get_cpuid_count(leaf, subleaf, &eax, &ebx, &ecx, &edx)) {
        LOG_DEBUG("CPUID: leaf=0x%x subleaf=0x%x unsupported; (reg=%d bit=%d) assumed absent", leaf,
                  subleaf, reg_idx, bit);
        return false;
    }

    switch (reg_idx) {
        case 0:
            return (eax >> bit) & 1;
        case 1:
            return (ebx >> bit) & 1;
        case 2:
            return (ecx >> bit) & 1;
        case 3:
            return (edx >> bit) & 1;
        default:
            LOG_WARN("CPUID: feature check with invalid reg_idx=%d", reg_idx);
            return false;
    }
}

unsigned int get_cpuid_value(unsigned int leaf, unsigned int subleaf, int reg_idx) {
    unsigned int eax, ebx, ecx, edx;

    if (!__get_cpuid_count(leaf, subleaf, &eax, &ebx, &ecx, &edx)) {
        // For numeric queries, unsupported leaves simply return 0;
        // callers should interpret that as "no data".
        LOG_DEBUG("CPUID: leaf=0x%x subleaf=0x%x unsupported; reg=%d -> 0", leaf, subleaf, reg_idx);
        return 0;
    }

    switch (reg_idx) {
        case 0:
            return eax;
        case 1:
            return ebx;
        case 2:
            return ecx;
        case 3:
            return edx;
        default:
            LOG_WARN("CPUID: value query with invalid reg_idx=%d", reg_idx);
            return 0;
    }
}
}  // namespace kernel::arch