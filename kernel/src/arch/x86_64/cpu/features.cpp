#include "cpu/features.hpp"
#include "libs/log.hpp"
#include <cpuid.h>

namespace kernel::arch {
// NOLINTNEXTLINE
bool check_feature(unsigned int leaf, int reg_idx, int bit) {
    unsigned int eax, ebx, ecx, edx;

    // If CPUID for this leaf is not supported, we conservatively report
    // "feature absent" and avoid relying on that capability.
    if (!__get_cpuid(leaf, &eax, &ebx, &ecx, &edx)) {
        LOG_DEBUG("CPUID leaf=0x%x unsupported; feature (reg=%d bit=%d) assumed absent",
                  leaf, reg_idx, bit);
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
            // Invalid register index: treat as "feature not present".
            LOG_WARN("CPUID feature check with invalid reg_idx=%d", reg_idx);
            return false;
    }
}
}  // namespace kernel::arch