#pragma once

#define FEATURE_SSE3                0x1, 2, 0
#define FEATURE_MON                 0x1, 2, 3
#define FEATURE_VMX                 0x1, 2, 5
#define FEATURE_TM2                 0x1, 2, 8
#define FEATURE_SSSE3               0x1, 2, 9
#define FEATURE_PDCM                0x1, 2, 15
#define FEATURE_PCID                0x1, 2, 17
#define FEATURE_SSE4_1              0x1, 2, 19
#define FEATURE_SSE4_2              0x1, 2, 20
#define FEATURE_X2APIC              0x1, 2, 21
#define FEATURE_TSC_DEADLINE        0x1, 2, 24
#define FEATURE_AESNI               0x1, 2, 25
#define FEATURE_XSAVE               0x1, 2, 26
#define FEATURE_AVX                 0x1, 2, 28
#define FEATURE_RDRAND              0x1, 2, 30
#define FEATURE_HYPERVISOR          0x1, 2, 31
#define FEATURE_FPU                 0x1, 3, 0
#define FEATURE_SEP                 0x1, 3, 11
#define FEATURE_PGE                 0x1, 3, 13
#define FEATURE_CLFLUSH             0x1, 3, 19
#define FEATURE_ACPI                0x1, 3, 22
#define FEATURE_MMX                 0x1, 3, 23
#define FEATURE_FXSR                0x1, 3, 24
#define FEATURE_SSE                 0x1, 3, 25
#define FEATURE_SSE2                0x1, 3, 26
#define FEATURE_TM                  0x1, 3, 29
#define FEATURE_DTS                 0x6, 0, 0
#define FEATURE_TURBO               0x6, 0, 1
#define FEATURE_PLN                 0x6, 0, 4
#define FEATURE_PTM                 0x6, 0, 6
#define FEATURE_HWP                 0x6, 0, 7
#define FEATURE_HWP_NOT             0x6, 0, 8
#define FEATURE_HWP_ACT             0x6, 0, 9
#define FEATURE_HWP_PREF            0x6, 0, 10
#define FEATURE_TURBO_MAX           0x6, 0, 14
#define FEATURE_HW_FEEDBACK         0x6, 2, 0
#define FEATURE_PERF_BIAS           0x6, 2, 3
#define FEATURE_FSGSBASE            0x7, 1, 0
#define FEATURE_TSC_ADJUST          0x7, 1, 1
#define FEATURE_AVX2                0x7, 1, 5
#define FEATURE_SMEP                0x7, 1, 7
#define FEATURE_ERMS                0x7, 1, 9
#define FEATURE_INVPCID             0x7, 1, 10
#define FEATURE_AVX512F             0x7, 1, 16
#define FEATURE_AVX512DQ            0x7, 1, 17
#define FEATURE_RDSEED              0x7, 1, 18
#define FEATURE_SMAP                0x7, 1, 20
#define FEATURE_AVX512IFMA          0x7, 1, 21
#define FEATURE_CLFLUSHOPT          0x7, 1, 23
#define FEATURE_CLWB                0x7, 1, 24
#define FEATURE_PT                  0x7, 1, 25
#define FEATURE_AVX512PF            0x7, 1, 26
#define FEATURE_AVX512ER            0x7, 1, 27
#define FEATURE_AVX512CD            0x7, 1, 28
#define FEATURE_AVX512BW            0x7, 1, 30
#define FEATURE_AVX512VL            0x7, 1, 31
#define FEATURE_AVX512VBMI          0x7, 2, 1
#define FEATURE_UMIP                0x7, 2, 2
#define FEATURE_PKU                 0x7, 2, 3
#define FEATURE_AVX512VBMI2         0x7, 2, 6
#define FEATURE_AVX512VNNI          0x7, 2, 11
#define FEATURE_AVX512BITALG        0x7, 2, 12
#define FEATURE_AVX512VPDQ          0x7, 2, 14
#define FEATURE_LA57                0x7, 2, 17
#define FEATURE_AVX512QVNNIW        0x7, 3, 2
#define FEATURE_AVX512QFMA          0x7, 3, 3
#define FEATURE_MD_CLEAR            0x7, 3, 10
#define FEATURE_IBRS_IBPB           0x7, 3, 26
#define FEATURE_STIBP               0x7, 3, 27
#define FEATURE_L1D_FLUSH           0x7, 3, 28
#define FEATURE_ARCH_CAPABILITIES   0x7, 3, 29
#define FEATURE_SSBD                0x7, 3, 31
#define FEATURE_KVM_PV_CLOCK        0x40000001, 0, 3
#define FEATURE_KVM_PV_EOI          0x40000001, 0, 6
#define FEATURE_KVM_PV_IPI          0x40000001, 0, 11
#define FEATURE_KVM_PV_CLOCK_STABLE 0x40000001, 0, 24
#define FEATURE_AMD_TOPO            0x80000001, 2, 22
#define FEATURE_SYSCALL             0x80000001, 3, 11
#define FEATURE_NX                  0x80000001, 3, 20
#define FEATURE_HUGE_PAGE           0x80000001, 3, 26
#define FEATURE_RDTSCP              0x80000001, 3, 27
#define FEATURE_INVAR_TSC           0x80000007, 3, 8
#define FEATURE_INVLPGB             0x80000008, 1, 3
#define FEATURE_XSAVEOPT            0xD, 1, 0, 0
#define FEATURE_FPU_SAVE_SIZE       0xD, 0, 2
#define FEATURE_XCR0_LOW            0xD, 0, 0
#define FEATURE_XCR0_HIGH           0xD, 0, 3

namespace kernel::arch {
bool check_feature(unsigned int leaf, int reg_idx, int bit);
bool check_feature(unsigned int leaf, unsigned int subleaf, int reg_idx, int bit);

unsigned int get_cpuid_value(unsigned int leaf, unsigned int subleaf, int reg_idx);
}  // namespace kernel::arch