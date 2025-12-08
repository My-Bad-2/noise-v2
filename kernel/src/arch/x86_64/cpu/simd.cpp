#include "cpu/simd.hpp"
#include "cpu/registers.hpp"
#include "cpu/features.hpp"

namespace kernel::cpu::arch {
namespace {
inline void fxsave(void* buffer) {
    asm volatile("fxsave (%0)" ::"r"(buffer) : "memory");
}

inline void fxrstor(void* buffer) {
    asm volatile("fxrstor (%0)" ::"r"(buffer) : "memory");
}

inline void xsave(void* buffer) {
    constexpr uint32_t low  = 0xFFFFFFFF;
    constexpr uint32_t high = 0xFFFFFFFF;
    asm volatile("xsave (%0)" ::"r"(buffer), "a"(low), "d"(high) : "memory");
}

inline void xrstor(void* buffer) {
    constexpr uint32_t low  = 0xFFFFFFFF;
    constexpr uint32_t high = 0xFFFFFFFF;
    asm volatile("xrstor (%0)" ::"r"(buffer), "a"(low), "d"(high) : "memory");
}

inline void xsaveopt(void* buffer) {
    constexpr uint32_t low  = 0xFFFFFFFF;
    constexpr uint32_t high = 0xFFFFFFFF;
    asm volatile("xsaveopt (%0)" ::"r"(buffer), "a"(low), "d"(high) : "memory");
}
}  // namespace

FpuMode SIMD::mode = None;
uint32_t SIMD::save_size = 0;

void SIMD::init() {
    using namespace kernel::arch;

    if (check_feature(FEATURE_SSE)) {
        Cr0 cr0                 = Cr0::read();
        cr0.emulation           = false;
        cr0.monitor_coprocessor = true;
        cr0.numeric_error       = true;
        cr0.write();

        Cr4 cr4        = Cr4::read();
        cr4.osfxsr     = true;
        cr4.osxmmexcpt = true;
        cr4.write();

        Mxcsr mxcsr;
        mxcsr.raw = 0;
        mxcsr.invalid_operation_mask = true;
        mxcsr.denormal_mask          = true;
        mxcsr.divide_by_zero_mask    = true;
        mxcsr.overflow_mask          = true;
        mxcsr.underflow_mask         = true;
        mxcsr.precision_mask         = true;

        mxcsr.write();

        mode      = SSE;
        save_size = 512;
    }

    if (check_feature(FEATURE_XSAVE) && check_feature(FEATURE_AVX)) {
        Cr4 cr4     = Cr4::read();
        cr4.osxsave = true;
        cr4.write();

        Xcr0 xcr0;
        xcr0.raw = 0;
        xcr0.x87 = true;
        xcr0.sse = true;
        xcr0.avx = true;

        if (check_feature(FEATURE_AVX512F)) {
            xcr0.opmask    = true;
            xcr0.zmm_hi256 = true;
            xcr0.hi16_zmm  = true;
        }

        xcr0.write();
        mode = AVX;

        if (check_feature(FEATURE_XSAVEOPT)) {
            mode = AVXOpt;
        }

        save_size = get_cpuid_value(FEATURE_FPU_SAVE_SIZE);
    }
}

void SIMD::save(void* buffer) {
    switch (mode) {
        case AVXOpt:
            xsaveopt(buffer);
            break;
        case AVX:
            xsave(buffer);
            break;
        case SSE:
            fxsave(buffer);
            break;
        case LegacyX87:
        case None:
        default:
            break;
    }
}

void SIMD::restore(void* buffer) {
    switch (mode) {
        case AVXOpt:
        case AVX:
            xrstor(buffer);
            break;
        case SSE:
            fxrstor(buffer);
            break;
        case LegacyX87:
        case None:
        default:
            break;
    }
}
}  // namespace kernel::cpu::arch